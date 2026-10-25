#include "system.hpp"
#include "player_contact.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/combat/pd2/leech.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x::server::combat {
DomainStatus System::advanceMissileDrain(SpellImpact &impact,ParticipantView target,int64_t physical,
    int64_t life,int64_t mana,int64_t stamina,TickContext tick) {
    auto &contact=*impact.missileContact;
    if(contact.drainCommitted) return DomainStatus::Applied;
    const Participants participants{ports_.players,ports_.monsters,ports_.social};const auto source=participants.find(impact.source);
    if(!source.id()) {contact.drainCommitted=true;return DomainStatus::Applied;}
    if(!contact.drainPrepared) {
        int32_t targetMana{},targetStamina{};
        if(target.player) {targetMana=target.player->persistent.player.mana.raw();targetStamina=target.player->persistent.player.stamina.raw();}
        else if(target.monster && (mana || stamina)) {
            const auto values=ports_.effects.unitStatValues(target.id(),tick.tick);
            if(!values || !target.monster->rule.stats) throw std::runtime_error("Missing native drain target resources");
            targetMana=values->get({target.monster->rule.stats->names.at("mana"),0});
            targetStamina=values->get({target.monster->rule.stats->names.at("stamina"),0});
        }
        const bool percentage=source.player || (source.monster && source.monster->hireling);
        const auto difficulty=source.player?size_t(source.player->persistent.difficulty):size_t(0);
        contact.drain=calculateNativeDrain({nativeChecked(life),nativeChecked(mana),nativeChecked(stamina),nativeChecked(physical),
            std::max(0,nativeSubtract(target.player?target.player->persistent.player.hp.raw():nativeChecked(target.monster->life),
                nativeChecked(target.player?contact.playerExtras.crushing:contact.extras.crushing))),targetMana,targetStamina,
            target.monster?target.monster->rule.drain:100,
            source.player?nativeSubtract(source.player->totals.character.maxLifeFixed.raw(),source.player->persistent.player.hp.raw()):nativeChecked(source.monster->maximumLife-source.monster->life),
            percentage,bool(source.player),impact.missileDamage->lifeStealDivisor.at(difficulty),impact.missileDamage->manaStealDivisor.at(difficulty)});
        contact.drainExpected=contact.drainRandom=source.player?source.player->combatRandom:source.monster->combatRandom;
        const auto &data=*impact.missileDamage;
        if(contact.drain.chooseOverlay) contact.drainOverlay=limitedRandom(contact.drainRandom,2)?data.manaStealOverlay:data.lifeStealOverlay;
        else if(contact.drain.lifeOverlay) contact.drainOverlay=data.lifeStealOverlay;
        else if(contact.drain.manaOverlay) contact.drainOverlay=data.manaStealOverlay;
        if((contact.drain.lifeOverlay || contact.drain.manaOverlay) && contact.drainOverlay<0) throw std::runtime_error("Missing native drain overlay");
        contact.drainPrepared=true;
    }
    if(contact.drain.lifeGain<=0 && contact.drain.manaGain<=0 && contact.drainOverlay<0 && contact.drainExpected==contact.drainRandom) {
        contact.drainCommitted=true;return DomainStatus::Applied;
    }
    std::vector<DomainFact> facts;
    const auto region=source.player?source.player->area:source.monster->area;
    if(contact.drainOverlay>=0) facts.emplace_back(OverlayFact{source.id(),uint8_t(source.player?0:1),region,contact.drainOverlay});
    if(source.player) {
        const auto &player=*source.player;const auto *area=ports_.areas.find(player.area);if(!area) return DomainStatus::Stale;
        transactions::CharacterEdit edit{{player.player,player.actor,player.area,area->generation,0,tick.tick},
            player.inventoryRevision,player.characterRevision,player.persistent.player};
        if(player.persistent.player.hp>0 && contact.drain.lifeGain>0) {
            const auto next=std::min(player.totals.character.maxLifeFixed.raw(),nativeAdd(edit.player.hp.raw(),contact.drain.lifeGain));
            auto pd2=player.totals.character.combat.pd2;
            if(pd2) {
                const auto values=ports_.effects.unitStatValues(source.id(),tick.tick);
                if(!values || !player.rules.skills || !player.rules.skills->stats) return DomainStatus::Unavailable;
                pd2->lifeDrainCap=values->get({player.rules.skills->stats->names.at("lifedrain_percentcap"),0});
            }
            if(!pd2 || pd2LifeDrainWriteAllowed(edit.player.hp.raw(),next,player.totals.character.maxLifeFixed.raw(),pd2->lifeDrainCap))
                edit.player.hp=Fixed8::fromRaw(next);
        }
        if(contact.drain.manaGain>0)
            edit.player.mana=Fixed8::fromRaw(std::min(player.totals.character.maxManaFixed.raw(),nativeAdd(edit.player.mana.raw(),contact.drain.manaGain)));
        edit.random=transactions::UnitRandomEdit{contact.drainExpected,contact.drainRandom};edit.publicFacts=std::move(facts);
        auto prepared=ports_.transactions.prepare(std::move(edit));if(!prepared) return prepared.status;
        const auto result=ports_.transactions.commit(std::move(*prepared.value));if(!result) return result.status;
    } else {
        const auto &monster=*source.monster;
        attributes::MonsterResourceTrace resources(monster,tick.tick);const auto sources=ports_.effects.unitStatSources(monster.id,tick.tick);
        resources.sampleSources(sources);
        if(monster.life>0 && contact.drain.lifeGain>0) {
            const auto next=std::min(nativeChecked(monster.maximumLife),nativeAdd(nativeChecked(monster.life),contact.drain.lifeGain));
            const auto pd2=ports_.effects.combatModifiers(source.id(),tick.tick).pd2;
            if(!pd2 || pd2LifeDrainWriteAllowed(nativeChecked(monster.life),next,nativeChecked(monster.maximumLife),pd2->lifeDrainCap))
                resources.seedLife(Fixed8::fromRaw(next));
        }
        if(contact.drain.manaGain>0) {
            const auto values=resources.values(sources);const auto &stats=*monster.rule.stats;
            const auto maximum=values.get({stats.names.at("maxmana"),0});
            resources.writeBaseStat("mana",std::min(maximum,nativeAdd(values.get({stats.names.at("mana"),0}),contact.drain.manaGain)),sources);
        }
        auto change=resources.take();
        if(contact.drainRandom!=contact.drainExpected) {change.random=std::pair{contact.drainExpected,contact.drainRandom};change.changed=true;}
        if(!ports_.monsters.canCommitLife(change)) return DomainStatus::Stale;
        if(!facts.empty() && !ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},region},std::move(facts)})) return DomainStatus::Capacity;
        if(!ports_.monsters.commitLife(std::move(change))) throw std::logic_error("Native drain source publication changed during commit");
    }
    contact.drainCommitted=true;return DomainStatus::Applied;
}
}
