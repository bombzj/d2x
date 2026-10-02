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
                             bool freezeHit) {
    if (!ignoreActivation && !active(enemy.pos)) return;
    dealDamage({source, enemy.id, amount, type, chill, alreadyMitigated, true, freezeHit,
                ignoreActivation ? DamagePermission::Debug : DamagePermission::Hostile});
}
void Simulation::onMonsterDamaged(Enemy &enemy, const DamageRequest &request, float dealt) {
    if (dealt <= 0) return;
    const auto source = request.attacker;
    if (enemy.identity.enchantment) {
        const auto &mods = *enemy.identity.enchantment;
        if (enemy.hp == 0 && (mods.has(9) || mods.has(18)))
            enemy.deathEnchantmentFrame = state_.frame + 4;
    }
    if (enemy.hp == 0) {
        const auto target = combatUnit(enemy.id);
        const auto deathKind = target.stats.boss ? EffectUnitKind::Boss : EffectUnitKind::Monster;
        auto keepDeathState = [&](const CombatStateDefinition &state) {
            if (!state.stayOnDeath[size_t(deathKind)]) return;
            enemy.deathHidden |= state.hideOnDeath;
            enemy.deathShattered |= state.shatterOnDeath;
            enemy.deathUnselectable |= state.corpseUnselectable;
        };
        // Native freeze survives ordinary monster death, including a lethal
        // freezing hit; bosses use the distinct bossstaydeath mask.
        if (enemy.freezeActive || enemy.freeze > 0) keepDeathState(freezeDeathState_);
        if (enemy.identity.enchantment && enemy.identity.enchantment->has(35))
            keepDeathState(shatterDeathState_); // MONUMOD_ICESHATTERDEATH.
        enemy.combatEffects.onDeath(deathKind);
        for (const auto &effect : enemy.combatEffects.entries())
            if (effect.activeAt(state_.frame)) keepDeathState(effect.spec.state);
        enemy.hitFlash = 0;
        enemy.freeze = 0;
        enemy.freezeActive = false;
        enemy.resurrectionRemaining = enemy.resurrectionDuration = 0;
        enemy.deathAge = 0;
        enemy.route.clear();
        enemy.approach.reset();
        enemy.aiPursuing = false;
        enemy.aiEscaping = false;
        enemy.aiCommanded = false;
        enemy.aiCircling = false;
        enemy.aiRunning = false;
        enemy.aiRetaliate = false;
        enemy.aiAlerted = false;
        enemy.aiCharged = false;
        enemy.aiAdvanceRemaining = 0;
        enemy.aiPhase = 0;
        if (enemy.kind != MonsterKind::FoulCrowNest) enemy.aiLoop = 0;
        enemy.aiCorpse = {};
        enemy.webAuraRemaining = enemy.webTrailDistance = 0;
        enemy.attack = enemy.attackDuration = 0;
        enemy.attackImpact = -1;
        enemy.teleportTarget.reset();
        enemy.nestSpawnPosition.reset();
        enemy.knockbackRemaining = enemy.knockbackDuration = 0;
        enemy.knockbackDestination.reset();
        enemy.attackMode = 1;
        if (enemy.allegiance.role == CombatRole::Summon) { enemy.corpseConsumed = true; return; }
        ++state_.area.kills;
        const auto controller = combatUnit(controllingPlayer(source));
        const auto attacker = combatUnit(source);
        const auto ownerMods = controller ? controller.stats.attributes.combat : CombatModifiers{};
        const auto mercMods = attacker && attacker.identity.role == CombatRole::Hireling ? attacker.stats.attributes.combat : CombatModifiers{};
        emit(EnemyDied{enemy.id, controllingPlayer(source), enemy.kind, state_.area.region, enemy.pos, enemy.identity,
                       state_.population.difficulty, source, enemy.combatRandom,
                       ownerMods.magicFind + mercMods.magicFind, ownerMods.goldFind + mercMods.goldFind});
    }
}
void Simulation::meleeDamage(EntityId defender, const WeaponDamage &weapon) {
    auto &player = state_.player;
    rollRandom(player.combatRandom);
    const auto target = combatUnit(defender);
    if (!target.alive() || !canAttack(player.id, defender)) return;
    const MonsterDefense defense{target.stats.level, target.stats.attributes.defense,
        target.stats.demon, target.stats.undead, target.stats.boss};
    if (uint32_t(player.combatRandom) % 100 >= unsigned(weaponHitChance(player.level,
        weapon.baseAttackRating, weapon.attackRatingPercent, weapon.target, defense, target.stats.rank))) {
        triggerCombatEffects(defender, CombatEffectEvent::AttackedInMelee, player.id);
        return;
    }
    rollRandom(player.combatRandom);
    const int targetBonus = (defense.demon ? std::max(0, weapon.target.demonDamage) : 0) +
        (defense.undead ? std::max(0, weapon.target.undeadDamage + (weapon.blunt ? 50 : 0)) : 0);
    const int bonus = std::max(-90, weapon.damagePercent + targetBonus);
    const int64_t minimum = weapon.meleeBaseMinimum + int64_t(weapon.meleeBaseMinimum) *
                            (bonus + weapon.minimumDamagePercent) / 100;
    const int64_t maximum = weapon.meleeBaseMaximum + int64_t(weapon.meleeBaseMaximum) *
                            (bonus + weapon.maximumDamagePercent) / 100;
    const auto range = uint32_t(std::max<int64_t>(0, maximum - minimum));
    const auto damage = std::max<int64_t>(0, minimum + (range ? uint32_t(player.combatRandom) % range : 0));
    auto elements = rollAttackElements(weapon.item);
    if (playerFireMastery_ && elements.fire > 0) {
        const int mastery = playerFireMastery_();
        elements.fire = float(int64_t(elements.fire * 256.f) * (100 + mastery) / 100) / 256.f;
    }
    if (player.weaponAttack && player.weaponAttack->skill && player.weaponAttack->skill->effect == SkillBehavior::Vengeance) {
        const auto &skill = *player.weaponAttack->skill;
        const int minimum = std::max(256, weapon.meleeBaseMinimum);
        const int maximum = std::max(minimum + 256, weapon.meleeBaseMaximum);
        const int base = minimum + int(limitedRandom(player.combatRandom, unsigned(maximum - minimum)));
        elements.fire += float(int64_t(base) * skill.weapon->elementPercent[0] / 100) / 256.f;
        elements.cold += float(int64_t(base) * skill.weapon->elementPercent[1] / 100) / 256.f;
        elements.lightning += float(int64_t(base) * skill.weapon->elementPercent[2] / 100) / 256.f;
        elements.coldDuration += skill.coldDuration;
    }
    elements.hitClass = weapon.hitClass;
    if (player.weaponAttack && player.weaponAttack->skill && player.weaponAttack->skill->effect == SkillBehavior::Charge) {
        elements.knockback = true;
        elements.hitClass = 112;
    }
    if (player.weaponAttack && player.weaponAttack->skill && player.weaponAttack->skill->weapon)
        elements.selfDamagePercent = player.weaponAttack->skill->weapon->selfDamagePercent;
    resolveWeaponHit(defender, float(damage) / 256.f, player.id, elements);
    if (wearEquipment_ && weapon.item) wearEquipment_(weapon.item, false);
}
void Simulation::updateMissiles(float dt) {
    auto &area = state_.area;
    std::vector<Missile> spawned;
    updatingMissiles_ = true;
    for (auto &m : area.missiles) {
        if (m.heaven) {
            m.remaining = std::max(0.f, m.remaining - dt);
            if (m.remaining > .00001f) continue;
            m.remaining = 0;
            const auto target = combatUnit(m.heavenTarget);
            if (!target.alive()) continue;
            dealDamage({m.owner, target.id, m.damage, MonsterDamageType::Lightning});
            int count = 0;
            for (auto candidate : combatUnits()) {
                if (!candidate.alive() || !canAttack(m.owner, candidate.id) || !active(*candidate.position)) continue;
                const int deltaX = int(candidate.position->x) - int(m.pos.x), deltaY = int(candidate.position->y) - int(m.pos.y);
                if (deltaX * deltaX + deltaY * deltaY > m.heaven->radius * m.heaven->radius) continue;
                if (count++ >= m.heaven->limit) break;
                Vec heading = (*candidate.position - m.pos).unit();
                if (heading.length() < .001f) {
                    const auto owner = combatUnit(m.owner);
                    heading = owner ? (m.pos - *owner.position).unit() : Vec{1, 0};
                    if (heading.length() < .001f) heading = {1, 0};
                }
                Missile bolt{ids_.allocate(), m.owner, m.pos, heading * (float(m.heaven->boltVelocity * 256 * 75 / 100) * 25.f / 4096.f),
                    float(m.heaven->boltFrames) / 25.f, SkillBehavior::HolyBolt, false, m.heaven->boltId};
                bolt.damage = float(m.heaven->minimum + limitedRandom(m.combatRandom,
                    unsigned(std::max(0, m.heaven->maximum - m.heaven->minimum)))) / 256.f;
                bolt.fixedElement = MonsterDamageType::Magic;
                bolt.healingMinimum = float(m.heaven->healingMinimum); bolt.healingMaximum = float(m.heaven->healingMaximum);
                bolt.combatRandom = childRandom(unitRandom_);
                spawned.push_back(std::move(bolt));
            }
            continue;
        }
        if (m.arc) { advanceArc(m, spawned); continue; }
        if (m.meteor) { advanceMeteor(m, spawned); continue; }
        if (m.firewall) { advanceMonsterFirewall(m, spawned); continue; }
        if (m.frozenOrb) { advanceFrozenOrb(m, spawned); continue; }
        if (m.blizzard) { advanceBlizzard(m, spawned); continue; }
        if (m.freezingArea) { advanceGlacialSpike(m, spawned); continue; }
        if (m.coldRetaliation) { advanceChillingArmorBolt(m, spawned); continue; }
        const int accelerationStep = int(m.age * 5.f + .00001f);
        m.age += dt;
        if (m.groundTargeted) { advanceGroundTargetedMissile(m, dt, spawned); continue; }
        if (m.poisonCloud) { advancePoisonCloud(m, dt, spawned); continue; }
        if (m.acceleration != 0 && int(m.age * 5.f + .00001f) > accelerationStep) {
            float speed = std::max(0.f, m.velocity.length() + m.acceleration);
            if (speed >= m.maxVelocity) { speed = m.maxVelocity; m.acceleration = 0; }
            m.velocity = m.velocity.unit() * speed;
        }
        if (m.monsterAttack && m.monsterAttackMode == 7) {
            m.remaining -= dt;
            if (m.remaining > 0)
                if (auto *owner = findEnemy(m.owner))
                    if (auto web = monsterWeb_ ? monsterWeb_(*owner) : std::nullopt)
                        for (auto target : combatUnits())
                            if (target.alive() && canAttack(m.owner, target.id) && (*target.position - m.pos).length() < m.radius)
                                applyWeb(target.id, m.slowDuration, web->slowPercent);
            continue;
        }
        if (m.physical && !m.monsterAttack) { advancePhysicalMissile(m, dt, spawned); continue; }
        const auto type = m.fixedElement.value_or(
            m.behavior == SkillBehavior::Nova || m.behavior == SkillBehavior::ChargedBolt ? MonsterDamageType::Lightning :
            m.chill > 0 ? MonsterDamageType::Cold : MonsterDamageType::Fire);
        auto hit = [&](EntityId defender) {
            reactToMissile(m, defender, spawned);
            m.lastHit = defender;
            if (m.nextHitDelay > 0) area.novaHitUntil[defender] = state_.time + m.nextHitDelay;
            if (m.monsterAttack && !m.fixedElement) {
                if (auto *source = findEnemy(m.owner)) {
                    if (source->kind == MonsterKind::BloodRaven && m.monsterAttackMode == 4)
                        resolveMonsterAttack(*source, 1, true, defender);
                    else if (m.monsterAttackMode >= 3) resolveMonsterSpell(*source, m, defender);
                    else resolveMonsterAttack(*source, m.monsterAttackMode, true, defender);
                }
            } else if (m.impact) resolveMissileImpact(m, spawned, defender);
            else {
                DamageRequest hit{m.owner, defender, m.damage, type, m.chill};
                if (m.behavior == SkillBehavior::IceBlast) {
                    hit.chill = 0;
                    hit.freezeFrames = int(m.chill * 25.f + .5f);
                }
                dealDamage(hit);
            }
            if (m.hitOverlayId >= 0)
                area.effects.push_back({unitPosition(defender), 0, m.hitOverlayDuration, -1, m.hitOverlayId, defender});
        };
        const bool piercing = !m.killOnHit || m.behavior == SkillBehavior::Nova ||
            m.behavior == SkillBehavior::FrostNova || m.behavior == SkillBehavior::Inferno;
        auto advance = [&](Vec next) {
            const bool wall = clipMissilePath(m.missileId, m.pos, next);
            const auto collision = missileCollisions_.find(m.missileId);
            if (collision == missileCollisions_.end()) { m.remaining = 0; return; }
            if (m.behavior == SkillBehavior::HolyBolt) {
                std::optional<std::pair<EntityId, float>> contact;
                for (auto target : combatUnits()) {
                    if (!target.alive() || target.id == m.owner || !active(*target.position)) continue;
                    const bool ally = relation(m.owner, target.id) == Relation::Allied;
                    if (!ally && (!canAttack(m.owner, target.id) || !target.stats.undead)) continue;
                    const auto intersection = missileUnitIntersection(m.pos, next, collision->second.size,
                        *target.position, target.stats.collisionSize);
                    if (intersection && (!contact || *intersection < contact->second)) contact = {target.id, *intersection};
                }
                m.pos = contact ? m.pos + (next - m.pos) * contact->second : next;
                if (contact) {
                    auto target = combatUnit(contact->first);
                    if (relation(m.owner, target.id) == Relation::Allied) {
                        const int minimum = int(m.healingMinimum * 256.f), maximum = int(m.healingMaximum * 256.f);
                        const float amount = float(minimum + limitedRandom(m.combatRandom,
                            unsigned(std::max(0, maximum - minimum)))) / 256.f;
                        *target.life = std::min(float(target.stats.attributes.maxLife), *target.life + amount);
                        if (m.hitOverlayId >= 0)
                            area.effects.push_back({*target.position, 0, m.hitOverlayDuration, -1, m.hitOverlayId, target.id});
                    } else {
                        const int healingOverlay = m.hitOverlayId;
                        m.hitOverlayId = -1;
                        hit(target.id);
                        m.hitOverlayId = healingOverlay;
                    }
                    m.remaining = 0;
                }
            } else if (piercing) {
                for (auto target : combatUnits()) {
                    if (!target.alive() || !canAttack(m.owner, target.id) || !active(*target.position) || target.id == m.lastHit) continue;
                    if (m.nextHitDelay > 0 && area.novaHitUntil[target.id] > state_.time) continue;
                    if (missileUnitIntersection(m.pos, next, collision->second.size, *target.position, target.stats.collisionSize)) hit(target.id);
                }
                m.pos = next;
            } else {
                const auto contact = missileTarget(m, next);
                m.pos = contact ? m.pos + (next - m.pos) * contact->second : next;
                if (contact) { hit(contact->first); m.remaining = 0; }
                else if (wall && m.impact) resolveMissileImpact(m, spawned);
            }
            if (wall) m.remaining = 0;
        };
        m.remaining = std::max(0.f, m.remaining - dt);
        if (m.remaining <= .00001f) { m.remaining = 0; continue; }
        if (m.behavior == SkillBehavior::ChargedBolt || m.behavior == SkillBehavior::BlessedHammer) {
            float distance = m.velocity.length() * dt;
            while (distance > 0 && !m.path.empty() && m.remaining > 0) {
                const Vec delta = m.path.front() - m.pos;
                const float step = std::min(distance, delta.length());
                if (step < .00001f) { m.path.pop_front(); continue; }
                m.velocity = delta.unit() * m.velocity.length();
                advance(m.pos + delta.unit() * step);
                distance -= step;
                if (step >= delta.length() - .00001f) m.path.pop_front();
            }
            if (m.path.empty()) m.remaining = 0;
        } else advance(m.pos + m.velocity * dt);
    }
    std::erase_if(area.missiles, [](const Missile &m) { return m.remaining <= 0; });
    updatingMissiles_ = false;
    for (auto &missile : spawned) area.missiles.push_back(std::move(missile));
    for (auto &missile : deferredMonsterMissiles_) area.missiles.push_back(std::move(missile));
    deferredMonsterMissiles_.clear();
    std::erase_if(area.novaHitUntil, [this](const auto &entry) { return entry.second <= state_.time; });
}
} // namespace d2x
