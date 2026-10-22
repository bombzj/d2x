#include "language.hpp"
#include "resources/archive.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <fstream>

namespace d2x {
namespace {
std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

struct LangInfo {
    std::string dir;
    std::string fontDir; // data/local/font/<fontDir>; Western languages share latin.
    uint32_t localeId;
    uint32_t languageCode; // 'enUS' style as DWORD: e,n,U,S -> 0x65 0x6E 0x55 0x53 -> enUS
    uint8_t gameLocale;
};

LangInfo mapInfo(LanguageId id) {
    switch (id) {
        case LanguageId::English: return {"eng", "latin", 1033, 0x656E5553u, 0}; // enUS
        case LanguageId::German:  return {"deu", "latin", 1031, 0x64655553u, 1}; // deDE? original uses 'deDE' in some forms; protocol DWORD varies
        case LanguageId::French:  return {"fra", "latin", 1036, 0x66725553u, 2}; // frFR
        case LanguageId::Spanish: return {"esp", "latin", 1034, 0x65735553u, 3}; // esES
        case LanguageId::Italian: return {"ita", "latin", 1040, 0x69745553u, 4}; // itIT
        case LanguageId::Japanese:return {"jpn", "jpn", 1041, 0x6A705553u, 5}; // jaJP
        case LanguageId::Korean:  return {"kor", "kor", 1042, 0x6B6F5553u, 6}; // koKR
        case LanguageId::Chinese: return {"chi", "chi", 1028, 0x7A685553u, 7}; // zhTW/CHT commonly 'chi' dir name used
        case LanguageId::Polish:  return {"pol", "latin", 1045, 0x706C5553u, 8}; // plPL
        case LanguageId::Russian: return {"rus", "latin", 1049, 0x72755553u, 9}; // ruRU
    }
    return {"eng", "latin", 1033, 0x656E5553u, 0};
}

LanguageId idFromOverride(std::string_view override) {
    if (override.empty()) return LanguageId::English;
    auto s = toLower(std::string(override));
    if (s == "0" || s == "eng" || s == "english" || s == "en" || s == "en-us") return LanguageId::English;
    if (s == "1" || s == "deu" || s == "de" || s == "german" || s == "de-de") return LanguageId::German;
    if (s == "2" || s == "fra" || s == "fr" || s == "french" || s == "fr-fr") return LanguageId::French;
    if (s == "3" || s == "esp" || s == "spa" || s == "es" || s == "spanish" || s == "es-es") return LanguageId::Spanish;
    if (s == "4" || s == "ita" || s == "it" || s == "italian" || s == "it-it") return LanguageId::Italian;
    if (s == "5" || s == "jpn" || s == "ja" || s == "japanese") return LanguageId::Japanese;
    if (s == "6" || s == "kor" || s == "ko" || s == "korean") return LanguageId::Korean;
    if (s == "7" || s == "chi" || s == "ch" || s == "cht" || s == "chinese" || s == "zh-tw") return LanguageId::Chinese;
    if (s == "8" || s == "pol" || s == "pl" || s == "polish") return LanguageId::Polish;
    if (s == "9" || s == "rus" || s == "ru" || s == "russian") return LanguageId::Russian;
    return LanguageId::English;
}

LanguageId readUse(Archives &archives) {
    // Try to read data/local/use from archives
    try {
        const auto p1 = "data/local/use";
        if (archives.contains(p1)) {
            auto b = archives.read(p1);
            if (!b.empty()) return static_cast<LanguageId>(b[0]);
        }
        const auto p2 = "data/local/USE";
        if (archives.contains(p2)) {
            auto b = archives.read(p2);
            if (!b.empty()) return static_cast<LanguageId>(b[0]);
        }
    } catch (...) {}
    return LanguageId::English;
}

LanguageId readUseFile(const std::string &pathHint) {
    try {
        // try common locations
        std::vector<std::string> candidates = {
            pathHint + "/data/local/use",
            pathHint + "/data/local/USE",
            pathHint + "/Use",
            pathHint + "/USE",
            "assets/mpq/data/local/use",
            "assets/mpq/data/local/USE",
            "assets/mpq2/data/local/use",
            "assets/mpq2/data/local/USE",
            "data/local/use",
            "data/local/USE",
            "/data/diablo/data/local/Use",
            "/data/diablo/data/local/use",
            "/data/Diablo/data/local/Use",
            "/data/Diablo/data/local/use",
        };
        for (const auto &c : candidates) {
            std::ifstream f(c, std::ios::binary);
            if (f) {
                char b;
                if (f.read(&b, 1)) return static_cast<LanguageId>(static_cast<uint8_t>(b));
            }
        }
    } catch (...) {}
    return LanguageId::English;
}
} // namespace

Language detectLanguage(std::string_view override, Archives &archives) {
    LanguageId id = LanguageId::English;
    if (!override.empty()) id = idFromOverride(override);
    else {
        id = readUse(archives);
        if (id == LanguageId::English) { // try filesystem too
            id = readUseFile("");
        }
        if (id > LanguageId::Russian) id = LanguageId::English; // clamp
    }
    auto info = mapInfo(id);
    Language lang;
    lang.id = id;
    lang.directory = info.dir;
    lang.fontDirectory = info.fontDir;
    lang.localeId = info.localeId;
    lang.languageCode = info.languageCode;
    lang.gameLocale = info.gameLocale;
    return lang;
}

void applyLanguage(Archives &archives, const Language &language) {
    archives.languageDirectory = language.directory;
    archives.languageFontDirectory = language.fontDirectory;
    // Non-Latin languages need their own DC6 font. When the mounted set ships
    // only the latin fonts, fall back to latin artwork instead of failing every
    // font load. String tables keep the selected language.
    const auto hasFont = [&](const std::string &dir) {
        return archives.contains("data/local/font/" + dir + "/font16.dc6");
    };
    if (!hasFont(archives.languageFontDirectory) && hasFont("latin"))
        archives.languageFontDirectory = "latin";
    archives.languageLocaleId = language.localeId;
    archives.languageCode = language.languageCode;
    archives.languageGameLocale = language.gameLocale;
}

Language languageOf(const Archives &archives) {
    Language lang;
    lang.directory = archives.languageDirectory;
    lang.fontDirectory = archives.languageFontDirectory;
    lang.localeId = archives.languageLocaleId;
    lang.languageCode = archives.languageCode;
    lang.gameLocale = archives.languageGameLocale;
    for (uint8_t i = 0; i <= uint8_t(LanguageId::Russian); ++i)
        if (mapInfo(LanguageId(i)).dir == lang.directory) { lang.id = LanguageId(i); break; }
    return lang;
}

std::string localUiArtPath(const Archives &archives, std::string_view name) {
    const auto directory = languageOf(archives).directory;
    if (directory != "eng") {
        std::string translated = "data/local/ui/" + directory + "/" + std::string(name) + ".dc6";
        if (archives.contains(translated)) return translated;
    }
    return "data/local/ui/eng/" + std::string(name) + ".dc6";
}

Language detectLanguage(std::string_view override, const std::string &archivesPathHint) {
    LanguageId id = LanguageId::English;
    if (!override.empty()) id = idFromOverride(override);
    else {
        id = readUseFile(archivesPathHint);
        if (id > LanguageId::Russian) id = LanguageId::English;
    }
    auto info = mapInfo(id);
    Language lang;
    lang.id = id;
    lang.directory = info.dir;
    lang.fontDirectory = info.fontDir;
    lang.localeId = info.localeId;
    lang.languageCode = info.languageCode;
    lang.gameLocale = info.gameLocale;
    return lang;
}
} // namespace d2x
