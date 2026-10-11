#include "retained_symbol_layer.h"

#include "diagnostics.h"
#include "session.h"

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
#include <condition_variable>
#include <cstdint>
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

constexpr int atlas_width = 512;
constexpr int atlas_height = 512;
constexpr int atlas_gutter = 1;
constexpr std::size_t maximum_candidates = 1024u;
constexpr std::size_t maximum_symbols = 32u;
constexpr int maximum_tiles = 25;

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
};

struct Atlas {
    std::uint64_t generation = 0;
    std::vector<std::uint8_t> pixels;
    std::vector<PackedSymbol> symbols;
};

struct Rectangle {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct GlVertex {
    GLfloat x;
    GLfloat y;
    GLfloat u;
    GLfloat v;
};

bool overlaps(const Rectangle& a, const Rectangle& b)
{
    return a.left < b.right && a.right > b.left &&
           a.top < b.bottom && a.bottom > b.top;
}

void append_candidate(std::vector<Candidate>& output,
                      const std::shared_ptr<OsmAnd::MapSymbol>& symbol,
                      const OsmAnd::PointI& position,
                      const OsmAnd::PointI& offset,
                      std::uintptr_t instance)
{
    if (output.size() >= maximum_candidates || !symbol || symbol->isHidden)
        return;
    const auto raster = std::dynamic_pointer_cast<OsmAnd::RasterMapSymbol>(symbol);
    if (!raster || !raster->image)
        return;
    const int width = raster->image->width();
    const int height = raster->image->height();
    // Reject pathological resources before they consume atlas or draw budget.
    // Normal road/place captions fit 240x80 and map icons fit 96x96.
    if (width < 2 || height < 2 || width > 240 || height > 96 ||
        width * height > 24576)
        return;
    output.push_back({raster->image, position, offset, symbol->order, instance});
}

void append_group(std::vector<Candidate>& output,
                  const std::shared_ptr<OsmAnd::MapSymbolsGroup>& group)
{
    if (!group)
        return;
    const auto append_symbols = [&](const auto& symbols,
                                    std::uintptr_t instance) {
        for (const auto& symbol : symbols) {
            if (const auto billboard = std::dynamic_pointer_cast<
                    OsmAnd::BillboardRasterMapSymbol>(symbol)) {
                append_candidate(output, symbol, billboard->position31,
                                 billboard->offset, instance);
            } else if (const auto on_path = std::dynamic_pointer_cast<
                           OsmAnd::OnPathRasterMapSymbol>(symbol)) {
                append_candidate(output, symbol,
                                 on_path->pinPointOnPath.point31,
                                 OsmAnd::PointI(0, 0), instance);
            }
        }
    };
    if (!group->additionalInstancesDiscardOriginal)
        append_symbols(group->symbols,
                       reinterpret_cast<std::uintptr_t>(group.get()));
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
                append_candidate(output, symbol, position, offset, instance);
            } else if (const auto on_path = std::dynamic_pointer_cast<
                           OsmAnd::OnPathRasterMapSymbol>(symbol)) {
                auto position = on_path->pinPointOnPath.point31;
                if (const auto overrides = std::dynamic_pointer_cast<const
                        OsmAnd::MapSymbolsGroup::AdditionalOnPathSymbolInstanceParameters>(
                            parameters)) {
                    if (overrides->overridesPinPointOnPath)
                        position = overrides->pinPointOnPath.point31;
                }
                append_candidate(output, symbol, position,
                                 OsmAnd::PointI(0, 0), instance);
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
        key.left = std::max(0, center.x - 2);
        key.top = std::max(0, center.y - 2);
        key.right = std::min(maximum, center.x + 2);
        key.bottom = std::min(maximum, center.y + 2);
    }
    return key;
}

