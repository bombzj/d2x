#include "map_description.hpp"
#include "content/classic_data.hpp"
#include "core/text.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/stats/stat_list.hpp"
#include <algorithm>
#include <cstdint>

namespace d2x::pd2 {
namespace {
std::u16string wide(std::string_view text) {
    std::u16string result;
    for(auto code:utf8Decode(text)) {
        if(!code) break;
        if(code>0x10ffff || (code>=0xd800 && code<=0xdfff)) code=0xfffd;
        if(code<0x10000) result+=char16_t(code);
        else {code-=0x10000;result+=char16_t(0xd800+(code>>10));result+=char16_t(0xdc00+(code&1023));}
    }
    return result;
}
std::string narrow(std::u16string_view text) {
    std::string result;
    for(size_t index=0;index<text.size();++index) {
        uint32_t code=text[index];
        if(code>=0xd800 && code<=0xdbff && index+1<text.size() && text[index+1]>=0xdc00 && text[index+1]<=0xdfff) {
            code=0x10000+((code-0xd800)<<10)+(text[index+1]-0xdc00);++index;
        } else if(code>=0xd800 && code<=0xdfff) code=0xfffd;
        if(code<0x80) result+=char(code);
        else if(code<0x800) {result+=char(0xc0|(code>>6));result+=char(0x80|(code&63));}
        else if(code<0x10000) {result+=char(0xe0|(code>>12));result+=char(0x80|((code>>6)&63));result+=char(0x80|(code&63));}
        else {result+=char(0xf0|(code>>18));result+=char(0x80|((code>>12)&63));result+=char(0x80|((code>>6)&63));result+=char(0x80|(code&63));}
    }
    return result;
}
std::string formatNumber(std::string_view pattern,int32_t value) {
    std::string result;
    bool used=false;
    for(size_t index=0;index<pattern.size();++index) {
        if(pattern[index]=='%' && index+1<pattern.size()) {
            if(pattern[index+1]=='%') {result+='%';++index;continue;}
            if(!used && pattern[index+1]=='d') {result+=std::to_string(value);++index;used=true;continue;}
        }
        result+=pattern[index];
    }
    return result;
}
struct Description {
    const ItemStatDefinition *definition;
    int32_t value;
    unsigned group;
    bool grouped{};
};
std::string describe(const ClassicData &data,const Description &entry) {
    const auto &stat=*entry.definition;
    const auto &costs=data.tables.at("itemstatcost");
    const auto text=[&](std::string_view key) {
        const auto found=data.itemStrings.find(key);
        return found==data.itemStrings.end()?std::string{}:found->second;
    };
    // Current 37AF70: single stats always use +38 (positive). Grouped
    // negatives use +42 (positive), nonnegatives +44 (negative), as encoded.
    const auto label=text(entry.grouped?costs.value(stat.row,entry.value<0?"dgrpstrpos":"dgrpstrneg"):stat.positive);
    const auto suffix=text(entry.grouped?costs.value(stat.row,"dgrpstr2"):stat.suffix);
    const int function=entry.grouped?costs.number(stat.row,"dgrpfunc").value_or(0):stat.descriptionFunction;
    const int placement=entry.grouped?costs.number(stat.row,"dgrpval").value_or(0):stat.descriptionValue;
    if(!function || placement<0 || placement>2) return {};
    if(function==19 && !placement) {
        const auto pattern=wide(label+"\n");
        if(pattern.size()>255) return {}; // Native secure format into WCHAR[256].
        return formatNumber(narrow(pattern),entry.value);
    }
    const bool withSuffix=function>=6 && function<=10;
    if(!placement) return label+(withSuffix?" "+suffix:"")+"\n";
    int32_t number=entry.value;
    if(function==5 || function==10) number=nativeMultiply(number,100)/128;
    if(function>=19 && function<=21) number=nativeSubtract(0,number);
    // Unknown nonzero functions use the native default +%d branch. In
    // particular function 5 has no percent sign, unlike ordinary items.
    const bool plus=function!=2 && function!=3 && function!=5 && function!=7 && function!=9 && function!=10 &&
        function!=19 && function!=20 && function!=21;
    const bool percent=function==2 || function==4 || function==7 || function==8 || function==10 || function==19 || function==20;
    const auto amount=std::string(plus?"+":"")+std::to_string(number)+(percent?"%":"");
    return (placement==1?amount+" "+label:label+" "+amount)+(withSuffix?" "+suffix:"")+"\n";
}
}
std::vector<std::string> describeMap(const ClassicData &data,std::span<const StatValue> attributes,
    std::string_view areaName,unsigned quality) {
    const auto &costs=data.tables.at("itemstatcost");
    std::vector<Description> descriptions;
    // Current 36C9B0 merges any equal-valued members of a nonzero group,
    // regardless of layer; it neither sums stats nor requires the whole group.
    for(const auto &attribute:attributes) {
        if(attribute.key.id==360 || attribute.key.id==361 || attribute.key.id==458) continue;
        const auto definition=std::find_if(data.itemStats.begin(),data.itemStats.end(),
            [&](const auto &stat){return stat.id==attribute.key.id;});
        if(definition==data.itemStats.end()) continue;
        const auto group=uint16_t(costs.number(definition->row,"dgrp").value_or(0));
        const auto previous=std::find_if(descriptions.begin(),descriptions.end(),
            [&](const auto &entry){return group && entry.group==group && entry.value==attribute.value;});
        if(previous!=descriptions.end()) {previous->grouped=true;continue;}
        if(descriptions.size()==15) break;
        descriptions.push_back({&*definition,attribute.value,group});
    }
    std::stable_sort(descriptions.begin(),descriptions.end(),[](const auto &left,const auto &right) {
        // 373D80 is stable insertion sort at this <=15 size; 36A210 uses <.
        return uint16_t(left.definition->descriptionPriority)<uint16_t(right.definition->descriptionPriority);
    });
    std::u16string buffer=u"\u00ffc3";
    const auto append=[&](std::string_view text) {
        const auto value=wide(text);
        // 29DF80 -> __stdio_common_vswprintf_s: overflow clears the append
        // destination, preserving the prefix, rather than emitting a partial line.
        if(value.size()<=999-buffer.size()) buffer+=value;
    };
    for(const auto &entry:descriptions) append(describe(data,entry));
    const char color=quality==4?'3':quality==6?'9':quality==7?'4':'0';
    append(std::string("\xff" "c")+color+std::string(areaName));
    if(buffer.size()>507) {
        buffer.erase(0,buffer.size()-507);
        buffer.replace(0,8,u"\u00ffc3[...]");
    }
    // Count/truncate in UTF-16 as the DLL does, then carry colour across the
    // renderer's separate lines. A cut surrogate becomes U+FFFD, never bad UTF-8.
    const auto text=narrow(buffer);
    std::vector<std::string> lines;
    std::string currentColor="\xff" "c3",line;
    for(size_t index=0;index<text.size();) {
        int colorCode=-1;
        if(const auto length=d2ColorEscape(text,index,colorCode)) {
            if(colorCode>=0) currentColor=text.substr(index,length);
            line.append(text,index,length);index+=length;
        } else if(text[index++]=='\n') {lines.push_back(std::move(line));line=currentColor;}
        else line+=text[index-1];
    }
    if(!line.empty()) lines.push_back(std::move(line));
    return lines;
}
}
