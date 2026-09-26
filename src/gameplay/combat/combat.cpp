#include "gameplay/simulation/simulation.hpp"
#include "gameplay/combat/accuracy.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include <algorithm>

namespace d2x {
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
    enemy.hitFlash = 0;
    if (enemy.hp > 0)
        enemy.hitFlash = monsterGetHitDuration_
            ? monsterGetHitDuration_(enemy.identity).value_or(.12f) : .12f;
    if (enemy.hp > 0 && monsterAi_)
        if (auto ai = monsterAi_(enemy); ai && ai->kind == MonsterAiKind::QuillRat)
            enemy.aiRetaliate = true;
    if (enemy.skill2Remaining > 0)
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
    if (enemy.hp > 0) emit(EnemyHit{enemy.id, enemy.kind});
    if (enemy.hp == 0) {
        enemy.freeze = 0;
        enemy.resurrectionRemaining = enemy.resurrectionDuration = 0;
        if (playerKillEffects && source == state_.player.id && !state_.player.dead) {
            auto &player = state_.player;
            player.hp = std::min(float(characterStats_.maxLife), player.hp + characterStats_.combat.lifeOnKill);
            player.mana = std::min(float(characterStats_.maxMana), player.mana + characterStats_.combat.manaOnKill);
        }
        enemy.deathAge = 0;
        enemy.route.clear();
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
        enemy.attackMode = 1;
        ++state_.area.kills;
        emit(EnemyDied{enemy.id, source, enemy.kind, state_.area.region, enemy.pos, enemy.identity,
                       state_.population.difficulty, !playerKillEffects});
    }
}
void Simulation::meleeDamage(Enemy &enemy, bool leftHand) {
    auto &player = state_.player;
    const WeaponDamage *weapon = &equipmentStats_.weapons[player.nextWeapon % equipmentStats_.weaponCount];
    if (leftHand) {
        for (int index = 0; index < equipmentStats_.weaponCount; ++index)
            if (equipmentStats_.weapons[index].leftHand) weapon = &equipmentStats_.weapons[index];
    } else
        player.nextWeapon = (player.nextWeapon + 1) % equipmentStats_.weaponCount;
    player.combatRandom = uint64_t(uint32_t(player.combatRandom)) * 0x6ac690c5ULL +
                          (player.combatRandom >> 32);
    if (monsterDefense_)
        if (auto defense = monsterDefense_(enemy, state_.area.region)) {
            if (uint32_t(player.combatRandom) % 100 >=
                unsigned(physicalHitChance(player.level, characterStats_.attackRating,
                                           defense->level, defense->defense)))
                return;
            player.combatRandom = uint64_t(uint32_t(player.combatRandom)) * 0x6ac690c5ULL +
                                  (player.combatRandom >> 32);
        }
    auto range = uint32_t(weapon->maximum - weapon->minimum) + 1;
    auto damage = weapon->minimum + (range ? uint32_t(player.combatRandom) % range : 0);
    resolveWeaponHit(enemy, float(damage) / 256.f, player.id, rollAttackElements(weapon->item));
    if (wearEquipment_ && weapon->item)
        wearEquipment_(weapon->item, false);
}
void Simulation::damage(Vec pos, float radius, float amount, EntityId source, float chill,
                        MonsterDamageType type) {
    for (auto &e : state_.area.enemies) {
        if (e.hp <= 0 || !active(e.pos) || (e.pos - pos).length() > radius)
            continue;
        damageEnemy(e, amount, source, chill, false, type);
    }
}
void Simulation::updateMissiles(float dt) {
    auto &area = state_.area;
    for (auto &m : area.missiles) {
        const int accelerationStep = int(m.age * 5.f + .00001f);
        m.age += dt;
        if (m.acceleration != 0 && int(m.age * 5.f + .00001f) > accelerationStep) {
            const auto heading = m.velocity.unit();
            float speed = std::max(0.f, m.velocity.length() + m.acceleration);
            if (speed >= m.maxVelocity) {
                speed = m.maxVelocity;
                m.acceleration = 0;
            }
            m.velocity = heading * speed;
        }
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
            continue;
        }
        if (m.skill == Skill::ChargedBolt && !m.hostile) {
            const float speed = m.velocity.length();
            float distance = speed * std::min(dt, m.remaining);
            while (distance > 0 && !m.path.empty() && m.remaining > 0) {
                const Vec offset = m.path.front() - m.pos;
                const float segmentLength = std::min(distance, offset.length());
                if (segmentLength < .00001f) { m.path.pop_front(); continue; }
                const Vec heading = offset.unit();
                const Vec nextPoint = m.pos + heading * segmentLength;
                if (!grid_->segment(m.pos, nextPoint)) { m.remaining = 0; break; }
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
                        area.effects.push_back({hit->pos, m.skill, 0, m.hitOverlayDuration,
                                                -1, m.hitOverlayId, hit->id});
                    m.remaining = 0;
                } else if (offset.length() <= segmentLength + .00001f) m.path.pop_front();
            }
            m.remaining = m.path.empty() ? 0 : m.remaining - dt;
            continue;
        }
        auto next = m.pos + m.velocity * dt;
        if (m.hostile) {
            if (!grid_->segment(m.pos, next)) {
                m.remaining = 0;
                continue;
            }
            const Vec motion = next - m.pos;
            const float lengthSquared = motion.x * motion.x + motion.y * motion.y;
            const Vec offset = state_.player.pos - m.pos;
            const float projection = lengthSquared > 0 ?
                std::clamp((offset.x * motion.x + offset.y * motion.y) / lengthSquared, 0.f, 1.f) : 0.f;
            const Vec closest = m.pos + motion * projection;
            const bool struck = !state_.player.dead &&
                (state_.player.pos - closest).length() < 1.2f;
            m.pos = struck ? closest : next;
            m.remaining -= dt;
            if (struck) {
                m.remaining = 0;
                if (auto *source = findEnemy(m.owner)) {
                    if (m.hostileMode >= 3) resolveMonsterSpell(*source, m);
                    else resolveMonsterAttack(*source, m.hostileMode, true);
                }
            }
            continue;
        }
        if (!m.physical && m.missileId >= 0) {
            const auto type = m.skill == Skill::Nova ? MonsterDamageType::Lightning :
                m.chill > 0 ? MonsterDamageType::Cold : MonsterDamageType::Fire;
            const bool wall = !grid_->segment(m.pos, next);
            if (wall) {
                float clear = 0.f, blocked = 1.f;
                for (int step = 0; step < 9; ++step) {
                    const float middle = (clear + blocked) * .5f;
                    if (grid_->segment(m.pos, m.pos + (next - m.pos) * middle)) clear = middle;
                    else blocked = middle;
                }
                next = m.pos + (next - m.pos) * clear;
            }
            if (m.skill == Skill::Nova || m.skill == Skill::FrostNova || m.skill == Skill::Inferno) {
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
                        area.effects.push_back({enemy.pos, m.skill, 0, m.hitOverlayDuration,
                                                -1, m.hitOverlayId, enemy.id});
                }
                m.pos = next;
                m.remaining = wall ? 0 : m.remaining - dt;
                continue;
            }
            Enemy *struck = nullptr;
            float first = 2.f;
            const Vec motion = next - m.pos;
            const float lengthSquared = motion.x * motion.x + motion.y * motion.y;
            for (auto &enemy : area.enemies) {
                if (enemy.hp <= 0 || !active(enemy.pos)) continue;
                const Vec offset = enemy.pos - m.pos;
                const float projection = lengthSquared > 0 ?
                    std::clamp((offset.x * motion.x + offset.y * motion.y) / lengthSquared, 0.f, 1.f) : 0.f;
                const Vec closest = m.pos + motion * projection;
                if ((enemy.pos - closest).length() < 1.2f && projection < first) {
                    struck = &enemy;
                    first = projection;
                }
            }
            m.pos = struck ? m.pos + motion * first : next;
            m.remaining -= dt;
            if (wall || struck || m.remaining <= 0) {
                m.remaining = 0;
                if (!wall && !struck) continue;
                if (m.impactMissileId >= 0)
                    area.effects.push_back({m.pos, m.skill, 0, m.impactDuration, m.impactMissileId});
                emit(MissileImpact{m.missileId, m.pos});
                if (m.radius > 0) {
                    damage(m.pos, m.radius, m.damage, m.owner, m.chill, type);
                } else if (struck)
                    damageEnemy(*struck, m.damage, m.owner, m.chill, false, type,
                                false, true, m.skill == Skill::IceBlast);
            }
            continue;
        }
        if (m.physical) {
            if (!grid_->segment(m.pos, next)) {
                m.remaining = 0;
                continue;
            }
            Enemy *struck = nullptr;
            float first = 2.f;
            const Vec motion = next - m.pos;
            const float lengthSquared = motion.x * motion.x + motion.y * motion.y;
            for (auto &enemy : area.enemies) {
                if (enemy.hp <= 0 || !active(enemy.pos)) continue;
                const Vec offset = enemy.pos - m.pos;
                const float projection = lengthSquared > 0 ?
                    std::clamp((offset.x * motion.x + offset.y * motion.y) / lengthSquared, 0.f, 1.f) : 0.f;
                const Vec closest = m.pos + motion * projection;
                if ((enemy.pos - closest).length() < 1.2f && projection < first) {
                    struck = &enemy;
                    first = projection;
                }
            }
            m.pos = next;
            m.remaining -= dt;
            if (struck) {
                m.remaining = 0;
                auto &player = state_.player;
                player.combatRandom = uint64_t(uint32_t(player.combatRandom)) * 0x6ac690c5ULL +
                                      (player.combatRandom >> 32);
                bool hit = true;
                if (monsterDefense_)
                    if (auto defense = monsterDefense_(*struck, state_.area.region))
                        hit = uint32_t(player.combatRandom) % 100 <
                            unsigned(physicalHitChance(m.attackerLevel > 0 ? m.attackerLevel : player.level,
                                                       m.attackerLevel > 0 ? m.attackRating :
                                                           characterStats_.attackRating,
                                                       defense->level, defense->defense));
                if (hit) resolveWeaponHit(*struck, m.damage, m.owner, m.attackElements);
            }
            continue;
        }
        bool hit = !grid_->segment(m.pos, next);
        for (const auto &e : area.enemies)
            if (e.hp > 0 && active(e.pos) && (e.pos - next).length() < 1.3f)
                hit = true;
        m.pos = next;
        m.remaining -= dt;
        if (hit) {
            m.remaining = 0;
            const auto &skill = skillDefinition(m.skill);
            damage(m.pos, skill.radius, skill.damage, m.owner, 0, MonsterDamageType::Fire);
            area.effects.push_back({m.pos, m.skill, 0, .55f});
        }
    }
    std::erase_if(area.missiles, [](const Missile &m) { return m.remaining <= 0; });
    std::erase_if(area.novaHitUntil, [this](const auto &entry) { return entry.second <= state_.time; });
}
} // namespace d2x
