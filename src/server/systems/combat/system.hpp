#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/combat/damage_type.hpp"
#include "server/runtime/combat_rules.hpp"
#include "gameplay/skills/weapon_damage.hpp"
#include <deque>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::combat {
// Hit, mitigation and resource damage planning; death is a separate transition.
struct Damage {
    EntityId source, target; DamageType type; int64_t minimum{}, maximum{}; uint64_t action{};
    RegionId area; uint64_t impact{}; int rating{}, level{}, range{}, sourceSize{2};
    std::optional<WeaponDamage> weapon;
    int criticalChance{}, deadlyChance{};
    bool reactionsStarted{}, cancelled{};
    std::optional<CombatModifiers> attackModifiers{};
    uint8_t monsterMode{4};
    uint64_t sourceInterruption{};
    std::optional<uint64_t> actionRandom{};
};
// Targets are captured at missile impact, not looked up again by radius on retry.
struct SpellImpact {
    EntityId projectile, source;
    RegionId area;
    DamageType type{DamageType::Fire};
    int64_t damage{};
    std::vector<EntityId> targets;
    size_t next{};
    uint64_t occurrence{}, coldFrames{}, nextDelay{};
    bool freeze{}, knockback{};
    bool returnFire{}, reaction{};
    int coldPierce{};
    std::array<int,3> coldDivisor{1,1,1}, freezeDivisor{1,1,1}, staticFloors{};
    int staticPercent{}, staticMinimum{};
    int64_t minimumStaticDamage{};
    std::vector<int64_t> targetDamage{};
    uint8_t hitClass{};
    std::optional<WeaponSkillDamage> weapon{};
    uint64_t poisonFrames{};
    std::optional<bool> weaponHit{};
    std::optional<MonsterHit> monsterHit{};
    MonsterHitStates monsterStates{};
    int monsterRating{}, monsterLevel{};
    bool monsterToHit{};
    int64_t sourceHeal{};
    std::optional<uint64_t> contactRandom{};
    bool unblockable{};
    bool targetApplied{};
    int64_t selfDamage{};
    int64_t healing{}; bool undeadOnly{};
    std::optional<WeaponSkillSpec> conversion{};
    int healingOverlay{-1};
};
struct SpellPlan { std::list<SpellImpact> spells; size_t targets{}; };
struct State { std::vector<Damage> pending; std::list<SpellImpact> spells; size_t spellTargets{}; uint32_t hitClassCursor{}; };
struct Ports { const PlayerStore &players; monsters::System &monsters; const AreaStore &areas; transactions::System &transactions; uint64_t &random; EventOutbox &events; effects::System &effects; inventory::System &inventory; };
class System {
    State state_;
    const Ports ports_;
    StepStatus resolveSpells(TickContext);
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> enqueue(const Damage &);
    bool weaponContact(const WeaponSkillDamage &,EntityId,uint64_t,uint64_t &) const;
    DomainResult<> enqueue(SpellImpact);
    DomainResult<SpellPlan> prepareSpells(std::vector<SpellImpact>) const;
    void commitSpells(SpellPlan &&) noexcept;
    void cancel(EntityId);
    StepStatus step(TickContext, FrameFacts &);
};
}
