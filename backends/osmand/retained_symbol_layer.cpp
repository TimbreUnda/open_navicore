#include "retained_symbol_layer.h"

#include "diagnostics.h"
#include "label_geometry.h"
#include "label_policy.h"
#include "session.h"
#include "thread_priority.h"

#include <OsmAndCore/ObfsCollection.h>
#include <OsmAndCore/Map/MapPresentationEnvironment.h>
#include <OsmAndCore/Map/MapPrimitivesProvider.h>
#include <OsmAndCore/Map/MapPrimitiviser.h>
#include <OsmAndCore/Map/ObfMapObjectsProvider.h>
#include <OsmAndCore/Map/ResolvedMapStyle.h>

#include <OsmAndCore/FunctorQueryController.h>
#include <OsmAndCore/IQueryController.h>
#include <OsmAndCore/Map/BillboardRasterMapSymbol.h>
#include <OsmAndCore/Map/IMapRenderer.h>
#include <OsmAndCore/Map/MapObjectsSymbolsProvider.h>
#include <OsmAndCore/Map/OnPathRasterMapSymbol.h>
#include <OsmAndCore/Map/RasterMapSymbol.h>
#include <OsmAndCore/Utilities.h>

#include <OsmAndCore/ignore_warnings_on_external_includes.h>
#include <SkImage.h>
#include <SkImageInfo.h>
#include <OsmAndCore/restore_internal_warnings.h>

#include <GLES2/gl2.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace navicore_osmand {
namespace {

constexpr int atlas_width = 1024;
constexpr int atlas_height = 512;
constexpr int atlas_gutter = 1;
constexpr std::size_t maximum_candidates = 1024u;
constexpr std::size_t maximum_symbols = 64u;
// 3x3 tiles around the target: each atlas must finish before the moving
// camera leaves it, or no labels are ever shown while driving.
constexpr int maximum_tiles = 9;

struct RequestKey {
    int zoom = -1;
    int left = 0;
    int top = 0;
    int right = -1;
    int bottom = -1;

