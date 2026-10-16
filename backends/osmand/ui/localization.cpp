#include "localization.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace navicore_osmand::ui::loc {
namespace {

std::atomic<int> g_language{static_cast<int>(Language::English)};

struct Row { const char* en; const char* uk; const char* fr; const char* es; };

// One row per Str, in enum order. A null or empty cell falls back to English.
const Row kStrings[] = {
    /* WhereTo */ {"Where to?", "Куди їдемо?", "Où allez-vous?", "¿A dónde vamos?"},
    /* Recent */ {"RECENT", "НЕЩОДАВНІ", "RÉCENTS", "RECIENTES"},
    /* NoRecent */ {"No recent destinations", "Немає нещодавніх місць",
                    "Aucune destination récente", "Sin destinos recientes"},
    /* NoRecentHint */ {"Places you navigate to appear here.",
                        "Тут з’являться місця, куди ви їздили.",
                        "Les lieux où vous allez apparaîtront ici.",
                        "Los lugares a los que navegues aparecerán aquí."},
    /* Close */ {"Close", "Закрити", "Fermer", "Cerrar"},
    /* Navigate */ {"Navigate", "Маршрут", "Itinéraire", "Ir"},
    /* Back */ {"Back", "Назад", "Retour", "Atrás"},
    /* Start */ {"Start", "Почати", "Démarrer", "Iniciar"},
    /* End */ {"End", "Стоп", "Fin", "Fin"},
    /* Done */ {"Done", "Готово", "Terminé", "Listo"},
    /* SearchPlaceholder */ {"Street, house or place", "Вулиця, будинок або місце",
                             "Rue, numéro civique ou lieu", "Calle, número o lugar"},
    /* TypingPaused */ {"Typing is paused while driving", "Введення вимкнено під час руху",
                        "Saisie suspendue en conduisant", "Escritura pausada al conducir"},
    /* TypingPausedHint */ {"Pick a recent destination, or stop to type.",
                            "Оберіть нещодавнє місце або зупиніться.",
                            "Choisissez une destination récente ou arrêtez-vous.",
                            "Elige un destino reciente o detente para escribir."},
    /* KeySpace */ {"space", "пробіл", "espace", "espacio"},
    /* TypeToSearch */ {"Type to search installed maps",
                        "Введіть запит для пошуку на встановлених картах",
                        "Saisissez pour chercher dans les cartes installées",
                        "Escribe para buscar en los mapas instalados"},
    /* MsgSearching */ {"Searching installed maps…", "Пошук на встановлених картах…",
                        "Recherche dans les cartes installées…",
                        "Buscando en los mapas instalados…"},
    /* MsgNoResults */ {"No offline results", "Нічого не знайдено на картах",
                        "Aucun résultat hors ligne", "Sin resultados sin conexión"},
    /* MsgCalculating */ {"Calculating route…", "Прокладання маршруту…",
                          "Calcul de l’itinéraire…", "Calculando ruta…"},
    /* MsgNoRoute */ {"No route available in installed maps",
                      "Маршрут не знайдено", "Aucun itinéraire trouvé",
                      "No se encontró una ruta"},
    /* DestinationFallback */ {"Destination", "Пункт призначення", "Destination", "Destino"},
    /* RouteFallback */ {"Route", "Маршрут", "Itinéraire", "Ruta"},
    /* Arrived */ {"You have arrived", "Ви прибули", "Arrivée à destination",
                   "Llegó a su destino"},
    /* Recalculating */ {"Recalculating…", "Перерахунок…",
                         "Recalcul de l’itinéraire…", "Recalculando ruta…"},
    /* OffRoute */ {"Off route · new route in a moment", "Ви поза маршрутом · новий буде за мить",
                    "Hors itinéraire · nouveau calcul en cours",
                    "Fuera de ruta · nueva ruta en un momento"},
    /* SimulationTag */ {"ROUTE SIMULATION", "СИМУЛЯЦІЯ", "SIMULATION", "SIMULACIÓN"},
    /* NoGps */ {"No GPS fix", "Немає сигналу GPS", "Pas de signal GPS", "Sin señal GPS"},
    /* NoGpsBody */ {"Guidance is paused until a position is available.",
                     "Навігацію призупинено, доки не буде визначено позицію.",
                     "Le guidage est suspendu jusqu’à l’obtention d’une position.",
                     "La guía se pausa hasta tener una posición."},
    /* NextOnRoute */ {"NEXT ON ROUTE", "ДАЛІ ПО МАРШРУТУ", "PROCHAINEMENT",
                       "SIGUIENTE EN LA RUTA"},
    /* EtaPaused */ {"ETA paused", "Пауза", "Pause", "Pausa"},
    /* ReasonFastest */ {"Fastest", "Найшвидший", "Le plus rapide", "Más rápida"},
    /* ReasonShorter */ {"Shorter", "Коротший", "Plus court", "Más corta"},
    /* ReasonAlternative */ {"Alternative", "Альтернативний", "Autre itinéraire", "Alternativa"},
    /* ReasonShortest */ {"Shortest", "Найкоротший", "Le plus court", "La más corta"},
    /* ReasonFewerTurns */ {"Fewer turns", "Менше поворотів", "Moins de virages", "Menos giros"},
    /* DroppedPin */ {"Dropped pin", "Позначене місце", "Repère placé", "Punto marcado"},
    /* TurnLeft */ {"Turn left", "Поверніть ліворуч", "Tournez à gauche", "Gire a la izquierda"},
    /* TurnSlightLeft */ {"Slight left", "Плавно поверніть ліворуч",
                          "Tournez légèrement à gauche", "Gire ligeramente a la izquierda"},
    /* TurnSharpLeft */ {"Sharp left", "Різко поверніть ліворуч",
                         "Tournez nettement à gauche", "Gire bruscamente a la izquierda"},
    /* TurnRight */ {"Turn right", "Поверніть праворуч", "Tournez à droite", "Gire a la derecha"},
    /* TurnSlightRight */ {"Slight right", "Плавно поверніть праворуч",
                           "Tournez légèrement à droite", "Gire ligeramente a la derecha"},
    /* TurnSharpRight */ {"Sharp right", "Різко поверніть праворуч",
                          "Tournez nettement à droite", "Gire bruscamente a la derecha"},
    /* KeepLeft */ {"Keep left", "Тримайтеся лівіше", "Gardez la gauche",
                    "Manténgase a la izquierda"},
    /* KeepRight */ {"Keep right", "Тримайтеся правіше", "Gardez la droite",
                     "Manténgase a la derecha"},
    /* UTurn */ {"Make a U-turn", "Розвернітеся", "Faites demi-tour", "Dé vuelta en U"},
    /* Roundabout */ {"Enter roundabout", "Заїдьте на кільце",
                      "Entrez dans le carrefour giratoire", "Entre a la glorieta"},
    /* Arrive */ {"Arriving at", "Прибуття до", "Arrivée à", "Llegando a"},
    /* Straight */ {"Continue", "Рухайтеся прямо", "Continuez tout droit", "Continúe derecho"},
    /* RoundaboutExit */ {nullptr, "На кільці — %d-й виїзд",
                          "Au carrefour giratoire, prenez la sortie %d",
                          "En la glorieta, tome la salida %d"},
    /* InDistance */ {"%s", "Через %s", "Dans %s", "En %s"},
    /* AddressPlace */ {"Place", "Місце", "Lieu", "Lugar"},
    /* AddressStreet */ {"Street", "Вулиця", "Rue", "Calle"},
    /* AddressBuilding */ {"Address", "Адреса", "Adresse", "Dirección"},
    /* AddressIntersection */ {"Intersection", "Перехрестя", "Intersection", "Intersección"},
    /* UnitMetre */ {"m", "м", "m", "m"},
    /* UnitKilometre */ {"km", "км", "km", "km"},
    /* UnitMinute */ {"min", "хв", "min", "min"},
    /* UnitHour */ {"h", "год", "h", "h"},
};
static_assert(sizeof(kStrings) / sizeof(kStrings[0]) == static_cast<std::size_t>(Str::Count_),
              "one row per Str");

const char* cell(const Row& row, Language language) noexcept
{
    const char* text = nullptr;
    switch (language) {
    case Language::English: text = row.en; break;
    case Language::Ukrainian: text = row.uk; break;
    case Language::FrenchCanadian: text = row.fr; break;
    case Language::SpanishMexican: text = row.es; break;
    }
    return text && *text ? text : row.en ? row.en : "";
}

// Point-of-interest and place categories by bare OSM value; map icon names
// ("amenity_fuel") are matched after dropping their key prefix.
struct Category { const char* key; const char* en; const char* uk; const char* fr; const char* es; };
const Category kCategories[] = {
    {"fuel", "Fuel", "АЗС", "Station-service", "Gasolinera"},
    {"charging_station", "Charging station", "Зарядна станція", "Borne de recharge",
     "Estación de carga"},
    {"services", "Services", "Зона обслуговування", "Halte routière", "Área de servicios"},
    {"rest_area", "Rest area", "Місце відпочинку", "Aire de repos", "Área de descanso"},
    {"hospital", "Hospital", "Лікарня", "Hôpital", "Hospital"},
    {"speed_camera", "Speed camera", "Камера швидкості", "Radar de vitesse",
     "Cámara de velocidad"},
    {"parking", "Parking", "Паркінг", "Stationnement", "Estacionamiento"},
    {"parking_entrance", "Parking entrance", "В’їзд на паркінг", "Entrée de stationnement",
     "Entrada de estacionamiento"},
    {"car_repair", "Car repair", "Автосервіс", "Garage", "Taller mecánico"},
    {"tyres", "Tyres", "Шиномонтаж", "Pneus", "Llantas"},
    {"car_wash", "Car wash", "Автомийка", "Lave-auto", "Autolavado"},
    {"compressed_air", "Compressed air", "Підкачка шин", nullptr, nullptr},
    {"vehicle_inspection", "Vehicle inspection", "Техогляд", nullptr, nullptr},
    {"car_parts", "Car parts", "Автозапчастини", "Pièces d’auto", "Refaccionaria"},
    {"car", "Car dealer", "Автосалон", "Concessionnaire", "Agencia de autos"},
    {"police", "Police", "Поліція", "Police", "Policía"},
    {"pharmacy", "Pharmacy", "Аптека", "Pharmacie", "Farmacia"},
    {"aerodrome", "Aerodrome", "Аеродром", "Aérodrome", "Aeródromo"},
    {"airport", "Airport", "Аеропорт", "Aéroport", "Aeropuerto"},
    {"station", "Railway station", "Залізнична станція", "Gare", "Estación de tren"},
    {"ferry_terminal", "Ferry terminal", "Поромний термінал", "Terminal de traversier",
     "Terminal de ferri"},
    {"toilets", "Toilets", "Туалет", "Toilettes", "Sanitarios"},
    {"motel", "Motel", "Мотель", "Motel", "Motel"},
    {"hotel", "Hotel", "Готель", "Hôtel", "Hotel"},
    {"guest_house", "Guest house", "Гостьовий будинок", "Maison d’hôtes", "Casa de huéspedes"},
    {"camp_site", "Campsite", "Кемпінг", "Terrain de camping", "Campamento"},
    {"supermarket", "Supermarket", "Супермаркет", "Supermarché", "Supermercado"},
    {"convenience", "Convenience store", "Магазин", "Dépanneur", "Tienda de conveniencia"},
    {"mall", "Shopping centre", "Торговий центр", "Centre commercial", "Centro comercial"},
    {"atm", "ATM", "Банкомат", "Guichet automatique", "Cajero automático"},
    {"bank", "Bank", "Банк", "Banque", "Banco"},
    {"fast_food", "Fast food", "Фастфуд", "Restauration rapide", "Comida rápida"},
    {"restaurant", "Restaurant", "Ресторан", "Restaurant", "Restaurante"},
    {"cafe", "Cafe", "Кафе", "Café", "Cafetería"},
    {"clinic", "Clinic", "Клініка", "Clinique", "Clínica"},
    {"doctors", "Doctors", "Лікарський кабінет", "Cabinet médical", "Consultorio médico"},
    {"bus_station", "Bus station", "Автовокзал", "Gare d’autocars", "Central de autobuses"},
    {"subway_entrance", "Metro entrance", "Вхід до метро", "Entrée de métro",
     "Entrada del metro"},
    {"marketplace", "Marketplace", "Ринок", "Marché", "Mercado"},
    {"museum", "Museum", "Музей", "Musée", "Museo"},
    {"attraction", "Attraction", "Визначне місце", "Attraction", "Atracción"},
    {"theatre", "Theatre", "Театр", "Théâtre", "Teatro"},
    {"cinema", "Cinema", "Кінотеатр", "Cinéma", "Cine"},
    {"monument", "Monument", "Пам’ятник", "Monument", "Monumento"},
    {"memorial", "Memorial", "Меморіал", "Mémorial", "Memorial"},
    {"castle", "Castle", "Замок", "Château", "Castillo"},
    {"ruins", "Ruins", "Руїни", "Ruines", "Ruinas"},
    {"viewpoint", "Viewpoint", "Оглядовий майданчик", "Point de vue", "Mirador"},
    {"park", "Park", "Парк", "Parc", "Parque"},
    {"school", "School", "Школа", "École", "Escuela"},
    {"university", "University", "Університет", "Université", "Universidad"},
    {"library", "Library", "Бібліотека", "Bibliothèque", "Biblioteca"},
    {"post_office", "Post office", "Пошта", "Bureau de poste", "Oficina de correos"},
    {"fire_station", "Fire station", "Пожежна частина", "Caserne de pompiers",
     "Estación de bomberos"},
    {"place_of_worship", "Place of worship", "Храм", "Lieu de culte", "Lugar de culto"},
    {"townhall", "Town hall", "Ратуша", "Hôtel de ville", "Ayuntamiento"},
    {"bar", "Bar", "Бар", "Bar", "Bar"},
    {"pub", "Pub", "Паб", "Pub", "Pub"},
    {"bakery", "Bakery", "Пекарня", "Boulangerie", "Panadería"},
    // Places (map place names).
    {"city", "City", "Місто", "Ville", "Ciudad"},
    {"town", "Town", "Містечко", "Ville", "Pueblo"},
    {"village", "Village", "Село", "Village", "Pueblo"},
    {"hamlet", "Hamlet", "Хутір", "Hameau", "Caserío"},
    {"suburb", "Suburb", "Район", "Banlieue", "Suburbio"},
    {"neighbourhood", "Neighbourhood", "Квартал", "Quartier", "Colonia"},
    {"quarter", "Quarter", "Квартал", "Quartier", "Barrio"},
    {"borough", "Borough", "Район", "Arrondissement", "Delegación"},
    {"locality", "Locality", "Місцевість", "Localité", "Localidad"},
    {"isolated_dwelling", "Dwelling", "Садиба", "Habitation isolée", "Vivienda aislada"},
    {"farm", "Farm", "Ферма", "Ferme", "Granja"},
    {"square", "Square", "Площа", "Place", "Plaza"},
};

const char* const kKeyPrefixes[] = {"amenity", "shop", "tourism", "highway", "railway",
                                    "aeroway", "leisure", "historic", "natural", "building",
                                    "office", "craft", "man_made", "place"};

std::string lower(std::string text)
{
    for (auto& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

const Category* find_category(const std::string& key)
{
    for (const auto& category : kCategories)
        if (key == category.key)
            return &category;
    return nullptr;
}

std::string humanize(std::string key)
{
    for (auto& c : key)
        if (c == '_')
            c = ' ';
    if (!key.empty())
        key[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(key[0])));
    return key;
}

// Replaces the first "%d" or "%s" of a table pattern.
std::string substitute(const char* pattern, const std::string& value)
{
    std::string text = pattern;
    const auto at = text.find('%');
    if (at != std::string::npos && at + 1 < text.size())
        text.replace(at, 2, value);
    return text;
}

std::string trim(const std::string& text)
{
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

bool read_first_line(const std::string& path, std::string& out)
{
    if (path.empty())
        return false;
    std::FILE* file = std::fopen(path.c_str(), "r");
    if (!file)
        return false;
    char buffer[128] = {};
    const bool ok = std::fgets(buffer, sizeof(buffer), file) != nullptr;
    std::fclose(file);
    if (ok)
        out = buffer;
    return ok;
}

} // namespace

Language language() noexcept
{
    return static_cast<Language>(g_language.load(std::memory_order_relaxed));
}

void set_language(Language value) noexcept
{
    g_language.store(static_cast<int>(value), std::memory_order_relaxed);
}

const char* language_code(Language value) noexcept
{
    switch (value) {
    case Language::English: return "en";
    case Language::Ukrainian: return "uk";
    case Language::FrenchCanadian: return "fr-CA";
    case Language::SpanishMexican: return "es-MX";
    }
    return "en";
}

std::vector<std::string> name_languages(Language value)
{
    switch (value) {
    case Language::English: return {"en"};
    case Language::Ukrainian: return {"uk"};
    case Language::FrenchCanadian: return {"fr", "en"};
    case Language::SpanishMexican: return {"es", "en"};
    }
    return {"en"};
}

bool parse_language(const std::string& text, Language& out)
{
    std::string name = lower(trim(text));
    const auto cfg = name.rfind(".cfg");
    if (cfg != std::string::npos && cfg + 4 == name.size())
        name.erase(cfg);
    for (auto& c : name)
        if (c == '_')
            c = '-';
    if (name == "uk" || name == "uk-ua" || name == "ua" || name == "ukrainian") {
        out = Language::Ukrainian;
    } else if (name == "en" || name == "en-us" || name == "en-gb" || name == "english") {
        out = Language::English;
    } else if (name == "fr" || name == "fr-ca" || name == "french") {
        out = Language::FrenchCanadian;
    } else if (name == "es" || name == "es-mx" || name == "spanish") {
        out = Language::SpanishMexican;
    } else {
        return false;
    }
    return true;
}

Detected detect_language(const char* environment, const std::string& marker_path,
                         const std::string& hmi_path)
{
    Detected found;
    if (environment && parse_language(environment, found.language)) {
        found.source = "environment";
        return found;
    }
    std::string line;
    if (read_first_line(marker_path, line) && parse_language(line, found.language)) {
        found.source = "marker";
        return found;
    }
    if (read_first_line(hmi_path, line) && parse_language(line, found.language)) {
        found.source = "hmi";
        return found;
    }
    found.language = Language::English;
    return found;
}

const char* tr(Language value, Str key) noexcept
{
    const auto index = static_cast<std::size_t>(key);
    if (index >= static_cast<std::size_t>(Str::Count_))
        return "";
    return cell(kStrings[index], value);
}

const char* tr(Str key) noexcept { return tr(language(), key); }

const char* maneuver_action(ManeuverKind kind) noexcept
{
    switch (kind) {
    case ManeuverKind::Left: return tr(Str::TurnLeft);
    case ManeuverKind::SlightLeft: return tr(Str::TurnSlightLeft);
    case ManeuverKind::SharpLeft: return tr(Str::TurnSharpLeft);
    case ManeuverKind::Right: return tr(Str::TurnRight);
    case ManeuverKind::SlightRight: return tr(Str::TurnSlightRight);
    case ManeuverKind::SharpRight: return tr(Str::TurnSharpRight);
    case ManeuverKind::KeepLeft: return tr(Str::KeepLeft);
    case ManeuverKind::KeepRight: return tr(Str::KeepRight);
    case ManeuverKind::UTurn: return tr(Str::UTurn);
    case ManeuverKind::Roundabout: return tr(Str::Roundabout);
    case ManeuverKind::Arrive: return tr(Str::Arrive);
    case ManeuverKind::Straight: return tr(Str::Straight);
    }
    return tr(Str::Straight);
}

std::string roundabout_exit_text(int exit)
{
    if (language() == Language::English) {
        char text[96] = {};
        const char* suffix = "th";
        if (exit % 100 < 11 || exit % 100 > 13) {
            switch (exit % 10) {
            case 1: suffix = "st"; break;
            case 2: suffix = "nd"; break;
            case 3: suffix = "rd"; break;
            default: break;
            }
        }
        std::snprintf(text, sizeof(text), "At the roundabout, take the %d%s exit", exit, suffix);
        return text;
    }
    return substitute(tr(Str::RoundaboutExit), std::to_string(exit));
}

Plural plural_form(Language value, long count) noexcept
{
    if (count < 0)
        count = -count;
    if (value == Language::Ukrainian) {
        const long ones = count % 10;
        const long tens = count % 100;
        if (ones == 1 && tens != 11)
            return Plural::One;
        if (ones >= 2 && ones <= 4 && (tens < 12 || tens > 14))
            return Plural::Few;
        return Plural::Many;
    }
    return count == 1 ? Plural::One : Plural::Many;
}

namespace {
std::string with_decimal_separator(std::string text)
{
    if (language() == Language::Ukrainian || language() == Language::FrenchCanadian) {
        const auto point = text.find('.');
        if (point != std::string::npos)
            text[point] = ',';
    }
    return text;
}
} // namespace

std::string format_distance(double metres, Rounding rounding)
{
    metres = std::max(0.0, metres);
    double shown = metres;
    if (rounding == Rounding::Coarse)
        shown = std::round(metres / (metres < 100.0 ? 5.0 : 10.0)) *
                (metres < 100.0 ? 5.0 : 10.0);
    else if (rounding == Rounding::Tens)
        shown = std::round(metres / 10.0) * 10.0;
    char text[48] = {};
    if (rounding == Rounding::None ? metres < 1000.0 : shown < 1000.0) {
        std::snprintf(text, sizeof(text), "%.0f %s", shown, tr(Str::UnitMetre));
        return text;
    }
    std::snprintf(text, sizeof(text), "%.1f", metres / 1000.0);
    return with_decimal_separator(text) + " " + tr(Str::UnitKilometre);
}

std::string format_duration_minutes(int minutes)
{
    minutes = std::max(0, minutes);
    char text[48] = {};
    if (minutes < 60)
        std::snprintf(text, sizeof(text), "%d %s", minutes, tr(Str::UnitMinute));
    else
        std::snprintf(text, sizeof(text), "%d %s %02d %s", minutes / 60, tr(Str::UnitHour),
                      minutes % 60, tr(Str::UnitMinute));
    return text;
}

std::string format_in_distance(double metres)
{
    const std::string distance = format_distance(metres, Rounding::Coarse);
    // "In 0 m" reads wrong: at the manoeuvre the bare distance stays.
    return metres < 10.0 ? distance : substitute(tr(Str::InDistance), distance);
}

std::string arrival_countdown_text(int seconds)
{
    char text[128] = {};
    switch (language()) {
    case Language::Ukrainian: {
        const Plural form = plural_form(Language::Ukrainian, seconds);
        const char* word = form == Plural::One ? "секунду"
                         : form == Plural::Few ? "секунди" : "секунд";
        std::snprintf(text, sizeof(text), "Повернення через %d %s", seconds, word);
        break;
    }
    case Language::FrenchCanadian:
        std::snprintf(text, sizeof(text), "Retour à la carte dans %d s", seconds);
        break;
    case Language::SpanishMexican:
        std::snprintf(text, sizeof(text), "Volviendo al mapa en %d s", seconds);
        break;
    default:
        std::snprintf(text, sizeof(text), "Returning to free drive in %d s", seconds);
        break;
    }
    return text;
}

std::string poi_category(const std::string& key_in)
{
    std::string key = lower(trim(key_in));
    // The whole key, then without a leading OSM key ("amenity_fuel" ->
    // "fuel", "tourism_guest_house" -> "guest_house").
    std::string rest = key;
    const auto split = key.find('_');
    if (!find_category(key) && split != std::string::npos) {
        const std::string prefix = key.substr(0, split);
        for (const char* known : kKeyPrefixes)
            if (prefix == known)
                rest = key.substr(split + 1);
    }
    if (const Category* category = find_category(rest)) {
        const Language value = language();
        const char* text = value == Language::Ukrainian ? category->uk
            : value == Language::FrenchCanadian ? category->fr
            : value == Language::SpanishMexican ? category->es : category->en;
        return text && *text ? text : category->en;
    }
    // Unknown: readable English without a known OSM key prefix.
    const auto underscore = key.find('_');
    if (underscore != std::string::npos && underscore + 1 < key.size()) {
        const std::string prefix = key.substr(0, underscore);
        for (const char* known : kKeyPrefixes)
            if (prefix == known)
                return humanize(key.substr(underscore + 1));
    }
    return humanize(key);
}

std::string route_reason(const std::string& english)
{
    static const struct { const char* english; Str key; } known[] = {
        {"Fastest", Str::ReasonFastest}, {"Shorter", Str::ReasonShorter},
        {"Alternative", Str::ReasonAlternative}, {"Shortest", Str::ReasonShortest},
        {"Fewer turns", Str::ReasonFewerTurns}};
    for (const auto& entry : known)
        if (english == entry.english)
            return tr(entry.key);
    return english;
}

std::string model_message(const std::string& english)
{
    static const struct { const char* english; Str key; } known[] = {
        {"Searching installed maps…", Str::MsgSearching},
        {"No offline results", Str::MsgNoResults},
        {"Calculating route…", Str::MsgCalculating},
        {"No route available in installed maps", Str::MsgNoRoute}};
    for (const auto& entry : known)
        if (english == entry.english)
            return tr(entry.key);
    return english;
}

} // namespace navicore_osmand::ui::loc
