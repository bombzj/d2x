#include "native_caster.hpp"
#include "evaluation.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/skills/resolve.hpp"
#include "gameplay/skills/assassin_summon.hpp"
#include <stdexcept>
namespace d2x::server::skills {
EntityId NativeCaster::id() const {return player?player->actor:monster->id;}
RegionId NativeCaster::area() const {return player?player->area:monster->area;}
Vec NativeCaster::position() const {return player?player->position:monster->position;}
int NativeCaster::level() const {return player?player->persistent.player.level:monster->rule.level;}
uint64_t NativeCaster::random() const {return player?player->combatRandom:monster->combatRandom;}
const SkillRules *NativeCaster::programs() const {return controller?controller->rules.skills.get():unitPrograms;}
ActorContext NativeCaster::context(const AreaStore &areas,uint64_t tick) const {return {controller?controller->player:PlayerId{},id(),area(),areas.at(area()).generation,0,tick};}
SkillFormulaInput NativeCaster::formulaInput(const effects::System &effects,uint64_t tick,RuleRandom random) const {
    if(player) return skills::formulaInput(*player,std::move(random));
    const auto sampled=effects.unitStatValues(monster->id,tick);if(!sampled) throw std::logic_error("Missing native caster stat view");
    return skills::formulaInput(*monster,*sampled,std::move(random));
}
std::optional<NativeCaster> nativeCaster(const PlayerStore &players,const monsters::System &monsters,EntityId id) {
    if(const auto *player=players.findActor(id)) return NativeCaster{player,player,nullptr};
    const auto *monster=monsters.find(id);if(!monster) return {};
    return NativeCaster{nullptr,monster->owner?players.find(*monster->owner):nullptr,monster,monsters.skillPrograms()};
}
SkillCastSpec evaluateNativeSkill(const NativeCaster &caster,const effects::System &effects,int skill,int rank,uint64_t tick) {
    if(caster.player) return evaluate(*caster.player,skill,rank);
    const auto *programs=caster.programs();if(!programs) throw std::logic_error("Missing native caster skill programs");
    auto input=caster.formulaInput(effects,tick);SkillFormulaValues values(*programs->formulas,input);
    const auto stat=[&](std::string_view name) {const auto value=values.stat(name);if(!value) throw std::logic_error("Missing native caster mastery");return *value;};
    const auto &definition=programs->definitions.at(skill).spec;
    auto result=resolveSkill(definition,{rank,input.baseRanks,stat("passive_fire_mastery"),stat("passive_ltng_mastery"),
        stat("passive_cold_mastery"),stat("passive_pois_mastery"),&values});
    if(definition.summon && definition.summon->assassin && caster.controller) {
        // SkillAss's summon helper walks the AI owner chain first; base and
        // inherited stats are evaluated against that top-level player.
        auto ownerInput=skills::formulaInput(*caster.controller);SkillFormulaValues ownerValues(*programs->formulas,ownerInput);
        result.summon=resolveAssassinSummon(*definition.summon,rank,caster.controller->persistent.player.level,
            caster.monster->rule.difficulty,ownerValues,skill,programs->pd2);
    }
    result.manaCost=result.startMana=0;return result;
}
}
