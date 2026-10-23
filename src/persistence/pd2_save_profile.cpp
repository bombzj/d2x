#include "pd2_save_profile.hpp"
#include "content/classic_data.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <stdexcept>
namespace d2x {
namespace {
constexpr size_t profileOffset = 0xE0;
constexpr std::array<uint8_t, 8> profile{'D','2','X','P','D','2',1,0};
}
void validatePd2SaveProfile(std::span<const uint8_t> bytes, const ClassicData &data) {
    if(bytes.size()<profileOffset+profile.size()) throw std::runtime_error("Truncated character profile");
    const bool marked = std::equal(profile.begin(),profile.end(),bytes.begin()+profileOffset);
    const bool family = std::equal(profile.begin(),profile.begin()+6,bytes.begin()+profileOffset);
    if(data.pd2Host ? !marked : family)
        throw std::runtime_error("Character ruleset mismatch: D2X PD2 town saves, LoD and native PD2 saves are separate formats");
}
void writePd2SaveProfile(Bytes &bytes, const ClassicData &data, uint8_t &skillCount) {
    if(!data.pd2Host) return;
    std::copy(profile.begin(),profile.end(),bytes.begin()+profileOffset);
    std::map<std::string,size_t,std::less<>> counts;
    const auto &skills=data.tables.at("skills");
    for(size_t row=0;row<skills.rows().size();++row)
        if(const auto code=skills.value(row,"charclass"); !code.empty()) ++counts[std::string(code)];
    size_t maximum=0;
    for(const auto &character:data.characters) maximum=std::max(maximum,counts[character.code]);
    if(!maximum || maximum>UINT8_MAX) throw std::runtime_error("PD2 skill count exceeds the saved header");
    skillCount=uint8_t(maximum);
}
}
