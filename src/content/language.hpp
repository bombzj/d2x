#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace d2x {
class Archives;

enum class LanguageId : uint8_t {
    English = 0,
    German = 1,
    French = 2,
    Spanish = 3,
    Italian = 4,
    Japanese = 5,
    Korean = 6,
    Chinese = 7,
    Polish = 8,
    Russian = 9
};

struct Language {
    LanguageId id{LanguageId::English};
    std::string directory{"eng"};
    std::string fontDirectory{"latin"};
    uint32_t localeId{1033};
    uint32_t languageCode{0x656E5553u}; // 'enUS'
    uint8_t gameLocale{0};
};

Language detectLanguage(std::string_view override, Archives &archives);
Language detectLanguage(std::string_view override, const std::string &archivesPathHint = {});
// Store/read the language selection on the mounted resource set so every
// ClassicStrings and protocol consumer shares one choice.
void applyLanguage(Archives &archives, const Language &language);
Language languageOf(const Archives &archives);
// data/local/ui/<language>/<name>.dc6 when the selected language ships that
// image, otherwise the original English one.
std::string localUiArtPath(const Archives &archives, std::string_view name);
} // namespace d2x
