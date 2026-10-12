#include "recents_store.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Deliberately stdio only. The backend links libstdc++ statically, but
// libOsmAndCore_shared.so exports its own libstdc++, and the linker binds our
// iostream calls to that copy, whose stream/locale state this plugin never
// initialises: the first std::ifstream crashed in std::ios_base::_M_init on
// the radio. Do not use <fstream>/<sstream>/<iostream> in this plugin.

namespace navicore_osmand::ui {
namespace {
std::string clean(std::string value)
{
    for (char& c : value)
        if (c == '\t' || c == '\n' || c == '\r')
            c = ' ';
    return value;
}

bool parse_double(const std::string& text, double& value)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    value = std::strtod(text.c_str(), &end);
    return end && *end == '\0' && std::isfinite(value);
}

std::vector<std::string> split_tabs(const std::string& line)
{
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t tab = line.find('\t', start);
        fields.push_back(line.substr(start, tab == std::string::npos
                                                ? std::string::npos
                                                : tab - start));
        if (tab == std::string::npos)
            break;
        start = tab + 1;
    }
    return fields;
}
} // namespace

std::vector<Destination> RecentsStore::load() const
{
    std::vector<Destination> recents;
    std::FILE* input = std::fopen(path_.c_str(), "r");
    if (!input)
        return recents;
    char buffer[1024];
    std::string line;
    while (recents.size() < NavigationModel::maximum_recents &&
           std::fgets(buffer, sizeof(buffer), input)) {
        line.append(buffer);
        if (line.empty() || line.back() != '\n') {
            if (!std::feof(input))
                continue;   // long line: keep reading it
        }
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        const auto fields = split_tabs(line);
        line.clear();
        Destination destination;
        if (fields.size() != 5 || fields[1].empty() ||
            !parse_double(fields[3], destination.latitude) ||
            !parse_double(fields[4], destination.longitude) ||
            std::fabs(destination.latitude) > 90.0 ||
            std::fabs(destination.longitude) > 180.0)
            continue;
        destination.id = fields[0];
        destination.title = fields[1];
        destination.subtitle = fields[2];
        recents.push_back(std::move(destination));
    }
    std::fclose(input);
    return recents;
}

bool RecentsStore::save(const std::vector<Destination>& recents) const
{
    const std::string temporary = path_ + ".tmp";
    std::FILE* output = std::fopen(temporary.c_str(), "w");
    if (!output)
        return false;
    bool ok = true;
    for (const auto& item : recents) {
        ok = std::fprintf(output, "%s\t%s\t%s\t%.9g\t%.9g\n",
                          clean(item.id).c_str(), clean(item.title).c_str(),
                          clean(item.subtitle).c_str(), item.latitude,
                          item.longitude) > 0 && ok;
    }
    ok = std::fflush(output) == 0 && ok;
    ok = std::fclose(output) == 0 && ok;
    if (!ok) {
        std::remove(temporary.c_str());
        return false;
    }
    return std::rename(temporary.c_str(), path_.c_str()) == 0;
}

} // namespace navicore_osmand::ui