Atlas build_atlas(const WorkRequest& work,
                  OsmAnd::MapObjectsSymbolsProvider& provider,
                  const std::atomic<bool>& stopping)
{
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
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const Candidate& left, const Candidate& right) {
            return left.order < right.order;
        });

    atlas.pixels.assign(static_cast<std::size_t>(atlas_width) *
                            atlas_height * 4u, 0u);
    std::set<std::tuple<int, int, int, int, int>> seen;
    int cursor_x = atlas_gutter;
    int cursor_y = atlas_gutter;
    int row_height = 0;
    for (const auto& candidate : candidates) {
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
        atlas.symbols.push_back({
            candidate.position31, candidate.offset, candidate.order,
            width, height,
            static_cast<float>(cursor_x) / atlas_width,
            static_cast<float>(cursor_y) / atlas_height,
            static_cast<float>(cursor_x + width) / atlas_width,
            static_cast<float>(cursor_y + height) / atlas_height});
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
    Impl(std::shared_ptr<OsmAnd::MapObjectsSymbolsProvider> provider,
         std::shared_ptr<Wakeup> wakeup)
        : provider_(std::move(provider)), wakeup_(std::move(wakeup)),
          worker_([this]() { worker_loop(); })
    {
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
        for (const auto& symbol : active_.symbols) {
            OsmAnd::PointI screen;
            if (!renderer.obtainScreenPointFromPosition(
                    symbol.position31, screen, true))
                continue;
            const int center_x = screen.x + symbol.offset.x;
            const int center_y = screen.y + symbol.offset.y;
            const Rectangle rectangle{
                center_x - symbol.width / 2 - 2,
                center_y - symbol.height / 2 - 2,
                center_x + (symbol.width + 1) / 2 + 2,
                center_y + (symbol.height + 1) / 2 + 2};
            if (rectangle.right <= 0 || rectangle.bottom <= 0 ||
                rectangle.left >= static_cast<int>(width) ||
                rectangle.top >= static_cast<int>(height))
                continue;
            bool collision = false;
            for (const auto& placed : accepted_) {
                if (overlaps(rectangle, placed)) {
                    collision = true;
                    break;
                }
            }
            if (collision)
                continue;
            accepted_.push_back(rectangle);
            const float left = static_cast<float>(center_x - symbol.width / 2);
            const float top = static_cast<float>(center_y - symbol.height / 2);
            const float right = left + symbol.width;
            const float bottom = top + symbol.height;
            const float x0 = left * 2.0f / width - 1.0f;
            const float x1 = right * 2.0f / width - 1.0f;
            const float y0 = 1.0f - top * 2.0f / height;
            const float y1 = 1.0f - bottom * 2.0f / height;
            vertices_.insert(vertices_.end(), {
                {x0, y1, symbol.u0, symbol.v1},
                {x1, y1, symbol.u1, symbol.v1},
                {x0, y0, symbol.u0, symbol.v0},
                {x0, y0, symbol.u0, symbol.v0},
                {x1, y1, symbol.u1, symbol.v1},
                {x1, y0, symbol.u1, symbol.v0}});
        }
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
            Atlas result = build_atlas(work, *provider_, stopping_);
            if (stopping_.load())
                break;
            bool publish = false;
            const std::size_t published_count = result.symbols.size();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (work.generation == generation_) {
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

    const std::shared_ptr<OsmAnd::MapObjectsSymbolsProvider> provider_;
    const std::shared_ptr<Wakeup> wakeup_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stopping_{false};
    std::thread worker_;
    std::uint64_t generation_ = 0;
    RequestKey last_key_;
    bool has_last_key_ = false;
    WorkRequest pending_;
    bool has_pending_ = false;
    Atlas published_;
    bool has_published_ = false;
    Atlas active_;
    bool atlas_dirty_ = false;
    std::vector<GlVertex> vertices_;
    std::vector<Rectangle> accepted_;
    GLuint program_ = 0;
    GLuint texture_ = 0;
    GLuint vertex_buffer_ = 0;
    GLint texture_uniform_ = -1;
};

RetainedSymbolLayer::RetainedSymbolLayer(
    std::shared_ptr<OsmAnd::MapObjectsSymbolsProvider> provider,
    std::shared_ptr<Wakeup> wakeup)
    : impl_(std::make_unique<Impl>(std::move(provider), std::move(wakeup)))
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
