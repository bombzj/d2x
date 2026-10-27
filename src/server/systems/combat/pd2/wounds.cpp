#include "wounds.hpp"
#include "../system.hpp"
#include "../participants.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "gameplay/effects/queries.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "gameplay/rules/pd2/arithmetic.hpp"
#include <algorithm>
#include <array>

namespace d2x::server::combat {
std::optional<OpenWoundsApplication> pd2OpenWounds(const Ports &ports,const SpellImpact &impact,ParticipantView source,
    ParticipantView target,uint64_t tick,uint64_t &random) {
    const auto caster=skills::nativeCaster(ports.players,ports.monsters,source.id());
    if(!caster || !caster->programs() || !impact.weapon) return {};
    const auto &rules=*caster->programs();
    const auto weapon=impact.weapon->weapon.item;
    const auto stats=ports.effects.unitStatValues(source.id(),tick,weapon?std::optional{weapon}:std::nullopt),victim=ports.effects.unitStatValues(target.id(),tick);
    if(!stats || !victim) return {};
    const auto stat=[&](std::string_view name){return stats->get({rules.stats->names.at(std::string(name)),0});};
    const auto chance=stat("item_openwounds");
    if(chance<=0 || pd2UnitEventRandom(random)%100>=uint32_t(chance)) return {};
    int32_t rate=40,remaining=std::max(0,caster->level()-1);
    constexpr std::array increments{9,18,27,36,45};
    for(size_t tier=0;tier<increments.size() && remaining;++tier) {
        const auto levels=tier==4?remaining:std::min(remaining,tier?15:14);
        rate=nativeAdd(rate,nativeMultiply(levels,increments[tier]));remaining-=levels;
    }
    auto deep=stat("deep_wounds");
    if(target.player) {
        // Current Fn015 uses 8/6/4 percent in normal/nightmare/hell.
        const int factor=std::array{8,6,4}.at(size_t(std::clamp(target.player->persistent.difficulty,0,2)));
        rate=pd2MultiplyDivide(rate,factor,100);
        double scale=double(factor)*(source.monster && source.monster->rule.nativeClass==290?.5:1.0);
        auto retained=0;bool retainedState=false;
        if(const auto effects=ports.effects.unitEffects(source.id())) if(const auto state=effects->findState(225))
            if(const auto values=combatEffectNativeValues(state->spec)) {
                retained=StatSnapshot{*values}.get({rules.stats->names.at("deep_wounds"),0});retainedState=true;
            }
        deep=retainedState?nativeAdd(pd2MultiplyDivide(nativeSubtract(deep,retained),factor,100),
            pd2MultiplyDivide(retained,double(factor)/3.0,100)):pd2MultiplyDivide(deep,scale,100);
    }
    rate=nativeAdd(rate,nativeMultiply(deep,5));
    const auto &victimRules=target.player?*target.player->rules.skills->stats:*target.monster->rule.stats;
    auto resistance=victim->get({victimRules.names.at("damageresist"),0});
    resistance=resistance>=100?100:nativeSubtract(resistance,stat("passive_phys_pierce"));
    if(resistance<0) resistance=nativeDivide(resistance,2);
    if(target.player) resistance=std::min(resistance,50);
    rate=pd2MultiplyDivide(rate,nativeSubtract(100,resistance),100);
    if(target.monster) {
        auto owner=ports.monsters.minionOwner(target.id());
        for(size_t depth=0;owner && depth<32;++depth) {
            if(ports.players.findActor(owner)) {rate=nativeDivide(rate,4);break;}
            const auto next=ports.monsters.minionOwner(owner);if(next==owner) break;owner=next;
        }
    } else if(ports.areas.at(target.player->area).definition.town) rate=0;
    const bool pair=bool(ports.monsters.pd2RathmaPartner(target.id()));
    if(pair) rate=pd2MultiplyDivide(rate,50.,100);
    const OpenWoundsApplication *previous=nullptr;
    if(target.monster && target.monster->wound) previous=&*target.monster->wound;
    else if(target.player) if(const auto recovery=ports.effects.read().players.find(target.id());
        recovery!=ports.effects.read().players.end() && recovery->second.wound) previous=&*recovery->second.wound;
    OpenWoundsApplication result{rate,source.id(),impact.binding.credit,tick+125,tick+1,1,{},{},{}};
    if(previous && previous->until>tick && previous->pd2Stacks) {
        result=*previous;result.until=tick+125;
        if(result.pd2Stacks<3) {result.rate=nativeAdd(nativeChecked(result.rate),rate);++result.pd2Stacks;}
    }
    if(pair) {result.pd2Increment=rate;result.pd2Attacker=source.id();result.pd2Credit=impact.binding.credit;}
    return result;
}
}
