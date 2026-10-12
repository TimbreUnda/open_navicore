#include "recents_store.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

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
} // namespace

std::vector<Destination> RecentsStore::load() const
{
    std::vector<Destination> recents;
    std::ifstream input(path_);
    std::string line;
    while (input && std::getline(input, line) &&
           recents.size() < NavigationModel::maximum_recents) {
        std::vector<std::string> fields;
        std::stringstream stream(line);
        std::string field;
        while (std::getline(stream, field, '\t'))
            fields.push_back(field);
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
    return recents;
}

bool RecentsStore::save(const std::vector<Destination>& recents) const
{
    const std::string temporary = path_ + ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output)
            return false;
        output.precision(9);
        for (const auto& item : recents)
            output << clean(item.id) << '\t' << clean(item.title) << '\t'
                   << clean(item.subtitle) << '\t' << item.latitude << '\t'
                   << item.longitude << '\n';
        output.flush();
        if (!output)
            return false;
    }
    return std::rename(temporary.c_str(), path_.c_str()) == 0;
}

} // namespace navicore_osmand::ui
