#include "equipment.hpp"
#include <algorithm>
#include <string_view>
#include <stdexcept>
#include <utility>
namespace d2x {
std::optional<bool> skillEquipmentReady(const SkillEquipmentRule &skill,
    const std::array<std::optional<SkillHandEquipment>, 2> &hands) {
    if (skill.basicAction != BasicSkillAction::None && skill.basicAction != BasicSkillAction::Attack) return {};
    if (skill.includedHandTypes[0][0].empty() && skill.excludedHandTypes[0][0].empty()) return true;
    const auto isType = [](const SkillHandEquipment &item, std::string_view type) {
        return item.present && std::find(item.types.begin(), item.types.end(), type) != item.types.end();
    };
    const auto matches = [&](size_t hand, size_t group) -> std::optional<bool> {
        if (!hands[hand]) return {};
        const auto &item = *hands[hand];
        const auto &other = hands[1 - hand];
        const auto &included = skill.includedHandTypes[group];
        const auto &excluded = skill.excludedHandTypes[group];
        if (!item.present) {
            if (included[0].empty() && excluded[0].empty()) return true;
            // D2Common permits bare hands for these original ItemTypes only
            // when group B is empty and the other hand is not a weapon.
            const auto &primary = skill.includedHandTypes[0][0];
            if ((primary == "weap" || primary == "mele" || primary == "h2h") &&
                skill.includedHandTypes[1][0].empty())
                return other ? std::optional{!isType(*other, "weap")} : std::nullopt;
            return false;
        }
        for (const auto &type : excluded) {
            if (type.empty()) break;
            if (isType(item, type)) return false;
        }
        if (included[0].empty()) return true;
        bool found = false;
        for (const auto &type : included) {
            if (type.empty()) break;
            if (isType(item, type)) { found = true; break; }
        }
        if (!found) return false;
        if(item.unusable) return skill.basicAction==BasicSkillAction::Attack;
        if (item.ammunitionType.empty() || skill.noAmmo) return true;
        const bool attack=skill.basicAction==BasicSkillAction::Attack;
        if(attack && item.magicArrow==true) return true;
        if (!other) return {};
        const bool unknownWeaponArrow=attack && !item.magicArrow;
        if (!isType(*other, item.ammunitionType)) return unknownWeaponArrow?std::nullopt:std::optional{false};
        if ((attack && other->magicArrow==true) || !other->stackable ||
            (other->quantity && *other->quantity>0)) return true;
        if(unknownWeaponArrow || (attack && !other->magicArrow) || !other->quantity) return {};
        return false;
    };
    const auto rightA = matches(0, 0);
    if (!rightA) return {};
    if (*rightA) return matches(1, 1);
    const auto rightB = matches(0, 1);
    if (!rightB) return {};
    return *rightB ? matches(1, 0) : std::optional{false};
}
std::optional<bool> skillSelectionRestorable(const SkillRestoreRule &rule,const SkillRestoreInput &input) {
    if(!input.exists) return false;
    if(!rule.inGame) return true; // DISABLED precedes the native level test.
    if(input.level<=0) return false;
    if(rule.aura || rule.passive) return true;
    return nativeSkillEquipmentReady(rule,input);
}
std::optional<bool> nativeSkillEquipmentReady(const SkillRestoreRule &rule,const SkillRestoreInput &input) {
    if(input.primaryHand && *input.primaryHand>=2) throw std::invalid_argument("Invalid native primary hand");
    if(rule.scroll) {
        if(!input.quantity) return {};
        if(*input.quantity<=0) return false;
    }
    auto hands=input.hands;
    const auto weapon=[&](size_t hand)->std::optional<bool> {
        if(!hands[hand]) return {};
        const auto &item=*hands[hand];
        return item.present && !item.unusable && std::find(item.types.begin(),item.types.end(),"weap")!=item.types.end();
    };
    const bool special=rule.basicAction==BasicSkillAction::Throw || rule.basicAction==BasicSkillAction::LeftHandThrow ||
        rule.basicAction==BasicSkillAction::LeftHandSwing;
    if(special) {
        std::optional<size_t> equipped;
        for(size_t hand=0;hand<2;++hand) {
            const auto usable=weapon(hand);if(!usable) return {};
            if(*usable) {equipped=hand;break;}
        }
        if(!equipped) return false;
        if(!input.primaryHandKnown && !input.primaryHand) return {};
        size_t hand=*equipped;const bool main=input.primaryHand && hand==*input.primaryHand;
        if(rule.basicAction==BasicSkillAction::Throw) {
            if(!main) {if(!input.dualWield) return false;hand=1-hand;}
            if(!input.throwable[hand]) return false;
        } else {
            if(rule.basicAction==BasicSkillAction::LeftHandSwing && !input.dualWield) return false;
            if(main) hand=1-hand;
            const auto usable=weapon(hand);if(!usable) return {};
            if(!*usable || (rule.basicAction==BasicSkillAction::LeftHandThrow && !input.throwable[hand])) return false;
            // The paired type test omits the registered main weapon.
            if(input.primaryHand) hands[*input.primaryHand]=SkillHandEquipment{};
        }
    }
    auto equipment=static_cast<const SkillEquipmentRule &>(rule);
    if(special) equipment.basicAction=BasicSkillAction::None;
    return skillEquipmentReady(equipment,hands);
}
std::optional<NativeSkillUseState> nativeSkillUseState(const NativeSkillUseChecks &checks) {
    using State=NativeSkillUseState;
    if(!checks.inGame) return State::Disabled;
    if(!checks.level) return {};
    if(*checks.level<=0) return State::NoLevel;
    if(checks.aura) return State::Aura;
    if(checks.passive) return State::Passive;
    for(const auto &[failure,ready]:std::array{
        std::pair{State::NoQuantity,checks.equipmentReady},std::pair{State::NoMana,checks.manaReady},
        std::pair{State::ShapeRestricted,checks.shapeReady},std::pair{State::NoMana,checks.startManaReady},
        std::pair{State::NoQuantity,checks.itemChargesReady},std::pair{State::Delayed,checks.delayReady}}) {
        if(!ready) return {};
        if(!*ready) return failure;
    }
    return State::Ready;
}
bool nativeSkillFallsBackToAttack(NativeSkillUseState state,bool attackNoMana) {
    using State=NativeSkillUseState;
    return attackNoMana && (state==State::NoMana || state==State::NoQuantity || state==State::ShapeRestricted);
}
NativeAttackArrow nativeAttackArrow(int magicArrow,int explosiveArrow) {
    using Kind=NativeAttackArrow::Kind;
    if(magicArrow>0) return {Kind::Magic,magicArrow};
    if(explosiveArrow>0) return {Kind::Explosive,explosiveArrow};
    return {};
}
std::string_view NativeAttackArrow::missileName() const {
    if(kind==Kind::Magic) return "magicarrow";
    if(kind==Kind::Explosive) return "explodingarrow";
    return {};
}
}
