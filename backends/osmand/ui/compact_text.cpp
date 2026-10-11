#include "compact_text.h"

#include <cstdint>

namespace navicore_osmand::ui {
namespace {

void append_transliteration(std::string& output, char32_t value)
{
    switch (value) {
    case U'А': output += "A"; break; case U'а': output += "a"; break;
    case U'Б': output += "B"; break; case U'б': output += "b"; break;
    case U'В': output += "V"; break; case U'в': output += "v"; break;
    case U'Г': output += "H"; break; case U'г': output += "h"; break;
    case U'Ґ': output += "G"; break; case U'ґ': output += "g"; break;
    case U'Д': output += "D"; break; case U'д': output += "d"; break;
    case U'Е': output += "E"; break; case U'е': output += "e"; break;
    case U'Є': output += "Ye"; break; case U'є': output += "ye"; break;
    case U'Ё': output += "Yo"; break; case U'ё': output += "yo"; break;
    case U'Ж': output += "Zh"; break; case U'ж': output += "zh"; break;
    case U'З': output += "Z"; break; case U'з': output += "z"; break;
    case U'И': output += "Y"; break; case U'и': output += "y"; break;
    case U'І': output += "I"; break; case U'і': output += "i"; break;
    case U'Ї': output += "Yi"; break; case U'ї': output += "yi"; break;
    case U'Й': output += "Y"; break; case U'й': output += "y"; break;
    case U'К': output += "K"; break; case U'к': output += "k"; break;
    case U'Л': output += "L"; break; case U'л': output += "l"; break;
    case U'М': output += "M"; break; case U'м': output += "m"; break;
    case U'Н': output += "N"; break; case U'н': output += "n"; break;
    case U'О': output += "O"; break; case U'о': output += "o"; break;
    case U'П': output += "P"; break; case U'п': output += "p"; break;
    case U'Р': output += "R"; break; case U'р': output += "r"; break;
    case U'С': output += "S"; break; case U'с': output += "s"; break;
    case U'Т': output += "T"; break; case U'т': output += "t"; break;
    case U'У': output += "U"; break; case U'у': output += "u"; break;
    case U'Ф': output += "F"; break; case U'ф': output += "f"; break;
    case U'Х': output += "Kh"; break; case U'х': output += "kh"; break;
    case U'Ц': output += "Ts"; break; case U'ц': output += "ts"; break;
    case U'Ч': output += "Ch"; break; case U'ч': output += "ch"; break;
    case U'Ш': output += "Sh"; break; case U'ш': output += "sh"; break;
    case U'Щ': output += "Shch"; break; case U'щ': output += "shch"; break;
    case U'Ъ': case U'ъ': case U'Ь': case U'ь': break;
    case U'Ы': output += "Y"; break; case U'ы': output += "y"; break;
    case U'Э': output += "E"; break; case U'э': output += "e"; break;
    case U'Ю': output += "Yu"; break; case U'ю': output += "yu"; break;
    case U'Я': output += "Ya"; break; case U'я': output += "ya"; break;
    default: output += '?'; break;
    }
}

} // namespace

std::string compact_font_text(const std::string& utf8)
{
    std::string output;
    output.reserve(utf8.size());
    for (std::size_t index = 0; index < utf8.size();) {
        const auto first = static_cast<unsigned char>(utf8[index]);
        if (first < 0x80u) {
            output.push_back(first >= 0x20u || first == '\n' ?
                                 static_cast<char>(first) : '?');
            ++index;
            continue;
        }

        char32_t value = 0;
        std::size_t length = 0;
        if ((first & 0xe0u) == 0xc0u) {
            value = first & 0x1fu;
            length = 2;
        } else if ((first & 0xf0u) == 0xe0u) {
            value = first & 0x0fu;
            length = 3;
        } else if ((first & 0xf8u) == 0xf0u) {
            value = first & 0x07u;
            length = 4;
        } else {
            output += '?';
            ++index;
            continue;
        }
        if (index + length > utf8.size()) {
            output += '?';
            break;
        }
        bool valid = true;
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto byte = static_cast<unsigned char>(utf8[index + offset]);
            if ((byte & 0xc0u) != 0x80u) {
                valid = false;
                break;
            }
            value = (value << 6u) | (byte & 0x3fu);
        }
        if (!valid) {
            output += '?';
            ++index;
            continue;
        }
        append_transliteration(output, value);
        index += length;
    }
    return output;
}

} // namespace navicore_osmand::ui
