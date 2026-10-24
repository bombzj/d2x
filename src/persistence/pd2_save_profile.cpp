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
void rejectPrivatePd2Save(std::span<const uint8_t> bytes) {
    if(bytes.size()<profileOffset+profile.size()) throw std::runtime_error("Truncated character profile");
    const bool family = std::equal(profile.begin(),profile.begin()+6,bytes.begin()+profileOffset);
    if(family)
        throw std::runtime_error("Legacy private D2X PD2 save is unsupported; load a native PD2 save or create a new character");
}
void preparePd2SaveHeader(const ClassicData &data, uint8_t &skillCount) {
    if(!data.pd2Host) return;
    std::map<std::string,size_t,std::less<>> counts;
    const auto &skills=data.tables.at("skills");
    for(size_t row=0;row<skills.rows().size();++row)
        if(const auto code=skills.value(row,"charclass"); !code.empty()) ++counts[std::string(code)];
    size_t maximum=0;
    for(const auto &character:data.characters) maximum=std::max(maximum,counts[character.code]);
    if(!maximum || maximum>UINT8_MAX) throw std::runtime_error("PD2 skill count exceeds the saved header");
    skillCount=uint8_t(maximum);
}
void validatePd2SaveExtension(std::span<const uint8_t> bytes) {
    // Current core 17d5fb1b, writer RVA 0x39A510 and reader 0x39AD40.
    // The native reader also accepts a legacy body without the optional tail.
    if(bytes.empty()) return;
    constexpr std::array<uint8_t,4> prefix{'p','d',0x82,0x0A};
    if(bytes.size()<prefix.size() || !std::equal(prefix.begin(),prefix.end(),bytes.begin()))
        throw std::runtime_error("Unsupported PD2 save extension version");
    size_t cursor=prefix.size();
    const auto section=[&](char first,char second,size_t length) {
        if(cursor+length>bytes.size() || bytes[cursor]!=uint8_t(first) || bytes[cursor+1]!=uint8_t(second))
            throw std::runtime_error("Truncated or invalid PD2 save extension section");
        cursor+=length;
    };
    if(cursor+3>bytes.size() || bytes[cursor+2]>128)
        throw std::runtime_error("Invalid PD2 saved skill count");
    section('s','u',3+size_t(bytes[cursor+2])*2);
    section('b','k',6); section('s','t',64); section('h','t',3); section('x','p',3); section('h','c',130);
    if(cursor!=bytes.size()) throw std::runtime_error("Unexpected data after PD2 save extension");
}
}
