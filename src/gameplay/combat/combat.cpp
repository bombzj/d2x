#include "core/random.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/combat/accuracy.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
bool Simulation::missilePathClear(int missileId, Vec from, Vec to) const {
    const auto found = missileCollisions_.find(missileId);
    return found != missileCollisions_.end() && grid_->missileSegment(from, to, found->second);
}
bool Simulation::clipMissilePath(int missileId, Vec from, Vec &to) const {
    if (missilePathClear(missileId, from, to)) return false;
    float clear = 0, blocked = 1;
    const auto delta = to - from;
    for (int step = 0; step < 12; ++step) {
        const float middle = (clear + blocked) * .5f;
        if (missilePathClear(missileId, from, from + delta * middle)) clear = middle;
        else blocked = middle;
    }
    to = from + delta * clear;
    return true;
}
void Simulation::damageEnemy(Enemy &enemy, float amount, EntityId source, float chill,
                             bool ignoreActivation, MonsterDamageType type, bool alreadyMitigated,
                             bool playerKillEffects, bool freezeHit) {
    if (enemy.hp <= 0 || (!ignoreActivation && !active(enemy.pos)))
        return;
    if (!ignoreActivation && !alreadyMitigated && monsterResistance_)
        if (auto resistance = monsterResistance_(enemy, state_.area.region, type)) {
            const int pierce = type == MonsterDamageType::Cold && source == state_.player.id &&
                               *resistance < 100 && coldPierce_ ? coldPierce_() : 0;
            const int effectiveResistance = *resistance - pierce;
            amount = mitigateMonsterDamage(amount, effectiveResistance);
            if (type == MonsterDamageType::Cold)
                chill *= float(std::clamp(100 - effectiveResistance, 0, 200)) / 100.f;
        }
    if (amount <= 0) return;
    enemy.hp = std::max(0.f, enemy.hp - amount);
    if (type != MonsterDamageType::Poison) enemy.hitFlash = 0;
    if (enemy.hp > 0 && type != MonsterDamageType::Poison)
        enemy.hitFlash = monsterGetHitDuration_
            ? monsterGetHitDuration_(enemy.identity).value_or(.12f) : .12f;
    if (enemy.hp > 0 && monsterAi_)
        if (auto ai = monsterAi_(enemy); ai && ai->kind == MonsterAiKind::QuillRat)
            enemy.aiRetaliate = true;
    if (enemy.skill2Remaining > 0 && type != MonsterDamageType::Poison)
        enemy.skill2Remaining = enemy.skill2Duration = 0;
    if (freezeHit && monsterFreezable_) {
        if (auto freezable = monsterFreezable_(enemy)) {
            if (*freezable && chill > 0 && enemy.hp > 0) {
                enemy.freeze = std::max(enemy.freeze, chill / monsterFreezeDivisor_);
                enemy.route.clear();
            } else if (!*freezable) {
                enemy.chill = std::max(enemy.chill, chill);
            }
        }
    } else {
        enemy.chill = std::max(enemy.chill, chill);
    }
    if (enemy.identity.enchantment) {
        const auto &mods = *enemy.identity.enchantment;
        if (enemy.hp > 0 && type != MonsterDamageType::Poison && mods.has(17) &&
            !enemy.pendingUniqueLightningFrame)
            enemy.pendingUniqueLightningFrame = state_.frame + 2;
        if (enemy.hp == 0 && (mods.has(9) || mods.has(18)))
            enemy.deathEnchantmentFrame = state_.frame + 4;
    }
    if (enemy.hp > 0 && type != MonsterDamageType::Poison) emit(EnemyHit{enemy.id, enemy.kind});
    if (enemy.hp == 0) {
        enemy.freeze = 0;
        enemy.resurrectionRemaining = enemy.resurrectionDuration = 0;
        if (playerKillEffects && source == state_.player.id && !state_.player.dead) {
            auto &player = state_.player;
            player.hp = std::min(float(characterStats_.maxLife), player.hp + characterStats_.combat.lifeOnKill);
            player.mana = std::min(float(characterStats_.maxMana), player.mana + characterStats_.combat.manaOnKill);
        }
        if (!playerKillEffects && source == state_.player.id && state_.player.hireling.active() && hirelingAttributes_) {
            const auto stats = hirelingAttributes_();
            auto &merc = state_.player.hireling;
            merc.hp = std::min(float(stats.maxLife), merc.hp + stats.combat.lifeOnKill);
        }
        enemy.deathAge = 0;
        enemy.route.clear();
        enemy.aiPursuing = false;
        enemy.aiEscaping = false;
        enemy.aiCommanded = false;
        enemy.aiCircling = false;
        enemy.aiRunning = false;
        enemy.aiRetaliate = false;
        enemy.aiCharged = false;
        enemy.aiAdvanceRemaining = 0;
        enemy.aiPhase = 0;
        if (enemy.kind != MonsterKind::FoulCrowNest) enemy.aiLoop = 0;
        enemy.aiCorpse = {};
        enemy.webAuraRemaining = enemy.webTrailDistance = 0;
        enemy.attack = enemy.attackDuration = 0;
        enemy.attackImpact = -1;
        enemy.teleportTarget.reset();
        enemy.attackMode = 1;
        ++state_.area.kills;
        emit(EnemyDied{enemy.id, source, enemy.kind, state_.area.region, enemy.pos, enemy.identity,
                       state_.population.difficulty, !playerKillEffects, enemy.combatRandom});
    }
}
void Simulation::meleeDamage(Enemy &enemy, const WeaponDamage &weapon) {
    auto &player = state_.player;
    rollRandom(player.combatRandom);
    const auto defense = monsterDefense_ ? monsterDefense_(enemy, state_.area.region) : std::nullopt;
    if (!defense) { state_.message = "Original monster defense is unavailable."; return; }
    if (uint32_t(player.combatRandom) % 100 >= unsigned(weaponHitChance(player.level,
        weapon.baseAttackRating, weapon.attackRatingPercent, weapon.target, *defense, enemy.identity.rank))) return;
    rollRandom(player.combatRandom);
    const int targetBonus = (defense->demon ? std::max(0, weapon.target.demonDamage) : 0) +
        (defense->undead ? std::max(0, weapon.target.undeadDamage + (weapon.blunt ? 50 : 0)) : 0);
    const int bonus = std::max(-90, weapon.damagePercent + targetBonus);
    const int64_t minimum = weapon.meleeBaseMinimum + int64_t(weapon.meleeBaseMinimum) *
                            (bonus + weapon.minimumDamagePercent) / 100;
    const int64_t maximum = weapon.meleeBaseMaximum + int64_t(weapon.meleeBaseMaximum) *
                            (bonus + weapon.maximumDamagePercent) / 100;
    const auto range = uint32_t(std::max<int64_t>(0, maximum - minimum));
    const auto damage = std::max<int64_t>(0, minimum + (range ? uint32_t(player.combatRandom) % range : 0));
    resolveWeaponHit(enemy, float(damage) / 256.f, player.id, rollAttackElements(weapon.item));
    if (wearEquipment_ && weapon.item) wearEquipment_(weapon.item, false);
}
std::optional<std::pair<bool, float>> Simulation::hostileMissileTarget(const Missile &missile, Vec to) const {
    const auto rule = missileCollisions_.find(missile.missileId);
    if (rule == missileCollisions_.end()) return std::nullopt;
    std::optional<std::pair<bool, float>> hit;
    const auto &p = state_.player;
    auto consider = [&](bool merc, EntityId id, Vec position, int size) {
        if (missile.lastHit == id) return;
        const auto delayed = state_.area.novaHitUntil.find(id);
        if (missile.nextHitDelay && delayed != state_.area.novaHitUntil.end() && delayed->second > state_.time) return;
        if (auto at = missileUnitIntersection(missile.pos, to, rule->second.size, position, size);
            at && (!hit || *at < hit->second)) hit = std::pair{merc, *at};
    };
    if (!p.dead && p.hp > 0) consider(false, p.id, p.pos, 2);
    if (p.hireling.active()) consider(true, p.hireling.id, p.hireling.pos, p.hireling.collisionSize);
    return hit;
}
void Simulation::updateMissiles(float dt) {
    auto &area = state_.area;
    std::vector<Missile> spawned;
    for (auto &m : area.missiles) {
        const int accelerationStep = int(m.age * 5.f + .00001f);
        m.age += dt;
        if (m.groundTargeted) { advanceGroundTargetedMissile(m, dt, spawned); continue; }
        if (m.poisonCloud) { advancePoisonCloud(m, dt); continue; }
        if (m.acceleration != 0 && int(m.age * 5.f + .00001f) > accelerationStep) {
            const auto heading = m.velocity.unit();
            float speed = std::max(0.f, m.velocity.length() + m.acceleration);
            if (speed >= m.maxVelocity) {
                speed = m.maxVelocity;
                m.acceleration = 0;
            }
            m.velocity = heading * speed;
        }
        if (m.hostileElement) { advanceHostileElementMissile(m, dt); continue; }
        if (m.hostile && m.hostileMode == 7) {
            m.remaining -= dt;
            if (!state_.player.dead && m.remaining > 0 &&
                (state_.player.pos - m.pos).length() < m.radius)
                if (auto *owner = findEnemy(m.owner))
                    if (auto web = monsterWeb_ ? monsterWeb_(*owner) : std::nullopt) {
                        state_.player.webSlowRemaining =
                            std::max(state_.player.webSlowRemaining, m.slowDuration);
                        state_.player.webSlowPercent = web->slowPercent;
                        state_.player.webSource = owner->id;
                    }
            auto &merc = state_.player.hireling;
            if (merc.active() && m.remaining > 0 && (merc.pos - m.pos).length() < m.radius)
                if (auto *owner = findEnemy(m.owner))
                    if (auto web = monsterWeb_ ? monsterWeb_(*owner) : std::nullopt) {
                        merc.webSlowRemaining = std::max(merc.webSlowRemaining, m.slowDuration);
                        merc.webSlowPercent = web->slowPercent;
                    }
            continue;
        }
        if (m.behavior == SkillBehavior::ChargedBolt && !m.hostile) {
            const float speed = m.velocity.length();
            float distance = speed * std::min(dt, m.remaining);
            while (distance > 0 && !m.path.empty() && m.remaining > 0) {
                const Vec offset = m.path.front() - m.pos;
                const float segmentLength = std::min(distance, offset.length());
                if (segmentLength < .00001f) { m.path.pop_front(); continue; }
                const Vec heading = offset.unit();
                const Vec nextPoint = m.pos + heading * segmentLength;
                if (!missilePathClear(m.missileId, m.pos, nextPoint)) { m.remaining = 0; break; }
                Enemy *hit = nullptr;
                float first = segmentLength;
                for (auto &enemy : area.enemies) {
                    if (enemy.hp <= 0 || !active(enemy.pos)) continue;
                    const Vec relative = enemy.pos - m.pos;
                    const float along = std::clamp(relative.x * heading.x + relative.y * heading.y, 0.f, segmentLength);
                    if ((enemy.pos - (m.pos + heading * along)).length() < 1.2f && (!hit || along < first)) {
                        hit = &enemy;
                        first = along;
                    }
                }
                m.velocity = heading * speed;
                m.pos = hit ? m.pos + heading * first : nextPoint;
                distance -= segmentLength;
                if (hit) {
                    damageEnemy(*hit, m.damage, m.owner, 0, false, MonsterDamageType::Lightning);
                    if (m.hitOverlayId >= 0)
                        area.effects.push_back({hit->pos, 0, m.hitOverlayDuration,
                                                -1, m.hitOverlayId, hit->id});
                    m.remaining = 0;
                } else if (offset.length() <= segmentLength + .00001f) m.path.pop_front();
            }
            m.remaining = m.path.empty() ? 0 : m.remaining - dt;
            continue;
        }
        auto next = m.pos + m.velocity * dt;
        if (m.hostile) {
            const bool wall = clipMissilePath(m.missileId, m.pos, next);
            const auto struck = m.remaining > dt ? hostileMissileTarget(m, next) : std::nullopt;
            m.pos = struck ? m.pos + (next - m.pos) * struck->second : next;
            m.remaining = wall ? 0 : std::max(0.f, m.remaining - dt);
            if (struck) {
                m.remaining = 0;
                if (auto *source = findEnemy(m.owner)) {
                    if (m.hostileMode >= 3) resolveMonsterSpell(*source, m, struck->first);
                    else resolveMonsterAttack(*source, m.hostileMode, true, struck->first);
                }
            }
            continue;
        }
        if (!m.physical && m.missileId >= 0) {
            const auto type = m.behavior == SkillBehavior::Nova ? MonsterDamageType::Lightning :
                m.chill > 0 ? MonsterDamageType::Cold : MonsterDamageType::Fire;
            const bool wall = !missilePathClear(m.missileId, m.pos, next);
            if (wall) {
                float clear = 0.f, blocked = 1.f;
                for (int step = 0; step < 9; ++step) {
                    const float middle = (clear + blocked) * .5f;
                    if (missilePathClear(m.missileId, m.pos, m.pos + (next - m.pos) * middle)) clear = middle;
                    else blocked = middle;
                }
                next = m.pos + (next - m.pos) * clear;
            }
            if (m.behavior == SkillBehavior::Nova || m.behavior == SkillBehavior::FrostNova || m.behavior == SkillBehavior::Inferno) {
                const Vec motion = next - m.pos;
                const float lengthSquared = motion.x * motion.x + motion.y * motion.y;
                for (auto &enemy : area.enemies) {
                    if (enemy.hp <= 0 || !active(enemy.pos)) continue;
                    const Vec offset = enemy.pos - m.pos;
                    const float projection = lengthSquared > 0 ?
                        std::clamp((offset.x * motion.x + offset.y * motion.y) / lengthSquared, 0.f, 1.f) : 0.f;
                    const Vec closest = m.pos + motion * projection;
                    if (enemy.id == m.lastHit || (enemy.pos - closest).length() >= 1.2f ||
                        (m.nextHitDelay > 0 && state_.time < area.novaHitUntil[enemy.id])) continue;
                    m.lastHit = enemy.id;
                    if (m.nextHitDelay > 0) area.novaHitUntil[enemy.id] = state_.time + m.nextHitDelay;
                    damageEnemy(enemy, m.damage, m.owner, m.chill, false, type);
                    if (m.hitOverlayId >= 0)
                        area.effects.push_back({enemy.pos, 0, m.hitOverlayDuration,
                                                -1, m.hitOverlayId, enemy.id});
                }
                m.pos = next;
                m.remaining = wall ? 0 : m.remaining - dt;
                continue;
            }
            Enemy *struck = nullptr;
            float first = 2.f;
            const Vec motion = next - m.pos;
            const auto collision = missileCollisions_.find(m.missileId);
            if (collision == missileCollisions_.end()) { m.remaining = 0; continue; }
            for (auto &enemy : area.enemies) {
                if (enemy.hp <= 0 || !active(enemy.pos)) continue;
                const int size = monsterSize_ ? monsterSize_(enemy) : 0;
                if (auto at = missileUnitIntersection(m.pos, next, collision->second.size, enemy.pos, size);
                    at && *at < first) {
                    struck = &enemy;
                    first = *at;
                }
            }
            m.pos = struck ? m.pos + motion * first : next;
            m.remaining -= dt;
            if (wall || struck || m.remaining <= 0) {
                m.remaining = 0;
                if (!wall && !struck) continue;
                if (m.impact) resolveMissileImpact(m, spawned, struck);
                else if (struck)
                    damageEnemy(*struck, m.damage, m.owner, m.chill, false, type,
                                false, true, m.behavior == SkillBehavior::IceBlast);
            }
            continue;
        }
        if (m.physical) { advancePhysicalMissile(m, dt, spawned); continue; }
        throw std::logic_error("Missile has no native execution definition");
    }
    std::erase_if(area.missiles, [](const Missile &m) { return m.remaining <= 0; });
    for (auto &missile : spawned) area.missiles.push_back(std::move(missile));
    std::erase_if(area.novaHitUntil, [this](const auto &entry) { return entry.second <= state_.time; });
}
} // namespace d2x