    bool operator==(const RequestKey& that) const noexcept
    {
        return zoom == that.zoom && left == that.left && top == that.top &&
               right == that.right && bottom == that.bottom;
    }
};

struct WorkRequest {
    std::uint64_t generation = 0;
    RequestKey key;
    OsmAnd::MapState state;
};

struct Candidate {
    sk_sp<const SkImage> image;
    OsmAnd::PointI position31;
    OsmAnd::PointI offset;
    int order = 0;
    std::uintptr_t instance = 0;
    LabelDecision decision;
    bool on_path = false;
    // Street names: a window of the road path around the pin, so the render
    // thread can align the text with the road every frame.
    std::vector<OsmAnd::PointI> path31;
    int pin_segment = 0;
    float pin_t = 0.0f;
};

struct PackedSymbol {
    OsmAnd::PointI position31;
    OsmAnd::PointI offset;
    int order = 0;
    int width = 0;
    int height = 0;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    LabelClass label_class = LabelClass::Drop;
    bool on_path = false;
    std::vector<OsmAnd::PointI> path31;
    int pin_segment = 0;
    float pin_t = 0.0f;
};

struct Atlas {
    std::uint64_t generation = 0;
    std::vector<std::uint8_t> pixels;
    std::vector<PackedSymbol> symbols;
};

struct GlVertex {
    GLfloat x;
    GLfloat y;
    GLfloat u;
    GLfloat v;
};

// Tags that decide whether a label matters to a driver.
struct GroupTags {
    std::string highway;
    std::string place;
    std::string amenity;
};

GroupTags group_tags(const std::shared_ptr<OsmAnd::MapSymbolsGroup>& group)
{
    GroupTags tags;
    const auto object_group = std::dynamic_pointer_cast<
        OsmAnd::MapObjectsSymbolsProvider::MapObjectSymbolsGroup>(group);
    if (!object_group || !object_group->mapObject)
        return tags;
    static const QString highway = QStringLiteral("highway");
    static const QString place = QStringLiteral("place");
    static const QString amenity = QStringLiteral("amenity");
    const auto& object = *object_group->mapObject;
    tags.highway = object.getResolvedAttribute(QStringRef(&highway)).toStdString();
    tags.place = object.getResolvedAttribute(QStringRef(&place)).toStdString();
    tags.amenity = object.getResolvedAttribute(QStringRef(&amenity)).toStdString();
    return tags;
}

void append_candidate(std::vector<Candidate>& output, const GroupTags& tags,
                      const std::shared_ptr<OsmAnd::MapSymbol>& symbol,
                      const OsmAnd::PointI& position,
                      const OsmAnd::PointI& offset, std::uintptr_t instance,
                      const OsmAnd::OnPathRasterMapSymbol* on_path,
                      const OsmAnd::IOnPathMapSymbol::PinPoint* pin)
{
    if (output.size() >= maximum_candidates || !symbol || symbol->isHidden)
        return;
    const auto raster = std::dynamic_pointer_cast<OsmAnd::RasterMapSymbol>(symbol);
    if (!raster || !raster->image)
        return;
    LabelTags label;
    label.highway = tags.highway;
    label.place = tags.place;
    label.amenity = tags.amenity;
    label.on_path = on_path != nullptr;
    label.icon = symbol->contentClass == OsmAnd::MapSymbol::ContentClass::Icon;
    const LabelDecision decision = classify_label(label);
    if (decision.label_class == LabelClass::Drop)
        return;   // churches, libraries, shops, footways: never rasterized
    const int width = raster->image->width();
    const int height = raster->image->height();
    // Reject pathological resources before they consume atlas or draw budget.
    // Normal road/place captions fit 240x80 and map icons fit 96x96.
    if (width < 2 || height < 2 || width > 240 || height > 96 ||
        width * height > 24576)
        return;
    Candidate candidate{raster->image, position, offset, symbol->order,
                        instance, decision, on_path != nullptr, {}, 0, 0.0f};
    if (on_path && pin) {
        const auto shared = on_path->getPath31SharedRef();
        if (!shared || shared->size() < 2)
            return;
        // Up to 8 points either side of the pin: enough for the label's
        // length at street zooms, small enough to project every frame.
        const int count = shared->size();
        const int base = std::clamp(static_cast<int>(pin->basePathPointIndex),
                                    0, count - 2);
        const int first = std::max(0, base - 8);
        const int last = std::min(count - 1, base + 9);
        for (int i = first; i <= last; ++i)
            candidate.path31.push_back(shared->at(i));
        candidate.pin_segment = base - first;
        candidate.pin_t = pin->normalizedOffsetFromBasePathPoint;
    }
    output.push_back(std::move(candidate));
}

void append_group(std::vector<Candidate>& output,
                  const std::shared_ptr<OsmAnd::MapSymbolsGroup>& group)
{
    if (!group)
        return;
    const GroupTags tags = group_tags(group);
    if (tags.highway.empty() && tags.place.empty() && tags.amenity.empty())
        return;   // nothing a driver needs (buildings, landuse, ...)
    if (!group->additionalInstancesDiscardOriginal) {
        const auto instance = reinterpret_cast<std::uintptr_t>(group.get());
        for (const auto& symbol : group->symbols) {
            if (const auto billboard = std::dynamic_pointer_cast<
                    OsmAnd::BillboardRasterMapSymbol>(symbol)) {
                append_candidate(output, tags, symbol, billboard->position31,
                                 billboard->offset, instance, nullptr, nullptr);
            } else if (const auto on_path = std::dynamic_pointer_cast<
                           OsmAnd::OnPathRasterMapSymbol>(symbol)) {
                append_candidate(output, tags, symbol,
                                 on_path->pinPointOnPath.point31,
                                 OsmAnd::PointI(0, 0), instance, on_path.get(),
                                 &on_path->pinPointOnPath);
            }
        }
    }
    for (const auto& additional : group->additionalInstances) {
        if (!additional || output.size() >= maximum_candidates)
            break;
        const auto instance = reinterpret_cast<std::uintptr_t>(additional.get());
        for (auto it = additional->symbols.constBegin();
             it != additional->symbols.constEnd(); ++it) {
            const auto& symbol = it.key();
            const auto& parameters = it.value();
            if (const auto billboard = std::dynamic_pointer_cast<
                    OsmAnd::BillboardRasterMapSymbol>(symbol)) {
                auto position = billboard->position31;
                auto offset = billboard->offset;
                if (const auto overrides = std::dynamic_pointer_cast<const
                        OsmAnd::MapSymbolsGroup::AdditionalBillboardSymbolInstanceParameters>(
                            parameters)) {
                    if (overrides->overridesPosition31)
                        position = overrides->position31;
                    if (overrides->overridesOffset)
                        offset = overrides->offset;
                }
                append_candidate(output, tags, symbol, position, offset,
                                 instance, nullptr, nullptr);
            } else if (const auto on_path = std::dynamic_pointer_cast<
                           OsmAnd::OnPathRasterMapSymbol>(symbol)) {
                auto pin = on_path->pinPointOnPath;
                if (const auto overrides = std::dynamic_pointer_cast<const
                        OsmAnd::MapSymbolsGroup::AdditionalOnPathSymbolInstanceParameters>(
                            parameters)) {
                    if (overrides->overridesPinPointOnPath)
                        pin = overrides->pinPointOnPath;
                }
                append_candidate(output, tags, symbol, pin.point31,
                                 OsmAnd::PointI(0, 0), instance, on_path.get(),
                                 &pin);
            }
            if (output.size() >= maximum_candidates)
                break;
        }
    }
}

RequestKey request_key(const OsmAnd::MapState& state)
{
    RequestKey key;
    key.zoom = static_cast<int>(state.zoomLevel);
    if (key.zoom < 0 || key.zoom > 30 ||
        state.visibleBBox31.right() < state.visibleBBox31.left() ||
        state.visibleBBox31.bottom() < state.visibleBBox31.top())
        return {};
    const int shift = 31 - key.zoom;
    const int maximum = (1 << key.zoom) - 1;
    key.left = std::clamp(state.visibleBBox31.left() >> shift, 0, maximum);
    key.top = std::clamp(state.visibleBBox31.top() >> shift, 0, maximum);
    key.right = std::clamp(state.visibleBBox31.right() >> shift, 0, maximum);
    key.bottom = std::clamp(state.visibleBBox31.bottom() >> shift, 0, maximum);
    if ((key.right - key.left + 1) * (key.bottom - key.top + 1) >
        maximum_tiles) {
        const auto center = OsmAnd::Utilities::getTileId(
            state.target31, state.zoomLevel);
        key.left = std::max(0, center.x - 1);
        key.top = std::max(0, center.y - 1);
        key.right = std::min(maximum, center.x + 1);
        key.bottom = std::min(maximum, center.y + 1);
    }
    return key;
}

Atlas build_atlas(const WorkRequest& work,
                  OsmAnd::MapObjectsSymbolsProvider& provider,
                  const std::atomic<bool>& stopping)
{
    // OsmAnd dereferences request.queryController unconditionally
    // (MapObjectsSymbolsProvider_P::obtainData); a null one is a SIGSEGV.
    // This one also lets stop() abort extraction mid-tile.
    const auto controller = std::make_shared<OsmAnd::FunctorQueryController>(
        [&stopping](const OsmAnd::FunctorQueryController*) {
            return stopping.load();
        });
    Atlas atlas;
    atlas.generation = work.generation;
    std::vector<Candidate> candidates;
    for (int y = work.key.top; y <= work.key.bottom && !stopping.load(); ++y) {
        for (int x = work.key.left; x <= work.key.right && !stopping.load(); ++x) {
            OsmAnd::IMapTiledSymbolsProvider::Request request;
            request.tileId = OsmAnd::TileId::fromXY(x, y);
            request.zoom = static_cast<OsmAnd::ZoomLevel>(work.key.zoom);
            request.detailedZoom = work.state.zoomLevel;
            request.visibleArea31 = work.state.visibleBBox31;
            request.areaTime = 0;
            request.cacheOnly = false;
            request.mapState = work.state;
            request.queryController = controller;
            std::shared_ptr<OsmAnd::IMapTiledSymbolsProvider::Data> data;
            if (!provider.obtainTiledSymbols(request, data) || !data)
                continue;
            for (const auto& group : data->symbolsGroups) {
                append_group(candidates, group);
                if (candidates.size() >= maximum_candidates)
                    break;
            }
        }
    }
    if (stopping.load())
        return atlas;

    // Driver policy: per-class budgets, best road classes first. The same
    // street name repeats along long roads; keep one per text image.
    std::vector<LabelCandidate> policy;
    policy.reserve(candidates.size());
    for (const auto& candidate : candidates)
        policy.push_back({candidate.decision, candidate.order});
    const auto chosen = select_labels(policy, LabelBudget{});

    atlas.pixels.assign(static_cast<std::size_t>(atlas_width) *
                            atlas_height * 4u, 0u);
    std::set<std::tuple<int, int, int, int, int>> seen;
    int cursor_x = atlas_gutter;
    int cursor_y = atlas_gutter;
    int row_height = 0;
    for (const std::size_t index : chosen) {
        const auto& candidate = candidates[index];
        if (atlas.symbols.size() >= maximum_symbols || stopping.load())
            break;
        const int width = candidate.image->width();
        const int height = candidate.image->height();
        const auto identity = std::make_tuple(
            candidate.position31.x, candidate.position31.y,
            candidate.order, width, height);
        if (!seen.insert(identity).second)
            continue;
        if (cursor_x + width + atlas_gutter > atlas_width) {
            cursor_x = atlas_gutter;
            cursor_y += row_height + atlas_gutter;
            row_height = 0;
        }
        if (cursor_y + height + atlas_gutter > atlas_height)
            break;
        std::vector<std::uint8_t> pixels(
            static_cast<std::size_t>(width) * height * 4u);
        const auto info = SkImageInfo::Make(
            width, height, SkColorType::kRGBA_8888_SkColorType,
            SkAlphaType::kPremul_SkAlphaType);
        if (!candidate.image->readPixels(
                nullptr, info, pixels.data(),
                static_cast<std::size_t>(width) * 4u, 0, 0,
                SkImage::kDisallow_CachingHint))
            continue;
        for (int row = 0; row < height; ++row) {
            std::memcpy(
                atlas.pixels.data() +
                    (static_cast<std::size_t>(cursor_y + row) * atlas_width +
                     cursor_x) * 4u,
                pixels.data() + static_cast<std::size_t>(row) * width * 4u,
                static_cast<std::size_t>(width) * 4u);
        }
        PackedSymbol packed;
        packed.position31 = candidate.position31;
        packed.offset = candidate.offset;
        packed.order = candidate.order;
        packed.width = width;
        packed.height = height;
        packed.u0 = static_cast<float>(cursor_x) / atlas_width;
        packed.v0 = static_cast<float>(cursor_y) / atlas_height;
        packed.u1 = static_cast<float>(cursor_x + width) / atlas_width;
        packed.v1 = static_cast<float>(cursor_y + height) / atlas_height;
        packed.label_class = candidate.decision.label_class;
        packed.on_path = candidate.on_path;
        packed.path31 = candidate.path31;
        packed.pin_segment = candidate.pin_segment;
        packed.pin_t = candidate.pin_t;
        atlas.symbols.push_back(std::move(packed));
        cursor_x += width + atlas_gutter;
        row_height = std::max(row_height, height);
    }
    return atlas;
}

GLuint compile(GLenum kind, const char* source)
{
    const GLuint shader = glCreateShader(kind);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

class RetainedSymbolLayer::Impl {
public:
    Impl(SymbolSource source, std::shared_ptr<Wakeup> wakeup)
        : source_(std::move(source)), wakeup_(std::move(wakeup))
    {
        // Start only after every request/result field below has completed
        // construction. Starting in the member initializer races worker_loop()
        // against later members such as has_pending_ and generation_.
        worker_ = std::thread([this]() { worker_loop(); });
        trace("retained_symbols=thread_started");
    }

    ~Impl() { stop(); }

    void request(const OsmAnd::MapState& state)
    {
        const RequestKey key = request_key(state);
        if (key.zoom < 0 || key.right < key.left || key.bottom < key.top)
            return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (has_last_key_ && key == last_key_)
            return;
        last_key_ = key;
        has_last_key_ = true;
        pending_ = WorkRequest{++generation_, key, state};
        has_pending_ = true;
        condition_.notify_one();
    }

    bool present(OsmAnd::IMapRenderer& renderer,
                 std::uint32_t width, std::uint32_t height)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (has_published_) {
                active_ = std::move(published_);
                has_published_ = false;
                atlas_dirty_ = true;
                visible_.clear();
            }
        }
        if (width == 0u || height == 0u || active_.symbols.empty())
            return true;
        while (glGetError() != GL_NO_ERROR) {}
        if (!program_ && !initialize())
            return false;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture_);
        if (atlas_dirty_) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas_width, atlas_height,
                         0, GL_RGBA, GL_UNSIGNED_BYTE, active_.pixels.data());
            atlas_dirty_ = false;
        }

        vertices_.clear();
        vertices_.reserve(active_.symbols.size() * 6u);
        accepted_.clear();
        accepted_.reserve(active_.symbols.size());
        // Stable placement: last frame's survivors claim space first so a
        // small camera move does not swap which of two labels is shown.
        order_.clear();
        for (std::size_t i = 0; i < active_.symbols.size(); ++i)
            if (i < visible_.size() && visible_[i]) order_.push_back(i);
        for (std::size_t i = 0; i < active_.symbols.size(); ++i)
            if (i >= visible_.size() || !visible_[i]) order_.push_back(i);
        visible_.assign(active_.symbols.size(), 0u);
        PlacementStats frame_stats;
        for (const std::size_t index : order_) {
            const auto& symbol = active_.symbols[index];
            OrientedBox box;
            if (symbol.on_path && symbol.path31.size() >= 2) {
                // Street name: follow the road on screen, upright, and only
                // where the visible road is long and straight enough.
                // Project outward from the pin and keep the contiguous run
                // of points that project: the label only needs the road near
                // its pin, and one far point failing used to drop half of
                // the street names (seen live: 5-7 of 12 per frame).
                ++frame_stats.streets;
                const int count = static_cast<int>(symbol.path31.size());
                const int pin = std::clamp(symbol.pin_segment, 0, count - 2);
                const auto project = [&](int index, ScreenPoint& out) {
                    OsmAnd::PointI projected;
                    if (!renderer.obtainScreenPointFromPosition(
                            symbol.path31[index], projected, false))
                        return false;
                    out = {static_cast<float>(projected.x),
                           static_cast<float>(projected.y)};
                    return true;
                };
                ScreenPoint first;
                ScreenPoint second;
                if (!project(pin, first) || !project(pin + 1, second)) {
                    ++frame_stats.projection_failed;
                    continue;
                }
                backward_.clear();
                for (int i = pin - 1; i >= 0; --i) {
                    ScreenPoint point;
                    if (!project(i, point)) break;
                    backward_.push_back(point);
                }
                screen_path_.assign(backward_.rbegin(), backward_.rend());
                const int local_pin = static_cast<int>(screen_path_.size());
                screen_path_.push_back(first);
                screen_path_.push_back(second);
                for (int i = pin + 2; i < count; ++i) {
                    ScreenPoint point;
                    if (!project(i, point)) break;
                    screen_path_.push_back(point);
                }
                if (!place_along_path(screen_path_, local_pin,
                                      symbol.pin_t,
                                      static_cast<float>(symbol.width),
                                      static_cast<float>(symbol.height), box)) {
                    ++frame_stats.fit_rejected;
                    continue;
                }
            } else {
                OsmAnd::PointI screen;
                if (!renderer.obtainScreenPointFromPosition(
                        symbol.position31, screen, true))
                    continue;
                box = {static_cast<float>(screen.x + symbol.offset.x),
                       static_cast<float>(screen.y + symbol.offset.y),
                       symbol.width * 0.5f, symbol.height * 0.5f, 0.0f};
            }
            const float reach = std::max(box.half_width, box.half_height);
            if (box.cx + reach <= 0.0f || box.cy + reach <= 0.0f ||
                box.cx - reach >= static_cast<float>(width) ||
                box.cy - reach >= static_cast<float>(height)) {
                ++frame_stats.offscreen;
                continue;
            }
            bool collision = false;
            for (const auto& placed : accepted_) {
                if (boxes_overlap(box, placed)) {
                    collision = true;
                    break;
                }
            }
            if (collision) {
                ++frame_stats.collided;
                continue;
            }
            if (symbol.on_path)
                ++frame_stats.streets_drawn;
            ++frame_stats.drawn;
            accepted_.push_back(box);
            visible_[index] = 1u;
            ScreenPoint corners[4];   // text top-left, top-right, bottom-right, bottom-left
            box_corners(box, corners);
            const auto clip = [&](const ScreenPoint& point, float u, float v) {
                return GlVertex{point.x * 2.0f / width - 1.0f,
                                1.0f - point.y * 2.0f / height, u, v};
            };
            vertices_.insert(vertices_.end(), {
                clip(corners[3], symbol.u0, symbol.v1),
                clip(corners[2], symbol.u1, symbol.v1),
                clip(corners[0], symbol.u0, symbol.v0),
                clip(corners[0], symbol.u0, symbol.v0),
                clip(corners[2], symbol.u1, symbol.v1),
                clip(corners[1], symbol.u1, symbol.v0)});
        }
        report_placement(frame_stats);
        if (vertices_.empty())
            return true;
        glUseProgram(program_);
        glUniform1i(texture_uniform_, 0);
        glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices_.size() * sizeof(GlVertex)),
                     vertices_.data(), GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GlVertex), nullptr);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GlVertex),
                              reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glViewport(0, 0, static_cast<GLsizei>(width),
                   static_cast<GLsizei>(height));
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
        return glGetError() == GL_NO_ERROR;
    }

    void release(bool context_lost) noexcept
    {
        if (!context_lost) {
            if (texture_) glDeleteTextures(1, &texture_);
            if (vertex_buffer_) glDeleteBuffers(1, &vertex_buffer_);
            if (program_) glDeleteProgram(program_);
        }
        texture_ = 0;
        vertex_buffer_ = 0;
        program_ = 0;
        texture_uniform_ = -1;
        atlas_dirty_ = true;
    }

    void stop() noexcept
    {
        bool expected = false;
        if (!stopping_.compare_exchange_strong(expected, true))
            return;
        condition_.notify_all();
        if (worker_.joinable())
            worker_.join();
    }

