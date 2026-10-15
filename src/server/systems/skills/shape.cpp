#include "shape.hpp"
#include "server/player_store.hpp"
#include "gameplay/actions/shape_timing.hpp"
#include "gameplay/actions/timing.hpp"
#include <algorithm>
#include <array>
namespace d2x::server::skills {
const ShapeProfile *activeShape(const SkillRules &rules,const std::set<int> &states) {
    for(const auto &[state,profile]:rules.shapes) if(states.contains(state)) return &profile;
    return nullptr;
}
bool shapeAllowsSkill(const SkillRules &rules,const std::set<int> &states,int skill) {
    const auto found=rules.shapeRestrictions.find(skill);if(found==rules.shapeRestrictions.end()) return false;
    const auto &restriction=found->second;const auto *shape=activeShape(rules,states);
    if(restriction.mode==2) return std::any_of(restriction.states.begin(),restriction.states.end(),[&](int state){return state>=0 && states.contains(state);});
    return restriction.mode==1 || !shape || !shape->restrictSkills;
}
std::optional<WeaponAttackTiming> shapeActionTiming(const PlayerState &player,const ShapeProfile &shape,std::string_view playerMode,bool skillUsesAttackRate) {
    constexpr std::array<std::string_view,20> playerModes{"dt","nu","wl","rn","gh","tn","tw","a1","a2","bl","sc","th","kk","s1","s2","s3","s4","dd","sq","kb"};
    const auto index=std::find(playerModes.begin(),playerModes.end(),playerMode);if(index==playerModes.end()) return {};
    const auto mode=shapeMonsterMode(uint8_t(index-playerModes.begin()),shape.enabledModes);
    const auto animation=shape.animations.find(mode);if(animation==shape.animations.end()) return {};
    const auto &clock=animation->second;const auto &attributes=player.totals.character.combat;
    int speed=clock.speed;
    if(mode==4 || mode==5 || ((mode==8 || mode==9 || mode==10 || mode==11) && skillUsesAttackRate)) {
        const auto &weapon=player.totals.equipment.weapons[0];
        const auto human=player.rules.skills->weaponAnimations.find("a1"+player.totals.equipment.animationClass);
        if(weapon.item && human==player.rules.skills->weaponAnimations.end()) return {};
        speed=shapeAttackSpeed(clock.frames,clock.speed,human==player.rules.skills->weaponAnimations.end()?0:human->second.frames,
            human==player.rules.skills->weaponAnimations.end()?0:human->second.speed,weapon.fasterAttack,weapon.baseSpeed,
            attributes.attackRate,weapon.itemBaseSpeed,weapon.itemAttackSpeed,bool(weapon.item));
    } else if(mode==7) speed=effectiveCastSpeed(clock.speed,attributes.fasterCast);
    else if(mode==3 || mode==6) speed=effectiveRecoverySpeed(clock.speed,mode==6?attributes.fasterBlock:attributes.fasterHitRecovery);
    else speed=effectiveOtherAnimationSpeed(clock.speed,player.totals.character.otherAnimationRate);
    return WeaponAttackTiming{std::string(playerMode),clock.frames,speed,clock.actionFrame,0};
}
}
