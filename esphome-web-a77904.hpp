#include <ctime>
#include <cstdio>
#include <cmath>
#include <cctype>
#include <cstdlib>

#include <esp_task_wdt.h>

#include "esphome/components/json/json_util.h"
#include "esphome/components/display/display.h"
#include "esphome/core/time.h"

namespace customcode {

static const char *const TAG = "customcode";

static esphome::Color BLACK = Color(0,0,0);
static esphome::Color GREY = Color(192,192,192);
static esphome::Color WHITE = Color(255,255,255);

std::tm* time_tm(bool gmt=false, std::time_t* time_out=nullptr);
std::vector<std::time_t> helper_calendar_range(const std::tm& in, bool fullrange=false);
std::string strftime(const char* format, const std::time_t& t, bool gmt=false);
time_t parse_iso_date_to_local(const char* str);

struct CalendarEvent {
    std::time_t start;
    std::time_t end;
    bool is_all_day;
    std::string summary;
    std::string location;
};

struct Forecast {
    std::time_t time;
    float temperature;
    float temperature_low;
    float precipitation;
    std::string condition;
};

std::vector<CalendarEvent> extract_json_calendar_events() {
    std::vector<CalendarEvent> ret;
    std::string& calendar_json = id(sensor_calendar).state;
    if (1 < calendar_json.length()) {
        esphome::json::parse_json(calendar_json.c_str(), [&](JsonObject root) {
            for (JsonPair cal : root) {
                for (JsonObject event : cal.value()["events"].as<JsonArray>()) {
                    CalendarEvent add{};
                    const char* start = event["start"];
                    const char* end = event["end"];
                    add.is_all_day = strlen(start) <= 10;
                    add.start = parse_iso_date_to_local(start);
                    add.end = parse_iso_date_to_local(end);
                    add.summary = event["summary"].as<std::string>();
                    if (event.containsKey("location")) {
                        add.location = event["location"].as<std::string>();
                    }
                    ret.push_back(add);
                }
            }
            return true;
        });
    }
    std::sort(ret.begin(), ret.end(), [](CalendarEvent& a, CalendarEvent& b) { return a.start < b.start; });
    return ret;
}

std::vector<Forecast> extract_json_forecast(esphome::text_sensor::TextSensor& sensor) {
    std::vector<Forecast> ret;
    std::string& forecast_json = sensor.state;
    std::string wrapper = "{\"d\":" + forecast_json + "}";
    if (1 < forecast_json.length()) {
        esphome::json::parse_json(wrapper.c_str(), [&](JsonObject root) {
            for (JsonObject fc : root["d"].as<JsonArray>()) {
                Forecast add{};
                if (fc.containsKey("datetime")) {
                    add.time = parse_iso_date_to_local(fc["datetime"]);
                }
                if (fc.containsKey("temperature")) {
                    add.temperature = fc["temperature"];
                }
                if (fc.containsKey("templow")) {
                    add.temperature_low = fc["templow"];
                }
                if (fc.containsKey("precipitation_probability")) {
                    add.precipitation = fc["precipitation_probability"];
                }
                if (fc.containsKey("condition")) {
                    add.condition = fc["condition"].as<std::string>();
                }
                ret.push_back(add);
            }
            return true;
        });
    }
    std::sort(ret.begin(), ret.end(), [](Forecast& a, Forecast& b) { return a.time < b.time; });
    return ret;
}

std::vector<std::string> extract_json_tasks() {
    std::vector<std::string> ret;
    std::string& tasks_json = id(sensor_tasks).state;
    std::string wrapper = "{\"d\":" + tasks_json + "}";
    if (1 < tasks_json.length()) {
        esphome::json::parse_json(wrapper.c_str(), [&](JsonObject root) {
            for (JsonObject fc : root["d"].as<JsonArray>()) {
                if (fc.containsKey("subject")) {
                    ret.push_back(fc["subject"].as<std::string>());
                }
            }
            return true;
        });
    }
    return ret;
}

int days_from_epoch(int y, int m, int d)
{
    y -= m <= 2;
    int era = y / 400;
    int yoe = y - era * 400;                                   // [0, 399]
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;  // [0, 365]
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;           // [0, 146096]
    return era * 146097 + doe - 719468;
}

// It  does not modify broken-down time
time_t timegm(struct tm const* t)     
{
    int year = t->tm_year + 1900;
    int month = t->tm_mon;          // 0-11
    if (month > 11)
    {
        year += month / 12;
        month %= 12;
    }
    else if (month < 0)
    {
        int years_diff = (11 - month) / 12;
        year -= years_diff;
        month += 12 * years_diff;
    }
    int days_since_epoch = days_from_epoch(year, month + 1, t->tm_mday);

    return 60 * (60 * (24L * days_since_epoch + t->tm_hour) + t->tm_min) + t->tm_sec;
}

time_t parse_iso_date_to_local(const char* str) {
    std::tm tm{};
    tm.tm_wday = -1;
    tm.tm_yday = -1;
    tm.tm_isdst = -1;
    int year, mon, mday, hour, min;
    float sec;
    char tz_sign;
    int tz_hour = 0;
    int tz_min = 0;
    time_t tz_offset = 0;
    int match = sscanf(str, "%d-%d-%dT%d:%d:%f%c%d:%d", &year, &mon, &mday, &hour, &min, &sec, &tz_sign, &tz_hour, &tz_min);
    if (match < 3) {
        ESP_LOGE(TAG, "Failed to parse '%s' as iso date.", str);
        return 0;
    }
    tm.tm_year = year - 1900;
    tm.tm_mon = mon - 1;
    tm.tm_mday = mday;
    if (match < 6) {
        return mktime(&tm);
    }
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;
    if ( 8 < match) {
        if (tz_sign=='+') {
            tz_offset = - (tz_hour*60+tz_min)*60;
        } else if (tz_sign=='-') {
            tz_offset = (tz_hour*60+tz_min)*60;
        } else if (tz_sign!='Z') {
            ESP_LOGV(TAG, "Unknown char after iso date '%s': '%c'.", str, tz_sign);
        }
    }
    //assuming time_t is seconds
    time_t gmt = timegm(&tm);
    time_t local = mktime(&tm);
    return local + (gmt-local) + tz_offset;
}

std::tm* time_tm(bool gmt, std::time_t* time_out) {
    std::time_t t = std::time(nullptr);
    if (time_out != nullptr) {
        *time_out = t;
    }
    return gmt ? std::gmtime(&t) : std::localtime(&t);
}

bool is_nan(float f) {
    return (f != f);
}

bool is_leap_year(uint32_t year) { return (year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0); }

//Fixed 6 weeks for calendar display, end is eclusive
std::vector<std::time_t> helper_calendar_range(const std::tm& in, bool fullrange) {
    const uint8_t actual_first_weekday = 1; //0 for sunday, 1 for monday

    std::vector<std::time_t> ret;

    int8_t actual_wday = (in.tm_wday - actual_first_weekday );
    int8_t actual_wday_first = (actual_wday - in.tm_mday + 1 + 42) % 7;
    int16_t start = in.tm_yday - in.tm_mday + 1 - actual_wday_first + 1;

    uint8_t max = 6*7;
    uint8_t step = fullrange ? 1 : max;
    for (int16_t i=0; i<=max; i+=step) {
        // https://cplusplus.com/reference/ctime/mktime/ mktime is fixing stuff if mon=0 and mday=yday (can be negative as well)
        std::tm tm{};
        tm.tm_year = in.tm_year;
        tm.tm_mon = 0;
        tm.tm_mday = start+i;
        tm.tm_hour = 0;
        tm.tm_min = 0;
        tm.tm_sec = 0;
        tm.tm_isdst = -1;
        std::time_t time = std::mktime(&tm);
        ret.push_back(time);
    }
    
    return ret;
}

std::string strftime(const char* format, const std::time_t& t, bool gmt) {
    std::tm* tm = gmt ? std::gmtime(&t) : std::localtime(&t);
    char buf[64];
    std::strftime(buf, sizeof(buf), format, tm);
    return buf;
}

std::string get_nameday(uint16_t year, uint8_t month, uint8_t day)
{
    static const char* namedays[12][31] = {
        {"Fruzsina, ÚJÉV","Ábel","Benjámin, Genovéva","Leóna, Titusz","Simon","Boldizsár","Attila, Ramóna","Gyöngyvér","Marcell","Melánia","Ágota","Ernő","Veronika","Bódog","Lóránd, Lóránt","Gusztáv","Antal, Antónia","Piroska","Márió, Sára","Fábián, Sebestyén","Ágnes","Artúr, Vince","Rajmund, Zelma","Timót","Pál","Paula, Vanda","Angelika","Karola, Károly","Adél","Martina","Gerda, Marcella"},
        {"Ignác","Aida, Karolina","Balázs","Csenge, Ráhel","Ágota, Ingrid","Dóra, Dorottya","Rómeó, Tódor","Aranka","Abigél, Alex","Elvira","Bertold, Marietta","Lídia, Lívia","Ella, Linda","Bálint, Valentin","Georgina, Kolos","Julianna, Lilla","Donát","Bernadett","Zsuzsanna","Aladár, Álmos","Eleonóra","Gerzson","Alfréd","Mátyás","Géza","Edina","Ákos, Bátor","Elemér","","",""},
        {"Albin","Lujza","Kornélia","Kázmér","Adorján, Adrián","Inez, Leonóra","Tamás","Zoltán","Fanni, Franciska","Ildikó","Szilárd","Gergely","Krisztián, Ajtony","Matild","Kristóf","Henrietta","Gertrúd, Patrik","Ede, Sándor","Bánk, József","Klaudia","Benedek","Beáta, Izolda","Emőke","Gábor, Karina","Irén, Írisz","Emánuel","Hajnalka","Gedeon, Johanna","Auguszta","Zalán","Árpád"},
        {"Hugó","Áron","Buda, Richárd","Izidor","Vince","Bíborka, Vilmos","Herman","Dénes","Erhard","Zsolt","Leó, Szaniszló","Gyula","Ida","Tibor","Anasztázia, Tas","Csongor","Rudolf","Andrea, Ilma","Emma","Konrád, Tivadar","Konrád","Csilla, Noémi","Béla","György","Márk","Ervin","Zita","Valéria","Péter","Katalin, Kitti",""},
        {"Fülöp, Jakab","Zsigmond"," Irma, Tímea","Flórián, Mónika","Adrián, Györgyi","Frida, Ivett","Gizella","Mihály","Gergely","Ármin, Pálma","Ferenc","Pongrác","Imola, Szervác","Bonifác","Szonja, Zsófia","Botond, Mózes","Paszkál","Alexandra, Erik","Ivó, Milán","Bernát, Felícia","Konstantin","Júlia, Rita","Dezső","Eliza, Eszter","Orbán","Evelin, Fülöp","Hella","Csanád, Emil","Magdolna","Janka, Zsanett","Angéla"},
        {"Tünde","Anita, Kármen","Klotild","Bulcsú","Fatime","Cintia, Norbert","Róbert","Medárd","Félix","Gréta, Margit","Barnabás","Villő","Anett, Antal","Vazul","Jolán, Vid","Jusztin","Alida, Laura","Arnold, Levente","Gyárfás","Rafael","Alajos, Leila","Paulina","Zoltán","Iván","Vilmos","János, Pál","László","Irén, Levente","Péter, Pál","Pál",""},
        {"Annamária, Tihamér","Ottó","Kornél, Soma","Ulrik","Emese, Sarolta","Csaba","Appolónia","Ellák","Lukrécia","Amália","Lili, Nóra","Dalma, Izabella","Jenő","Örs, Stella","Henrik, Roland","Valter","Elek, Endre","Frigyes","Emília","Illés","Dániel, Daniella","Magdolna","Lenke","Kincső, Kinga","Jakab, Kristóf","Anikó, Anna","Liliána, Olga","Szabolcs","Flóra, Márta","Judit, Xénia","Oszkár"},
        {"Boglárka","Lehel","Hermina","Dominika, Dominik, Domonkos","Krisztina","Berta, Bettina","Ibolya","László","Emőd","Lörinc","Tiborc, Zsuzsanna","Klára","Ipoly","Marcell","Mária","Ábrahám","Jácint","Ilona","Huba","István","Hajna, Sámuel","Menyhért, Mirjam","Bence","Bertalan","Lajos, Patrícia","Izsó","Gáspár","Ágoston","Beatrix, Erna","Rózsa","Bella, Erika"},
        {"Egon, Egyed","Dorina, Rebeka","Hilda","Rozália","Lőrinc, Viktor","Zakariás","Regina","Adrienn, Mária","Ádám","Hunor, Nikolett","Teodóra","Mária","Kornél","Roxána, Szeréna","Enikő, Melitta","Edit","Zsófia","Diána","Vilhelmina","Friderika","Máté, Mirella","Móric","Tekla","Gellért, Mercédesz","Eufrozina, Kende","Jusztina, Pál","Adalbert","Vencel","Mihály","Jeromos",""},
        {"Malvin","Petra","Helga","Ferenc","Aurél","Brúnó, Renáta","Amália","Koppány","Dénes","Gedeon","Brigitta","Miksa","Ede, Kálmán","Helén","Teréz","Gál","Hedvig","Lukács","Nándor","Vendel","Orsolya","Előd","Gyöngyi","Salamon","Bianka, Blanka","Dömötör","Szabina","Simon, Szimonetta","Nárcisz","Alfonz","Farkas"},
        {"Marianna","Achilles","Győző","Károly","Imre","Lénárd","Rezső","Zsombor","Tivadar","Réka","Márton","Jónás, Renátó","Szilvia","Aliz","Albert, Lipót","Ödön","Gergely, Hortenzia","Jenő","Erzsébet, Elizabet","Jolán","Olivér","Cecília","Kelemen, Klementina","Emma","Katalin","Virág","Virgil","Stefánia","Taksony","Andor, András",""},
        {"Elza","Melinda, Vivien","Ferenc, Olívia","Barbara, Borbála","Vilma","Miklós","Ambrus","Mária","Natália","Judit","Árpád","Gabriella","Luca, Otília","Szilárda","Valér","Aletta, Etelka","Lázár, Olimpia","Auguszta","Viola","Teofil","Tamás","Zéno","Viktória","Ádám, Éva","Eugénia, KARÁCSONY","István, KARÁCSONY","János","Kamilla","Tamara, Tamás","Dávid","Szilveszter"}
    };
    // https://hu.wikipedia.org/wiki/Sz%C3%B6k%C5%91nap
    if (month==2 && 24<=day && is_leap_year(year)) {
        if (day == 24) {
            return "";
        }
        else {
            day -= 1;
        }
    }
    return namedays[month-1][day-1];
}

bool is_overlap(std::time_t x1, std::time_t x2, std::time_t y1, std::time_t y2) {
    return x1 < y2 && y1 < x2;
}

std::string capitalize(const std::string& input) {
    std::string ret = input;
    ret[0] = std::toupper(ret[0]);
    // UTF-8, https://hu.wikipedia.org/wiki/Magyar_%C3%A9kezetes_karakterek_k%C3%B3dk%C3%A9szletekben
    if (1<ret.length()) {
        if (ret[0] == 0xC3) {
            switch (ret[1]) {
                case 0xA1: // á
                case 0xA9: // é
                case 0xAD: // í
                case 0xB3: // ó
                case 0xB6: // ö
                case 0xBA: // ú
                case 0xBC: // ü
                    ret[1] -= 0x20;
                    break;
            }
        } else if (ret[0] == 0xC5) {
            switch (ret[1]) {
                case 0x91: // ő
                case 0xB1: // ű
                    ret[1] -= 0x01;
                    break;
            }
        }
    }
    return ret;
}

// units are as in std::tm struct
void render_calendar_today(esphome::display::Display& it, uint16_t x, uint16_t y, uint16_t now_year, uint8_t now_month, uint8_t now_mday, uint8_t now_wday) {
    static const char* days_full[] = {"Vasárnap", "Hétfő", "Kedd", "Szerda", "Csütörtök", "Péntek", "Szombat"};
    static const char* months_full[] = {"Január", "Február", "Március", "Április", "Május", "Június", "Július", "Augusztus", "Szeptember", "Október", "November", "December"};
    uint16_t now_year_full = now_year + 1900;
    it.printf(x, y, &id(verdana_28), BLACK, TextAlign::TOP_CENTER, "%d %s", now_year_full, months_full[now_month]);
    y = y+= 32;
    it.printf(x, y, &id(verdanab_86), BLACK, TextAlign::TOP_CENTER, "%d", now_mday);
    y = y+= 100;
    it.printf(x, y, &id(verdanab_28), BLACK, TextAlign::TOP_CENTER, "%s", days_full[now_wday]);
    y = y+= 32;
    it.printf(x, y, &id(verdana_28), BLACK, TextAlign::TOP_CENTER, "%s", get_nameday(now_year_full, now_month+1, now_mday).c_str());
}

std::string get_icon_from_condition(const std::string& condition, bool day=true) {
    // https://developers.home-assistant.io/docs/core/entity/weather/#recommended-values-for-state-and-condition
    static const std::map<std::string, std::string> icon_remap = {
        {"clear-night","\U000F0594"}, // mdi-weather-night
        {"cloudy","\U000F0590"}, // mdi-weather-cloudy
        {"exceptional","\U000F0F2F"}, // mdi-weather-cloudy-alert
        {"fog","\U000F0591"}, // mdi-weather-fog
        {"hail","\U000F0592"}, // mdi-weather-hail
        {"lightning","\U000F0593"}, // mdi-weather-lightning
        {"lightning-rainy","\U000F067E"}, // mdi-weather-lightning-rainy
        {"pouring","\U000F0596"}, // mdi-weather-pouring
        {"rainy","\U000F0597"}, // mdi-weather-rainy
        {"snowy","\U000F0598"}, // mdi-weather-snowy
        {"snowy-rainy","\U000F067F"}, // mdi-weather-snowy-rainy
        {"sunny","\U000F0599"}, // mdi-weather-sunny
        {"windy","\U000F059D"}, // mdi-weather-windy
        {"windy-variant","\U000F059E"} // mdi-weather-windy-variant
    };
    auto search = icon_remap.find(condition);
    if (search != icon_remap.end()) {
        return std::string(search->second);
    }
    if(condition == "partlycloudy") {
        if (day) {
            return "\U000F0595"; // mdi-weather-partly-cloudy
        } else {
            return "\U000F0F31"; // mdi-weather-night-partly-cloudy
        }
    }
    return "\U000F1BF9"; // mdi-cloud-question-outline
}

void render_calendar_calendar(
                                esphome::display::Display& it, uint16_t x, uint16_t y, 
                                const std::vector<std::time_t> &calendar, const std::vector<CalendarEvent> &events, 
                                uint8_t now_month, uint8_t now_mday, uint8_t now_wday
                                ) {
    static const char* days[] = {"V", "H", "K", "Sz", "Cs", "P", "Sz"};
    uint16_t xx;
    uint16_t yy;
    const uint8_t cal_box = 40;
    xx = x+cal_box/2;
    yy = y+cal_box/2;
    for (uint8_t i=0;i<7;i++) {
        if (i<calendar.size()) {
            std::tm* tm = std::localtime(&calendar[i]);
            it.printf(xx, yy, &id(verdanab_22), BLACK, TextAlign::CENTER, days[tm->tm_wday]);
            xx += cal_box;
        }
    }
    yy = y+cal_box/2+cal_box;
    for (uint8_t i=0;i<6;i++) {
        xx = x+cal_box/2;
        for (uint8_t j=0;j<7;j++) {
            uint8_t ij = i*7+j;
            if (ij<(calendar.size()-1)) {
                std::tm* tm = std::localtime(&calendar[ij]);
                esphome::Color color;
                bool busy = false;
                for (auto event : events) {
                    if (is_overlap(calendar[ij], calendar[ij+1], event.start, event.end)) {
                        busy = true;
                        break;
                    }
                }

                if (tm->tm_mon==now_month && tm->tm_mday==now_mday) 
                {
                    color = WHITE;
                    it.filled_rectangle(xx-cal_box/2, yy-cal_box/2, cal_box+1, cal_box+1, BLACK);
                } else {
                    color = (now_month == tm->tm_mon) ? BLACK : GREY;
                }
                it.printf(xx, yy, &id(verdanab_22), color, TextAlign::CENTER, "%d", tm->tm_mday);
                if (busy) {
                    it.rectangle(xx-cal_box/2+3, yy-cal_box/2+3, cal_box-5, cal_box-5, color);
                    if (color == WHITE) {
                        it.rectangle(xx-cal_box/2+4, yy-cal_box/2+4, cal_box-7, cal_box-7, color);
                    }
                }
                xx += cal_box;
            }
        }
        yy += cal_box;
    }
}

void render_weather_current(esphome::display::Display& it, uint16_t x, uint16_t y,
                            float now_temp, float today_temp_low, float today_temp_high, 
                            float now_precipitation, const std::string& condition, float sun
                            ) {
    if (is_nan(now_temp)) { now_temp = 0; }
    if (is_nan(today_temp_low)) { today_temp_low = 0; }
    if (is_nan(today_temp_high)) { today_temp_high = 0; }
    if (is_nan(now_precipitation)) { now_precipitation = 0; }
    uint16_t xx;
    uint16_t yy;
    xx = x + 64;
    yy = y;
    bool is_day = (sun > 0) || is_nan(sun); // NaN check
    it.print(xx, yy, &id(weather_128), BLACK, TextAlign::TOP_CENTER, get_icon_from_condition(condition, is_day).c_str());
    yy += 18;
    xx += 136;
    it.printf(xx, yy, &id(verdanab_48), BLACK, TextAlign::TOP_CENTER, "%d°", (int)std::round(now_temp));
    yy += 54;
    it.printf(xx, yy, &id(verdanab_22), BLACK, TextAlign::TOP_CENTER, "%d°/%d°", (int)std::round(today_temp_low), (int)std::round(today_temp_high));
    xx = x + 64;
    yy = y + 130;
    if (5 < now_precipitation) {
        it.printf(xx, yy, &id(verdana_22), BLACK, TextAlign::TOP_CENTER, "%d%%", (int)std::round(now_precipitation));
    }
}

void render_weather_forecast_hourly(esphome::display::Display& it, uint16_t x, uint16_t y,
                                    std::time_t nowt, const std::vector<Forecast> &forecast_hourly
                                    ) {
    uint16_t xx;
    uint16_t yy;
    const uint8_t fc_box = 70;
    xx = x+fc_box/2;
    uint8_t counter = 0;
    for (auto fc : forecast_hourly) {
        if (nowt<fc.time) {
            yy = y;
            std::tm* tm = std::localtime(&fc.time);
            it.printf(xx, yy, &id(verdanab_22), BLACK, TextAlign::TOP_CENTER, "%d", tm->tm_hour);
            yy += 24;
            //TODO: dynamically calculate sun elevation?
            std::string condition = fc.condition;
            if (condition == "partlycloudy") { condition="cloudy"; }
            it.print(xx, yy, &id(weather_60), BLACK, TextAlign::TOP_CENTER, get_icon_from_condition(condition).c_str());
            yy += 64;
            it.printf(xx, yy, &id(verdanab_22), BLACK, TextAlign::TOP_CENTER, "%d°", (int)std::round(fc.temperature));;
            yy += 24;
            if (5 < fc.precipitation) {
                it.printf(xx, yy, &id(verdana_22), BLACK, TextAlign::TOP_CENTER, "%d%%", (int)std::round(fc.precipitation));
            }
            xx += fc_box;
            if (++counter == 4) {
                break;
            }
        }
    }
}

void render_weather_forecast_daily(esphome::display::Display& it, uint16_t x, uint16_t y,
                                    std::time_t nowt, const std::vector<Forecast> &forecast_daily
                                    ) {
    static const char* days_full[] = {"Vasárnap", "Hétfő", "Kedd", "Szerda", "Csütörtök", "Péntek", "Szombat"};
    static const std::map<std::string, std::string> condition_remap = {
        {"clear-night","Tiszta éjszaka"},
        {"cloudy","Felhős"},
        {"exceptional","Rendkívüli"},
        {"fog","Köd"},
        {"hail","Jégeső"},
        {"lightning","Zivatar"},
        {"lightning-rainy","Zivatar és eső"},
        {"partlycloudy","Borús"},
        {"pouring","Felhőszakadás"},
        {"rainy","Eső"},
        {"snowy","Havazás"},
        {"snowy-rainy","Havas eső"},
        {"sunny","Napos"},
        {"windy","Szeles"},
        {"windy-variant","Szeles és felhős"}
    };
    uint16_t xx;
    uint16_t yy;
    const uint8_t row_height = 38;
    yy = y+row_height/2;
    uint8_t counter = 0;
    for (auto fc : forecast_daily) {
        if (nowt<fc.time) {
            xx = x;
            std::tm* tm = std::localtime(&fc.time);
            it.printf(xx, yy, &id(verdana_22), BLACK, TextAlign::LEFT, "%s", days_full[tm->tm_wday]);
            xx += 140;
            //TODO: dynamically calculate sun elevation?
            std::string condition = fc.condition;
            if (condition == "partlycloudy") { condition="cloudy"; }
            it.print(xx, yy, &id(weather_30), BLACK, TextAlign::LEFT, get_icon_from_condition(condition).c_str());
            xx += 70;
            it.printf(xx, yy, &id(verdana_22), BLACK, TextAlign::LEFT, "%d°", (int)std::round(fc.temperature_low));
            xx += 70;
            it.printf(xx, yy, &id(verdana_22), BLACK, TextAlign::LEFT, "%d°", (int)std::round(fc.temperature));
            xx += 70;
            condition = fc.condition;
            auto search = condition_remap.find(condition);
            if (search != condition_remap.end()) {
                condition = std::string(search->second);
            }
            it.printf(xx, yy, &id(verdana_22), BLACK, TextAlign::LEFT, "%s", condition.c_str());
            yy += row_height;
            if (7 <= ++counter) {
                break;
            }
        }
    }
}


void render_tasks(esphome::display::Display& it, uint16_t x, uint16_t y,
                    std::time_t nowt, std::vector<std::time_t> &calendar, 
                    std::vector<CalendarEvent> &events, std::vector<std::string> &tasks
                    ) {
    uint16_t xx;
    uint16_t yy;
    std::time_t today_start = 0;
    std::time_t today_end = 0;
    std::time_t tomorrow_end = 0;
    for (int i=0; i<(calendar.size()-2);i++) {
        if (calendar[i]<=nowt && nowt<calendar[i+1]) {
            today_start = calendar[i];
            today_end = calendar[i+1];
            tomorrow_end = calendar[i+2];
            break;
        }
    }
    auto format_date = [&](std::time_t t, bool include_year) -> std::string { 
        std::tm* tm = localtime(&t);
        char date[64];
        if (today_start <= t && t < today_end) {
            snprintf(date, sizeof(date), "Ma");
        } else if (today_end <= t && t < tomorrow_end) {
            snprintf(date, sizeof(date), "Holnap");
        } else if (include_year) {
            snprintf(date, sizeof(date), "%d.%02d.%02d.", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday);
        } else {
            snprintf(date, sizeof(date), "%02d.%02d.", tm->tm_mon+1, tm->tm_mday);
        }
        return std::string(date);
    };

    static const uint8_t row_height = 24;
    static const uint8_t column_width = 160;
    static const std::time_t day = 86400;
    yy = y + row_height/2;
    uint8_t counter = 0;
    for (auto event : events) {
        if (is_overlap(today_start, today_start+day*3, event.start, event.end)) {
            std::tm start = *localtime(&event.start);
            std::tm end = *localtime(&event.end);
            bool sameday = (start.tm_year == end.tm_year && start.tm_yday == end.tm_yday) ||
                (event.is_all_day && event.end < (event.start + day*1.5 ) );

            xx = x + column_width - 5;
            if (event.is_all_day && sameday) {
                it.printf(xx, yy, &id(verdana_22), BLACK, TextAlign::RIGHT, "%s", format_date(event.start, true).c_str());
            } else if (event.is_all_day) {
                it.printf(xx, yy, &id(verdanab_11), BLACK, TextAlign::RIGHT, "%s", format_date(event.start, true).c_str());
                it.printf(xx, yy+12, &id(verdanab_11), BLACK, TextAlign::RIGHT, "%s", format_date(event.end-day*0.5, true).c_str());
            } else if (sameday) {
                it.printf(xx, yy, &id(verdanab_11), BLACK, TextAlign::RIGHT, "%s %02d:%02d", format_date(event.start, true).c_str(), start.tm_hour, start.tm_min);
                it.printf(xx, yy+12, &id(verdanab_11), BLACK, TextAlign::RIGHT, "%02d:%02d", end.tm_hour, end.tm_min );
            } else {
                it.printf(xx, yy, &id(verdanab_11), BLACK, TextAlign::RIGHT, "%s %02d:%02d", format_date(event.start, true).c_str(), start.tm_hour, start.tm_min);
                it.printf(xx, yy+12, &id(verdanab_11), BLACK, TextAlign::RIGHT, "%s %02d:%02d", format_date(event.end, true).c_str(), end.tm_hour, end.tm_min );
            }
            xx = x + column_width + 5;
            it.printf(xx, yy, &id(verdana_22), BLACK, TextAlign::LEFT, "%s", event.summary.c_str());
            yy += row_height;
            if (9 <= ++counter) {
                break;
            }
        }
    }
    for (auto task : tasks) {
        xx = x + column_width + 5;
        it.printf(xx, yy, &id(verdana_22), BLACK, TextAlign::LEFT, "%s", task.c_str());
        yy += row_height;
        if (12 <= ++counter) {
            break;
        }
    }
}

void render_page1(esphome::display::Display& it) {
    std::time_t nowt;
    std::tm* now = time_tm(false, &nowt);
    uint16_t now_year = now->tm_year;
    uint8_t now_month = now->tm_mon;
    uint8_t now_mday = now->tm_mday;
    uint8_t now_wday = now->tm_wday;
    uint8_t now_hour = now->tm_hour;
    uint8_t now_min = now->tm_min;

    std::vector<std::time_t> calendar = helper_calendar_range(*now, true);
    std::vector<CalendarEvent> events = extract_json_calendar_events();
    std::vector<Forecast> forecast_hourly = extract_json_forecast(id(sensor_weather_forecast_hourly));
    std::vector<std::string> tasks = extract_json_tasks();

    it.fill(WHITE);

    render_calendar_today(it, 160, 54, now_year, now_month, now_mday, now_wday); //Center aligned
    render_calendar_calendar(it, 300, 20, calendar, events, now_month, now_mday, now_wday);

    render_weather_current(it, 20, 300, 
        id(sensor_weather_now_temperature).state, id(sensor_weather_daily_temperature_low).state, id(sensor_weather_daily_temperature_high).state, 
        id(sensor_weather_now_precipitation).state, id(sensor_weather_now_condition).state, id(sensor_sun_elevation).state
        );
    render_weather_forecast_hourly(it, 300, 324, nowt, forecast_hourly);
    it.print(20, 460, &id(verdana_22), BLACK, TextAlign::TOP_LEFT, capitalize(id(sensor_weather_now_text).state).c_str());
    
    render_tasks(it, 20, 486, nowt, calendar, events, tasks);

    it.filled_rectangle(0, 720, 80, 80, WHITE);
    it.qr_code(10, 730, &id(wifi_qr), BLACK, 2); // ~60px
    //it.filled_rectangle(520, 750, 80, 50, WHITE);
    //it.filled_rectangle(520, 760, 80, 40, WHITE);
    it.printf(590, 790, &id(verdana_22), BLACK, TextAlign::BOTTOM_RIGHT, "%02d:%02d", now_hour, now_min);
    //it.printf(590, 764, &id(verdanab_11), GREY, TextAlign::BOTTOM_RIGHT, "Updated");
}

void render_page2(esphome::display::Display& it) {
    std::time_t nowt;
    std::tm* now = time_tm(false, &nowt);
    uint8_t now_hour = now->tm_hour;
    uint8_t now_min = now->tm_min;

    it.fill(WHITE);

    std::vector<Forecast> forecast_hourly = extract_json_forecast(id(sensor_weather_forecast_hourly));
    std::vector<Forecast> forecast_daily = extract_json_forecast(id(sensor_weather_forecast_daily));

    render_weather_current(it, 20, 20, 
        id(sensor_weather_now_temperature).state, id(sensor_weather_daily_temperature_low).state, id(sensor_weather_daily_temperature_high).state, 
        id(sensor_weather_now_precipitation).state, id(sensor_weather_now_condition).state, id(sensor_sun_elevation).state
        );
    render_weather_forecast_hourly(it, 300, 44, nowt, forecast_hourly);
    it.print(20, 180, &id(verdana_22), BLACK, TextAlign::TOP_LEFT, capitalize(id(sensor_weather_now_text).state).c_str());

    render_weather_forecast_daily(it, 20, 220, nowt, forecast_daily);


    it.print(20, 535, &id(verdanab_22), BLACK, TextAlign::TOP_LEFT, "WIFI");
    it.qr_code(20, 575, &id(wifi_qr), BLACK, 5);
    it.print(20, 730, &id(verdana_22), BLACK, TextAlign::TOP_LEFT, "SSID");
    it.print(20, 755, &id(verdana_22), BLACK, TextAlign::TOP_LEFT, "Pass");
    it.print(85, 730, &id(verdana_22), BLACK, TextAlign::TOP_LEFT, id(wifi_ssid).c_str());
    it.print(85, 755, &id(verdana_22), BLACK, TextAlign::TOP_LEFT, id(wifi_password).c_str());

    it.print(300, 535, &id(verdanab_22), BLACK, TextAlign::TOP_LEFT, "Deathbaron");
    it.qr_code(300, 575, &id(homepage_qr), BLACK, 5);
    it.print(300, 730, &id(verdana_22), BLACK, TextAlign::TOP_LEFT, id(homepage).c_str());

    it.printf(590, 790, &id(verdana_22), BLACK, TextAlign::BOTTOM_RIGHT, "%02d:%02d", now_hour, now_min);
}

void boot() {
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 30000,
        .idle_core_mask = 0,
        .trigger_panic = false
    };
    ESP_LOGD(TAG, "Free RAM heap size (all / psram): %d / %d.", heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    esp_task_wdt_init(&wdt_config);
}

}  // namespace customcode
