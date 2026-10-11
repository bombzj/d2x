#include "skill_description.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <utility>

namespace d2x {
namespace {
// Independent interpretation of the original 1.13c D2Client RVA 316C0
// dispatch (1..75). The table chooses the program; there are no skill identities
// in this file. Formatting helper evidence is recorded in DESCRIPTIONS.md.
DescriptionNumber scaled(DescriptionNumber value,int multiplier,int divisor=1) {
    if (!value || !divisor) return {};
    const int64_t result=int64_t(*value)*multiplier/divisor;
    if (result<INT32_MIN || result>INT32_MAX) return {};
    return int32_t(result);
}
std::string integer(DescriptionNumber value,bool sign=false) {
    return value?(sign && *value>=0?"+":"")+std::to_string(*value):"?";
}
std::string decimal(DescriptionNumber value,int divisor,int precision=10,bool sign=false,bool fractionRequired=false) {
    if (!value || divisor<=0 || precision<=0) return "?";
    const int64_t whole=*value/divisor, fraction=std::abs(int64_t(*value)%divisor)*precision/divisor;
    std::string result=(sign && *value>=0?"+":"")+std::to_string(whole);
    if (*value<0 && !whole) result="-0";
    // Native helpers format the truncated fractional integer using %d:
    // neither zero padding nor trimming is part of that program.
    if (fraction || fractionRequired) result+="."+std::to_string(fraction);
    return result;
}
std::string range(const DescriptionRange &value,bool plus=false) {
    if (value.minimum && value.maximum && *value.minimum==*value.maximum) return integer(value.minimum,plus);
    return integer(value.minimum,plus)+"-"+integer(value.maximum);
}
std::string format(std::string_view pattern,const std::vector<std::string> &arguments) {
    // TBL supplies formats; never pass untrusted resource text to printf.
    std::string result; size_t index=0;
    for (size_t position=0;position<pattern.size();++position) {
        if (pattern[position]=='%' && position+1<pattern.size()) {
            if (pattern[position+1]=='%') { result+='%'; ++position; continue; }
            if (pattern[position+1]=='s' || pattern[position+1]=='d' || pattern[position+1]=='i' || pattern[position+1]=='u') {
                result+=index<arguments.size()?arguments[index++]:"?"; ++position; continue;
            }
        }
        result+=pattern[position];
    }
    return result;
}
class Programs {
    const SkillDescriptionCatalog &catalog_;
    SkillDescriptionValues values_;
    SkillDescriptionContext context_;
    SkillDescriptionResult &result_;
    std::string text(std::string_view key) const { return std::string(catalog_.label(key)); }
    std::string seconds(DescriptionNumber frames,bool sign=false) const {
        return decimal(frames,25,10,sign)+text(frames==25?"StrSkill15":"StrSkill16");
    }
    std::string elementLabel(std::string_view element) const {
        const auto key=element=="fire"?"StrSkill5":element=="ltng"?"StrSkill7":element=="cold"?"StrSkill6":
            element=="pois"?"StrSkill8":element=="mag"?"StrSkill39":element=="life"?"ModStr2z":"";
        return text(key);
    }
    std::string radius(DescriptionNumber cells,int multiplier) const {
        if (!cells) return "?"+text("StrSkill26");
        const int64_t tenths=int64_t(*cells)*multiplier*10/3;
        const int64_t whole=std::max<int64_t>(1,tenths/10),fraction=std::abs(tenths%10);
        return std::to_string(whole)+(fraction?"."+std::to_string(fraction):"")+text(whole==1?"StrSkill36":"StrSkill26");
    }
    void unknown(int program) {
        if (std::find(result_.unknownPrograms.begin(),result_.unknownPrograms.end(),program)==result_.unknownPrograms.end())
            result_.unknownPrograms.push_back(program);
    }
  public:
    Programs(const SkillDescriptionCatalog &catalog,const SkillDescriptionInput &input,const SkillDescriptionSource &source,
             SkillDescriptionContext context,SkillDescriptionResult &result):catalog_(catalog),values_(catalog,input,source),context_(context),result_(result) {}
    std::vector<std::string> operator()(const SkillDescriptionLine &line) {
        if (!line.program) return {};
        const auto first=values_.formula(line.calculationA),second=values_.formula(line.calculationB);
        const auto &prefix=line.textA,&suffix=line.textB;
        auto single=[&](std::string value,bool known=true) {
            if (!known) unknown(line.program);
            return std::vector<std::string>{std::move(value)};
        };
        auto basic=[&](DescriptionNumber value,bool sign=false,bool numberFirst=false) {
            if (value==0) return std::vector<std::string>{};
            return single(numberFirst?integer(value,sign)+prefix+suffix:prefix+integer(value,sign)+suffix,value.has_value());
        };
        auto damage=[&](DescriptionRange value,std::string label,std::string tail={}) {
            if (value.minimum==0 && value.maximum==0) return std::vector<std::string>{};
            return single(std::move(label)+range(value)+tail,value.minimum && value.maximum);
        };
        auto duration=[&](DescriptionNumber frames,std::string label,bool sign=false) {
            if (scaled(frames,10,25)==0) return std::vector<std::string>{};
            return single(std::move(label)+seconds(frames,sign),frames.has_value());
        };
        auto colon=[&] { return prefix+text("colon")+text("space"); };
        switch (line.program) {
        case 1: {
            if (values_.source().number("mana")==0 && values_.source().number("lvlmana")==0) return {};
            const auto cost=values_.mana();
            const auto shift=values_.source().number("manashift"),increment=values_.source().number("lvlmana");
            const bool fractional=shift && *shift>=0 && *shift<=15 && increment && *shift!=8 &&
                int64_t(*increment)*(int64_t(1)<<*shift)<=256 && cost && *cost<=25*256;
            const int precision=first && *first>10?*first:10;
            return single(values_.source().description.manaLabel+(fractional?(first?decimal(cost,256,precision):"?"):integer(scaled(cost,1,256))),
                          cost.has_value() && (!fractional || first.has_value()));
        }
        case 2: return basic(first,true);
        case 3: return basic(first);
        case 4: case 5:
            return first==0?std::vector<std::string>{}:single(prefix+integer(first,line.program==4),first.has_value());
        case 6: case 7:
            return first==0?std::vector<std::string>{}:single(integer(first,line.program==6)+prefix,first.has_value());
        case 8: {
            const auto value=values_.parameter("toht");
            return value==0?std::vector<std::string>{}:single(prefix+suffix+text("StrSkill10")+integer(value,true)+text("StrSkill23"),value.has_value());
        }
        case 9: {
            auto value=values_.physicalDamage();
            auto adjust=[&](DescriptionNumber amount) -> DescriptionNumber {
                if (!amount || !first || !second) return {};
                const int64_t adjusted=int64_t(*amount)+int64_t(*amount)*(*first)/100+*second;
                return adjusted>=INT32_MIN && adjusted<=INT32_MAX?DescriptionNumber{int32_t(adjusted)}:DescriptionNumber{};
            };
            value={adjust(value.minimum),adjust(value.maximum)};
            return damage(value,prefix+suffix+text("StrSkill4"));
        }
        case 10: return damage(values_.elementalDamage(),prefix+suffix+elementLabel(values_.element()));
        case 11:
            if (values_.element()!="cold" && values_.element()!="pois") return {};
            return duration(values_.parameter("edln"),prefix+suffix+text(values_.element()=="cold"?"StrSkill13":"StrSkill14"));
        case 12: return duration(first,prefix);
        case 13: {
            const auto life=values_.petLife(first,second);
            return life==0?std::vector<std::string>{}:single(text("StrSkill42")+integer(life),life.has_value());
        }
        case 14: {
            const auto frames=values_.parameter("edln");
            DescriptionRange total;
            // Multiply fixed-point rates by the duration before the final shift.
            if (frames) total=values_.elementalDamage(*frames);
            auto lines=damage(total,prefix+suffix+elementLabel(values_.element()));
            auto length=duration(frames,text("StrSkill63Patch"));
            lines.insert(lines.end(),length.begin(),length.end()); return lines;
        }
        case 15: return single(colon()+suffix);
        case 16:
            if (first==0 && second==0) return {};
            return single(text("StrSkill20")+(first && second && first==second?decimal(first,25):decimal(first,25)+"-"+decimal(second,25))+text("StrSkill16"),first && second);
        // Programs 17/19/24 prepend TextB before the TextA value line.
        // Native RVA 31B1B / 31BE8 / 31CE0; charge labels are not extra lines.
        case 17: return damage({first,second},suffix+prefix,text("StrSkill34"));
        case 35: return single(colon()+integer(first)+"-"+integer(second),first && second);
        case 18: case 25: case 40: case 46: {
            if (line.program==46 && second==0) return {};
            const auto rendered=line.program==40 || line.program==46?format(prefix,{suffix}):line.program==18?prefix:prefix+suffix;
            if (context_==SkillDescriptionContext::TreeBonus && first==2) result_.bonusHeading=rendered;
            else if (!rendered.empty()) return single(rendered);
            return {};
        }
        case 19: return single(suffix+prefix+radius(first,2),first.has_value());
        case 20: case 21:
            return first==0?std::vector<std::string>{}:single(prefix+integer(first,line.program==20)+text("StrSkill23")+suffix,first.has_value());
        case 22: return damage(values_.missileElementalDamage(0,75,DescriptionMastery::Include),text("StrSkill68")+elementLabel(values_.missileElement()),text("StrSkill34"));
        case 23: return duration(values_.missileRange(0),prefix);
        case 24: return damage(values_.elementalDamage(),suffix+(prefix.empty()?elementLabel(values_.element()):prefix));
        case 26: return damage(values_.elementalDamage(25),text("StrSkill68")+elementLabel(values_.element()),text("StrSkill34"));
        case 27: return damage(values_.elementalDamage(75),text("StrSkill68")+text("StrSkill5"),text("StrSkill34"));
        case 28: return single(text("StrSkill18")+radius(values_.missileParameter(0,1),2),values_.missileParameter(0,1).has_value());
        case 29: return single(integer(scaled(values_.missileRange(0,false,true),2,3))+text("StrSkill26"),values_.missileRange(0,false,true).has_value());
        case 30: return duration(values_.missileRange(0,true),prefix);
        case 31: return duration(values_.curseDuration(first),prefix);
        case 32: return single(prefix+suffix+integer(first,true)+text("StrSkill23"),first.has_value());
        case 33: return values_.rank() && *values_.rank()<=1?std::vector<std::string>{}:single(prefix+suffix);
        case 34: return damage(values_.petDamage(),text("StrSkill4"));
        case 36: return first==0?std::vector<std::string>{}:single(integer(first)+(first==1?prefix:suffix),first.has_value());
        case 37: return single(prefix+radius(first,1),first.has_value());
        case 38: return damage({first,second},prefix,suffix);
        case 39: return damage(values_.petDamage(true),text("StrSkill4"));
        case 41: return damage(values_.petElementDamage(first,second),elementLabel(values_.element()));
        case 42:
            if (first==0 && second==0) return {};
            return single(colon()+integer(first,true)+"."+integer(second)+text("space")+suffix,first && second);
        case 43: case 44: {
            const int precision=line.program==43?10:100;
            if (first==0 && second==0) return {};
            const bool fractional=(first && int64_t(*first%256)*precision/256) ||
                (second && int64_t(*second%256)*precision/256);
            const auto low=decimal(first,256,precision,false,fractional),high=decimal(second,256,precision,false,fractional);
            return single(prefix+low+"-"+high+suffix,first && second);
        }
        case 45: {
            if (values_.rank() && *values_.rank()<=1) return {};
            const auto value=values_.previousParameter("ln12");
            if (!value) unknown(line.program);
            return {text("StrSkill112"),integer(value)+text("StrSkill55")};
        }
        case 47: return damage({first,second},prefix);
        case 48: return damage(values_.elementalDamage(),text("StrSkill4"));
        case 49: {
            const auto value=values_.physicalDamage(true);
            const bool equal=value.minimum && value.maximum && value.minimum==value.maximum;
            return single(prefix+(equal?"":text("space"))+range(value,true),value.minimum && value.maximum);
        }
        case 50: return damage(values_.missileElementalDamage(),prefix.empty()?elementLabel(values_.missileElement()):prefix);
        case 51: case 66: return single(format(prefix,{integer(first)}),first.has_value());
        case 52: case 58: case 62: {
            if (first==0 && second==0) return {};
            const bool equal=first && second && first==second;
            const auto value=range({first,second},!equal && line.program!=62);
            return single(prefix+(line.program==52 || equal?value+suffix:suffix+value),first && second);
        }
        case 53: case 54: case 55: case 56: return single(format(text("scrollbooktext"),{integer(values_.quantity())}),values_.quantity().has_value());
        case 57: return duration(first,prefix,true);
        case 59: return damage({first,second},suffix+(prefix.empty()?elementLabel(values_.element()):prefix));
        case 60: case 61: {
            const int precision=second && *second!=0?*second:10;
            if (first==0) return {};
            return single(prefix+(second?decimal(first,256,precision,line.program==60):"?")+suffix,
                          first && second && precision>0);
        }
        case 63: case 67:
            if (first==0) return {};
            return single(colon()+integer(first,true)+(line.program==63?text("percent"):"")+text("space")+suffix,first.has_value());
        case 64:
            if (first==0 || (second && *second<=0)) return {};
            return single(colon()+integer(first,true)+"/"+integer(second)+text("space")+suffix,first && second);
        case 65: return single(colon()+suffix);
        case 68: return basic(first,false,true);
        case 69: return single(suffix+text("colon")+text("space")+integer(first),first.has_value());
        case 70: return single(prefix+suffix+integer(first,true),first.has_value());
        case 71: return single(colon()+format(suffix,{integer(first)}),first.has_value());
        case 72: case 73:
            if (first==0 || (second && *second<=0)) return {};
            return single(integer(first,line.program==72)+"/"+integer(second)+text("space")+prefix,first && second);
        case 74: return single(format(prefix,{integer(first)}),first.has_value());
        case 75: return single(format(prefix,{integer(first),integer(second)}),first && second);
        default: unknown(line.program); return {prefix+"?"+suffix};
        }
    }
};
}
SkillDescriptionResult describeSkill(const SkillDescriptionCatalog &catalog,const SkillDescriptionInput &input,
                                     SkillDescriptionContext context) {
    SkillDescriptionResult result;
    const auto found=catalog.skills.find(input.skill);
    if (found==catalog.skills.end() || !found->second.hasDescription) return result;
    const auto &description=found->second.description;
    Programs program(catalog,input,found->second,context,result);
    auto append=[&](const auto &lines,bool reverse) {
        auto add=[&](const auto &line) { auto rendered=program(line); result.lines.insert(result.lines.end(),rendered.begin(),rendered.end()); };
        if (reverse) { for (auto entry=lines.rbegin();entry!=lines.rend();++entry) add(*entry); }
        else { for (const auto &line:lines) add(line); }
    };
    switch (context) {
    case SkillDescriptionContext::Picker: append(description.general,true); append(description.levels,true); break;
    case SkillDescriptionContext::TreeLevel: append(description.levels,true); break;
    case SkillDescriptionContext::TreeGeneral: append(description.general,true); break;
    case SkillDescriptionContext::TreeBonus: append(description.bonuses,false); break;
    }
    return result;
}
std::string describeSkillCharges(const SkillDescriptionCatalog &catalog,int current,int maximum) {
    return format(catalog.label("ModStre10d"),{integer(current),integer(maximum)});
}
} // namespace d2x