private:
    bool initialize()
    {
        static const char* vertex_source =
            "attribute vec2 position;attribute vec2 uv;varying vec2 vuv;"
            "void main(){gl_Position=vec4(position,0.0,1.0);vuv=uv;}";
        static const char* fragment_source =
            "precision mediump float;uniform sampler2D atlas;varying vec2 vuv;"
            "void main(){gl_FragColor=texture2D(atlas,vuv);}";
        const GLuint vertex = compile(GL_VERTEX_SHADER, vertex_source);
        const GLuint fragment = compile(GL_FRAGMENT_SHADER, fragment_source);
        if (!vertex || !fragment) {
            if (vertex) glDeleteShader(vertex);
            if (fragment) glDeleteShader(fragment);
            return false;
        }
        program_ = glCreateProgram();
        glAttachShader(program_, vertex);
        glAttachShader(program_, fragment);
        glBindAttribLocation(program_, 0, "position");
        glBindAttribLocation(program_, 1, "uv");
        glLinkProgram(program_);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        GLint ok = GL_FALSE;
        glGetProgramiv(program_, GL_LINK_STATUS, &ok);
        if (ok != GL_TRUE) {
            release(false);
            return false;
        }
        texture_uniform_ = glGetUniformLocation(program_, "atlas");
        glGenBuffers(1, &vertex_buffer_);
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return glGetError() == GL_NO_ERROR;
    }

    void worker_loop()
    {
        (void)lower_current_thread_priority(2);
        std::shared_ptr<OsmAnd::MapObjectsSymbolsProvider> provider =
            source_.shared_provider;
        if (provider)
            trace("retained_symbols=shared_provider");
        else try {
            auto presentation =
                std::make_shared<OsmAnd::MapPresentationEnvironment>(
                    source_.style, 1.0f, 1.0f, 1.0f);
            presentation->setLocaleLanguageId(source_.locale);
            presentation->setSettings(source_.style_settings);
            auto primitiviser =
                std::make_shared<OsmAnd::MapPrimitiviser>(presentation);
            auto objects =
                std::make_shared<OsmAnd::ObfMapObjectsProvider>(source_.obfs);
            auto primitives = std::make_shared<OsmAnd::MapPrimitivesProvider>(
                objects, primitiviser, 256u);
            provider = std::make_shared<OsmAnd::MapObjectsSymbolsProvider>(
                primitives, 256u);
            trace("retained_symbols=private_provider_ready");
        } catch (...) {
            trace("error=retained_symbols_provider");
            return;
        }
        while (!stopping_.load()) {
            WorkRequest work;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this]() {
                    return stopping_.load() || has_pending_;
                });
                if (stopping_.load())
                    break;
                work = pending_;
                has_pending_ = false;
            }
            const auto build_started = std::chrono::steady_clock::now();
            Atlas result = build_atlas(work, *provider, stopping_);
            {
                std::size_t streets = 0, shields = 0, places = 0, pois = 0;
                for (const auto& symbol : result.symbols) {
                    switch (symbol.label_class) {
                    case LabelClass::Street: ++streets; break;
                    case LabelClass::Shield: ++shields; break;
                    case LabelClass::Place: ++places; break;
                    case LabelClass::DriverPoi: ++pois; break;
                    case LabelClass::Drop: break;
                    }
                }
                char message[160];
                std::snprintf(message, sizeof(message),
                              "retained_symbols=atlas streets=%zu shields=%zu "
                              "places=%zu pois=%zu",
                              streets, shields, places, pois);
                trace(message);
            }
            trace_result("retained_symbols=built_ms",
                         static_cast<std::uint64_t>(
                             std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - build_started)
                                 .count()),
                         result.symbols.size());
            if (stopping_.load())
                break;
            bool publish = false;
            const std::size_t published_count = result.symbols.size();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                // Publish the newest finished atlas even if the camera has
                // moved on: symbols are world-anchored and still correct,
                // and waiting for an exact match never publishes while
                // driving (seen live: one empty atlas in a 267 s drive).
                if (work.generation > published_generation_) {
                    published_generation_ = work.generation;
                    published_ = std::move(result);
                    has_published_ = true;
                    publish = true;
                }
            }
            if (publish && wakeup_)
                (void)wakeup_->request();
            if (publish)
                trace_result("retained_symbols=published", work.generation,
                             published_count);
        }
    }

    const SymbolSource source_;
    const std::shared_ptr<Wakeup> wakeup_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stopping_{false};
    std::thread worker_;
    std::uint64_t generation_ = 0;
    std::uint64_t published_generation_ = 0;
    RequestKey last_key_;
    bool has_last_key_ = false;
    WorkRequest pending_;
    bool has_pending_ = false;
    Atlas published_;
    bool has_published_ = false;
    Atlas active_;
    bool atlas_dirty_ = false;
    std::vector<GlVertex> vertices_;
    struct PlacementStats {
        std::size_t streets = 0;
        std::size_t projection_failed = 0;
        std::size_t fit_rejected = 0;
        std::size_t offscreen = 0;
        std::size_t collided = 0;
        std::size_t streets_drawn = 0;
        std::size_t drawn = 0;
    };
    // Traces the last frame's placement outcome every 5 s.
    void report_placement(const PlacementStats& stats)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - placement_reported_at_ < std::chrono::seconds(5))
            return;
        placement_reported_at_ = now;
        char message[200];
        std::snprintf(message, sizeof(message),
                      "retained_symbols=placement symbols=%zu streets=%zu "
                      "projection_failed=%zu fit_rejected=%zu offscreen=%zu "
                      "collided=%zu streets_drawn=%zu drawn=%zu",
                      active_.symbols.size(), stats.streets,
                      stats.projection_failed, stats.fit_rejected,
                      stats.offscreen, stats.collided, stats.streets_drawn,
                      stats.drawn);
        trace(message);
    }
    std::chrono::steady_clock::time_point placement_reported_at_{};
    std::vector<OrientedBox> accepted_;
    std::vector<ScreenPoint> screen_path_;
    std::vector<ScreenPoint> backward_;
    std::vector<std::size_t> order_;
    std::vector<std::uint8_t> visible_;
    GLuint program_ = 0;
    GLuint texture_ = 0;
    GLuint vertex_buffer_ = 0;
    GLint texture_uniform_ = -1;
};

RetainedSymbolLayer::RetainedSymbolLayer(SymbolSource source,
                                         std::shared_ptr<Wakeup> wakeup)
    : impl_(std::make_unique<Impl>(std::move(source), std::move(wakeup)))
{
}

RetainedSymbolLayer::~RetainedSymbolLayer() = default;

void RetainedSymbolLayer::request(const OsmAnd::MapState& state)
{
    impl_->request(state);
}

bool RetainedSymbolLayer::present(OsmAnd::IMapRenderer& renderer,
                                  std::uint32_t width,
                                  std::uint32_t height)
{
    return impl_->present(renderer, width, height);
}

void RetainedSymbolLayer::release(bool context_lost) noexcept
{
    impl_->release(context_lost);
}

void RetainedSymbolLayer::stop() noexcept
{
    impl_->stop();
}

} // namespace navicore_osmand
