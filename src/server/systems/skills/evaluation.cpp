#include "evaluation.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/skills/passive_stats.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/skills/resolve.hpp"
#include "gameplay/skills/rank_bonus.hpp"
#include "gameplay/skills/amazon_summon_spec.hpp"
#include "gameplay/skills/summon_resolve.hpp"
#include "gameplay/skills/aura_resolve.hpp"
#include "server/systems/attributes/monster_sources.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x::server::skills {
namespace {
int32_t periodicDelay(const PlayerState &player,int skill,int rank) {
    auto input=formulaInput(player);
    SkillFormulaValues values(*player.rules.skills->formulas,input);
    const auto delay=values.calculation(FormulaDomain::Skill,skill,rank,"perdelay");
    if(!delay) throw std::runtime_error("Unavailable native periodic skill delay: "+std::to_string(skill));
    return *delay;
}
}
uint64_t periodicTick(const PlayerState &player,int skill,int rank,uint64_t tick) {
    return skillPeriodicTick(tick,periodicDelay(player,skill,rank));
}
uint64_t periodicDuration(const PlayerState &player,int skill,int rank,uint64_t tick) {
    const auto duration=skillPeriodicDuration(nativeNarrow(uint32_t(tick)),periodicDelay(player,skill,rank));
    return uint32_t(duration);
}
SkillFormulaInput formulaInput(const PlayerState &player,RuleRandom random,std::optional<EntityId> weapon) {
    return formulaInput(player,player.persistent.player,player.totals,std::move(random),weapon);
}
SkillFormulaInput formulaInput(const PlayerState &player,const CharacterRecord &record,const attributes::Totals &totals,
    RuleRandom random,std::optional<EntityId> weapon) {
    SkillFormulaInput input{record.skillRanks,totals.skillRanks};
    input.statRules=player.rules.character->stats.get();
    input.actor=totals.statSources.get();input.stats=&totals.nativeStats;
    if(weapon) input.stats=&totals.handStats.at(*weapon);
    const auto selected=weapon.value_or(totals.equipment.weapons[0].item);
    if(selected) {
        input.weaponStats=&totals.itemStats.at(selected);
        for(const auto &entry:totals.equipment.weapons) if(entry.item==selected) {input.twoHanded=entry.twoHanded;break;}
    }
    input.attackRatingClassFactor=player.definition.toHitFactor;
    input.random=std::move(random);
    if(!player.rules.skills->formulas) throw std::runtime_error("Missing native skill level catalog");
    input.referenceRanks=prepareSkillFormulaRanks(record.skillRanks,totals.skillRanks,
        player.rules.character->innateSkills,totals.chargedSkills,player.rules.skills->formulas->maximumSkillRank);
    if(!input.statRules || !input.actor) throw std::runtime_error("Missing player native formula stat sources");
    const auto resource=[&](std::string_view name,Fixed8 value) {
        const auto id=input.statRules->names.find(name);
        if(id==input.statRules->names.end()) throw std::runtime_error("Missing player native resource stat");
        input.currentResources.emplace(StatKey{id->second,0},value.raw());
    };
    resource("hitpoints",record.hp);
    resource("mana",record.mana);
    resource("stamina",record.stamina);
    return input;
}
SkillFormulaInput formulaInput(const monsters::Actor &unit,StatSnapshot sampled,RuleRandom random) {
    if(!unit.rule.stats || !unit.rule.formulas || !unit.nativeStats || !unit.nativeStats->sources)
        throw std::runtime_error("Missing native monster formula sources");
    const auto &rules=*unit.rule.stats;
    SkillFormulaInput input{unit.rule.baseSkillRanks,unit.rule.baseSkillRanks};
    input.statRules=&rules;input.actor=unit.nativeStats->sources.get();input.random=std::move(random);
    input.sampledStats=std::make_shared<StatSnapshot>(std::move(sampled));input.stats=input.sampledStats.get();
    const auto effective=nativeMonsterSkillRanks(unit.rule.baseSkillRanks,*input.stats,rules,*unit.rule.formulas);
    const auto charges=attributes::monsterChargedSkills(unit,*input.actor);
    input.referenceRanks=prepareSkillFormulaRanks(unit.rule.baseSkillRanks,effective,{},charges,unit.rule.formulas->maximumSkillRank);
    input.currentResources.emplace(StatKey{rules.names.at("hitpoints"),0},nativeChecked(unit.life));
    if(unit.petWeapon && unit.petWeapon->item) {
        const auto selected=unit.petWeapon->item;
        const auto readWeapon=[&](auto &&self,const StatList &list)->const StatList * {
            if(list.ownerKind==StatOwnerKind::Item && list.source.owner==selected && list.handWeapon) return &list;
            for(const auto &child:list.contributions) if(child) if(const auto *weapon=self(self,*child)) return weapon;
            return nullptr;
        };
        const auto *weapon=readWeapon(readWeapon,*input.actor);
        if(!weapon) throw std::runtime_error("Missing native monster formula weapon source");
        input.sampledWeaponStats=std::make_shared<StatSnapshot>(evaluateStatList(rules,*weapon,{input.actor,{}, {}}));
        input.weaponStats=input.sampledWeaponStats.get();input.twoHanded=unit.petWeapon->twoHanded;
    }
    return input;
}
int mastery(const PlayerState &player, const std::map<int, std::pair<int, int>> &curves) {
    int value = 0;
    for (const auto &[id, curve] : curves)
        if (const auto rank = player.totals.skillRanks.find(id); rank != player.totals.skillRanks.end())
            value += skillRankBonus(curve, rank->second);
    return value;
}
SkillCastSpec evaluate(const PlayerState &player, int skill, int rank) {
    const auto &rules = *player.rules.skills;
    if(!rules.formulas) throw std::runtime_error("Missing native skill formula catalog");
    const auto &definition=rules.definitions.at(skill).spec;
    auto selected=player.totals.equipment.weapons[0].item;
    if(definition.weapon && definition.weapon->leftHand)
        for(const auto &weapon:player.totals.equipment.weapons) if(weapon.leftHand) {selected=weapon.item;break;}
    auto formulaContext=formulaInput(player,{},selected);
    SkillFormulaValues formulas(*rules.formulas,formulaContext);
    auto result=resolveSkill(definition,
        {rank, player.persistent.player.skillRanks,player.totals.character.combat.fireSkillDamagePercent,
         player.totals.character.combat.lightningSkillDamagePercent, player.totals.character.combat.coldSkillDamagePercent,
         player.totals.character.combat.poisonSkillDamagePercent,&formulas});
    if(definition.summon && definition.summon->amazon) result.summon=resolveAmazonSummon(*definition.summon,rank,player.persistent.player.level,player.persistent.difficulty,player.totals.character,player.persistent.player.skillRanks,&formulas,skill);
    else if(definition.summon) {
        const auto effective=[&](int id) {const auto found=player.totals.skillRanks.find(id);return found==player.totals.skillRanks.end()?0:found->second;};
        result.summon=resolveSummon(*definition.summon,rank,effective(definition.summon->masterySkill),effective(definition.summon->resistSkill),player.persistent.player.level,player.persistent.difficulty,player.persistent.player.skillRanks,&formulas,skill);
    }
    return result;
}
std::optional<ChargedSkill> chargedSource(const PlayerState &player,int skill,bool right) {
    const auto owner=player.persistent.player.selectedSkillOwners.at(player.persistent.player.weaponSet*2+(right?1:0));
    const auto source=resolveSkillUseSource(skill,owner,false,0,player.totals.chargedSkills);
    return source ? source->charged : std::nullopt;
}
std::optional<SkillUseSource> selectedSource(const PlayerState &player, int skill, bool right) {
    const auto owner = player.persistent.player.selectedSkillOwners.at(player.persistent.player.weaponSet * 2 + (right ? 1 : 0));
    const auto rank = player.totals.skillRanks.find(skill);
    return resolveSkillUseSource(skill, owner, player.rules.character->innateSkills.contains(skill),
        rank == player.totals.skillRanks.end() ? 0 : rank->second, player.totals.chargedSkills);
}
int coldPierce(const PlayerState &player) {
    return player.totals.character.combat.coldPierce;
}
}
