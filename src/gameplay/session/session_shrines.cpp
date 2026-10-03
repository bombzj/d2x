#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/resolve.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "core/random.hpp"
#include "session_impl.hpp"
#include "content/monsters/monster_enchantment.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace d2x {
namespace {
uint32_t shrineRoll(uint64_t &seed, uint32_t bound) {
    return limitedRandom(seed, bound);
}
}
bool GameSessionImpl::applyShrine(int code, EntityId source, Vec position) {
    const auto found = content_.shrines.find(activeShrineCode(code));
    auto &player = simulation_->state_.player;
    if (found == content_.shrines.end() || player.dead || player.hp <= 0) return false;
    const auto &shrine = found->second;
    code = shrine.code;
    if (code <= 3) {
        if (code != 3) player.hp = float(characterStats().maxLife);
        if (code != 2) player.mana = float(characterStats().maxMana);
        return true;
    }
    if (code >= 17) return applySpecialShrine(shrine, position);
    const auto record = content_.states.find(shrineStateName(code));
    if (record == content_.states.end() || shrine.durationFrames <= 0) return false;
    CombatEffectSpec effect;
    effect.state = record->second.definition;
    effect.source = {CombatEffectSource::Shrine, source, code, 0};
    effect.duration = EffectFrame(shrine.durationFrames);
    auto &modifiers = effect.modifiers;
    switch (code) {
    case 6: modifiers.combat.defensePercent = shrine.argument0; break;
    case 7: {
        // ObjMode::OBJMODE_GetToHitPercentage snapshots Attack's flat to-hit.
        // Its integer percentage division and extra class factor are intentional.
        const auto &weapon = simulation_->state_.player.equipment.weapons[0];
        const int64_t rate = weapon.potion ? 0 : weapon.baseAttackRating;
        const int64_t bonus = rate * (weapon.attackRatingPercent / 100 + 1) +
                              characterDefinition_.toHitFactor;
        modifiers.attackRating = int(std::clamp<int64_t>(bonus * shrine.argument0 / 100,
            std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
        modifiers.combat.damagePercent = shrine.argument1;
        break;
    }
    case 8:
        modifiers.fireResist = shrine.argument0;
        modifiers.combat.preventBurn = true;
        break;
    case 9: modifiers.coldResist = shrine.argument0; break;
    case 10: modifiers.lightningResist = shrine.argument0; break;
    case 11:
        modifiers.poisonResist = shrine.argument0;
        modifiers.combat.preventPoison = true;
        break;
    case 12: modifiers.combat.allSkills = 2; break; // D2Common D2Skills: shrine_skill.
    case 13: modifiers.combat.manaRecovery = shrine.argument0; break;
    case 14:
        // The original multiplies the existing skill_staminapercent stat, not
        // maximum stamina. An unmodified player therefore gets zero extra capacity.
        modifiers.staminaPercent = int(int64_t(shrine.argument0) * characterStats().staminaPercent / 100);
        modifiers.staminaRecoveryBonus = 1000;
        effect.restoreStaminaOnRemoval = true;
        player.stamina = float(characterStats().maxStamina) + float(2 * modifiers.staminaPercent) / 256.f;
        break;
    case 15: modifiers.combat.experiencePercent = shrine.argument0; break;
    default: return false;
    }
    const auto applied = player.combatEffects.apply(std::move(effect), state().frame);
    simulation_->combatEffectsChanged(applied.removed);
    shrineStatuses_.clear();
    shrineStatuses_.push_back({code, shrine.name, shrine.effect,
        state().time + float(shrine.durationFrames) / 25.f, applied.handle});
    return true;
}
bool GameSessionImpl::applySpecialShrine(const ShrineDefinition &shrine, Vec position) {
    auto &player = simulation_->state_.player;
    if (shrine.code == 17) return openShrinePortal();
    if (shrine.code == 20) return upgradeShrineMonster(position);
    if (shrine.code == 18) {
        ItemHandle ingredient;
        std::string output;
        for (auto id : inventory_.contents(playerContainers_.backpack)) {
            const auto *item = inventory_.item(id);
            const auto *definition = inventory_.catalog().find(item->definition);
            if (definition->betterGem.empty() || definition->betterGem == "non") continue;
            ingredient = item->handle();
            output = definition->betterGem;
            break;
        }
        if (output.empty()) {
            // Original six-colour fallback deliberately excludes skulls.
            constexpr std::array codes{"gcw", "gcr", "gcg", "gcb", "gcy", "gcv"};
            output = codes[shrineRoll(shrineRandom_, unsigned(codes.size()))];
        }
        const auto before = inventory_.state_;
        auto result = inventory_.createItem(output, 1, GroundLocation{region().definition.id, player.pos},
            unsigned(player.level), {}, player.pos);
        if (!result) {
            publishInventory(std::move(result), ingredient.id);
            return false;
        }
        if (ingredient.id) {
            auto consumed = inventory_.consume(ingredient, 1, inventoryAccess());
            if (!consumed) {
                inventory_.state_ = before;
                publishInventory(std::move(consumed), ingredient.id);
                return false;
            }
            result.changes.insert(result.changes.end(), consumed.changes.begin(), consumed.changes.end());
        }
        publishInventory(std::move(result), ingredient.id);
        return true;
    }
    const int rank = std::clamp(player.level / 5, 1, 8);
    if (shrine.code == 19) {
        const auto *entry = content_.skills.find(47); // Native missile 62 uses Fire Ball's damage.
        if (!entry || !entry->spell) return false;
        const auto skill = resolveSkill(*entry->spell, {rank, player.skillRanks,
                                       fireMasteryPercent(), lightningMasteryPercent()});
        if (skill.missileId != 62 || !skill.missileImpact) return false;
        auto reduce = [&](float &hp, Vec target) {
            if (hp <= 0 || !map().activation.nearby(position, target)) return;
            const Vec offset = target - position;
            if (offset.x * offset.x + offset.y * offset.y >
                float(shrine.argument1) * float(shrine.argument1)) return;
            // Direct stat subtraction bypasses resistances and does not award kills.
            hp -= float(int64_t(std::floor(hp)) * shrine.argument0 / 100);
        };
        for (auto target : simulation_->combatUnits()) reduce(*target.life, *target.position);
        for (int x = 1; x <= 4; ++x)
            for (int y = 1; y <= 4; ++y) {
                const Vec direction{float((x & 1 ? 1 : -1) * 5 * x),
                                    float((y & 1 ? 1 : -1) * 5 * y)};
                const int low = int(skill.minimumDamage * 256.f), high = int(skill.maximumDamage * 256.f);
                const float damage = float(low + shrineRoll(shrineRandom_, unsigned(std::max(0, high - low)))) / 256.f;
                Missile missile{ids_.allocate(), player.id, position, direction.unit() * skill.missileVelocity,
                    skill.missileLifetime, skill.effect, false, skill.missileId, damage};
                missile.impact = skill.missileImpact;
                missile.impactDamage.channels[size_t(MonsterDamageType::Fire)] = damage;
                missile.skillRank = rank;
                missile.combatRandom = childRandom(simulation_->unitRandom_);
                simulation_->state_.area.missiles.push_back(std::move(missile));
            }
        simulation_->emit(MissileReleased{skill.missileId});
        return true;
    }
    if (shrine.code == 21 || shrine.code == 22) {
        const int missileId = shrine.code == 21 ? 45 : 48;
        const WeaponProjectileSpec *projectile = nullptr;
        for (const auto &[code, item] : content_.items.entries())
            if (item.base.projectile && item.base.projectile->id == missileId)
                projectile = &*item.base.projectile;
        if (!projectile || !projectile->impact || projectile->velocityUnits <= 0 ||
            shrine.argument0 < 0 || shrine.argument1 <= shrine.argument0) return false;
        const int count = shrine.argument0 + int(shrineRoll(shrineRandom_, unsigned(shrine.argument1 - shrine.argument0)));
        const auto code = shrine.code == 21 ? "opm" : "gpm";
        for (int i = 0; i < count; ++i) {
            auto result = inventory_.createItem(code, 1, GroundLocation{region().definition.id, player.pos},
                                               unsigned(player.level), {}, player.pos);
            publishInventory(std::move(result), {});
        }
        constexpr Vec offsets[]{{-6,6}, {-6,-6}, {0,6}, {0,-6}, {6,6}, {6,-6}};
        for (const auto offset : offsets) {
            const int frames = std::max(1, int(int64_t(missileDistance(position, position + offset)) *
                                                4096 / projectile->velocityUnits));
            Missile missile{ids_.allocate(), player.id, position, offset.unit() * projectile->speed,
                float(frames) / 25.f, SkillBehavior::None, false, missileId};
            missile.groundTargeted = true;
            missile.impact = projectile->impact;
            missile.skillRank = rank;
            for (size_t channel = 0; channel < projectile->damage.size(); ++channel) {
                const auto &range = projectile->damage[channel];
                missile.impactDamage.channels[channel] = float(range.minimum +
                    int(shrineRoll(shrineRandom_, unsigned(std::max(0, range.maximum - range.minimum))))) / 256.f;
            }
            missile.combatRandom = childRandom(simulation_->unitRandom_);
            simulation_->state_.area.missiles.push_back(std::move(missile));
        }
        simulation_->emit(MissileReleased{missileId});
        return true;
    }
    return false;
}
bool GameSessionImpl::openShrinePortal() {
    const auto townId = portalTown(region().definition.id);
    if (!portalResources_ || !townId || portalReach_ <= 0 || region().definition.safe ||
        state().nextPortalRevision == std::numeric_limits<uint64_t>::max()) return false;
    ensureRegion(*townId);
    if (!townPortalArrivals_.contains(*townId)) return false;
    const auto town = std::find_if(regions_.begin(), regions_.end(), [&](const Region &entry) {
        return entry.definition.id == *townId;
    });
    if (town == regions_.end()) return false;
    // Search the existing collision field at each endpoint. Public portals have
    // no player owner: opening a scroll cannot replace them or consume them on return.
    auto freePosition = [&](const Region &region, Vec desired) -> std::optional<Vec> {
        const auto existing = portals(region.definition.id);
        std::optional<Vec> best;
        float bestDistance = std::numeric_limits<float>::max();
        for (int y = 0; y < region.map.grid.height; ++y)
            for (int x = 0; x < region.map.grid.width; ++x) {
                const Vec point{float(x) + .5f, float(y) + .5f};
                const float distance = (point - desired).length();
                if (distance >= bestDistance || !region.map.grid.walkable(point)) continue;
                bool free = true;
                for (const auto &portal : existing)
                    if ((portal.position - point).length() < 3.f) free = false;
                if (free && region.map.grid.segment(desired, point)) {
                    best = point;
                    bestDistance = distance;
                }
            }
        return best;
    };
    const Vec desired = state().player.pos + Vec{5, 5};
    const Vec fieldOrigin = map().grid.walkable(desired) ? desired : state().player.pos;
    auto field = freePosition(region(), fieldOrigin), arrival = freePosition(*town, townPortalArrivals_.at(*townId));
    if (!field || !arrival) return false;
    simulation_->state_.publicPortals.push_back({true, ++simulation_->state_.nextPortalRevision,
        region().definition.id, *field, *arrival, state().time, false});
    return true;
}
bool GameSessionImpl::upgradeShrineMonster(Vec) {
    Enemy *nearest = nullptr;
    float distance = std::numeric_limits<float>::max();
    for (auto &enemy : simulation_->state_.area.enemies) {
        // Original predicate only admits NU/WL, normal, hostile, mortal units.
        if (enemy.hp <= 0 || enemy.identity.rank != MonsterRank::Normal || enemy.enchantment ||
            enemy.attack > 0 || enemy.hitFlash > 0 || enemy.freeze > 0 || enemy.stun > 0 ||
            enemy.skill2Remaining > 0 || enemy.resurrectionRemaining > 0 || enemy.aiRunning ||
            !map().activation.nearby(state().player.pos, enemy.pos)) continue;
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (!record || !monsterShrineEligible(content_, *record)) continue;
        const float candidate = (enemy.pos - state().player.pos).length();
        if (candidate < distance) { nearest = &enemy; distance = candidate; }
    }
    // As in ObjMode, the shrine is spent even if no eligible unit is nearby.
    if (!nearest) return true;
    const auto *record = monsterContent_.find(nearest->identity.monster);
    const auto base = resolvedMonsterCombat(nearest->identity, state().area.region);
    if (!base) {
        simulation_->emit(InteractionFailed{{}, "Original monster combat data is unavailable."});
        return false;
    }
    auto rank = MonsterRank::Unique;
    auto mods = rollMonsterEnchantment(content_, *record, *base, state().population.difficulty,
                                       nearest->combatRandom, rank);
    // Only explicit original party ownership propagates the initialization.
    // Group membership alone also contains unrelated ordinary pack members.
    for (auto &minion : simulation_->state_.area.enemies) {
        if (minion.hp <= 0 || minion.enchantment || nearest->identity.spawnKey.empty() ||
            minion.identity.ownerSpawnKey != nearest->identity.spawnKey) continue;
        const auto *record = monsterContent_.find(minion.identity.monster);
        const auto base = resolvedMonsterCombat(minion.identity, state().area.region);
        if (!record || !base) continue;
        auto inherited = inheritedMonsterEnchantment(content_, *record, *base,
            state().population.difficulty, mods);
        const auto life = int64_t(minion.maxHp * 256.f) * (100 + inherited.lifePercent) / 100;
        minion.hp = minion.maxHp = float(std::max<int64_t>(1, life)) / 256.f;
        minion.identity.rank = MonsterRank::Minion;
        minion.enchantment = std::move(inherited);
    }
    // UMod2 operates on the existing HP roll and fully heals the transformed unit.
    int64_t life = int64_t(nearest->maxHp * 256.f);
    life += life * mods.lifePercent / 100;
    life = life * mods.lifeScalePercent / 100;
    nearest->identity.rank = rank;
    nearest->enchantment = std::move(mods);
    nearest->hp = nearest->maxHp = float(std::max<int64_t>(1, life)) / 256.f;
    nearest->nextAuraFrame = state().frame;
    return true;
}
} // namespace d2x
