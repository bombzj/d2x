#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace d2x {
// Strict transport/input decoding. Resource strings retain their separate
// legacy-byte fallback in text.hpp.
inline std::optional<std::vector<uint32_t>> strictUtf8(std::string_view text) {
    std::vector<uint32_t> result;
    for(size_t i=0;i<text.size();) {
        const auto lead=uint8_t(text[i]);uint32_t code=lead;size_t length=1;
        if(lead>=0xc2 && lead<=0xdf) {length=2;code=lead&31;}
        else if(lead>=0xe0 && lead<=0xef) {length=3;code=lead&15;}
        else if(lead>=0xf0 && lead<=0xf4) {length=4;code=lead&7;}
        else if(lead>=0x80) return {};
        if(i+length>text.size()) return {};
        for(size_t j=1;j<length;++j) {
            const auto byte=uint8_t(text[i+j]);if((byte&0xc0)!=0x80) return {};
            code=(code<<6)|(byte&63);
        }
        if((length==2 && code<0x80) || (length==3 && code<0x800) ||
           (length==4 && code<0x10000) || code>0x10ffff || (code>=0xd800 && code<=0xdfff)) return {};
        result.push_back(code);i+=length;
    }
    return result;
}
inline void appendUtf8(std::string &text,uint32_t code) {
    if(code<0x80) text+=char(code);
    else if(code<0x800) {text+=char(0xc0|(code>>6));text+=char(0x80|(code&63));}
    else if(code<=0xffff) {text+=char(0xe0|(code>>12));text+=char(0x80|((code>>6)&63));text+=char(0x80|(code&63));}
    else if(code<=0x10ffff) {
        text+=char(0xf0|(code>>18));text+=char(0x80|((code>>12)&63));text+=char(0x80|((code>>6)&63));text+=char(0x80|(code&63));
    }
}
inline size_t previousUtf8(std::string_view text,size_t at) {
    if(!at) return 0;
    --at;while(at && (uint8_t(text[at])&0xc0)==0x80) --at;
    return at;
}
inline size_t nextUtf8(std::string_view text,size_t at) {
    if(at>=text.size()) return text.size();
    ++at;while(at<text.size() && (uint8_t(text[at])&0xc0)==0x80) ++at;
    return at;
}
}
