#include "scene_assets.hpp"
#include "resources/monster_palshift.hpp"

namespace d2x {
void SceneAssets::indexMonsterArt(const GameSession &session) {
    const auto &content = session.monsterContent();
    std::map<MonsterKind, const MonsterRecord *> baseActors;
    for (const auto &[id, actor] : content.monsters()) {
        auto implementation = monsterImplementation(id);
        if (implementation.substitute) continue;
        const bool base = baseActors.emplace(implementation.kind, &actor).second;
        monsterArtSources[id] = {implementation.kind, &actor, 0, base};
        if (implementation.kind == MonsterKind::NecroSkeleton)
            for (size_t shield = 1; shield < actor.shieldVariants.size(); ++shield)
                monsterArtSources[id + "#sh" + std::to_string(shield)] =
                    {implementation.kind, &actor, shield, false};
    }
    for (int index = 0; index < int(MonsterKind::Count); ++index) {
        const auto kind = MonsterKind(index);
        if (!baseActors.contains(kind))
            throw std::runtime_error("Implemented monster kind has no MPQ actor");
        baseMonsterArt[kind] = {kind, baseActors.at(kind), 0, true};
    }
}
const std::map<std::string, GpuAnimation> &SceneAssets::monsterAnimationSet(
    const GameSession &session, std::string_view monsterClass, MonsterKind kind, int summonShield) const {
    const std::string key = std::string(monsterClass) +
                            (summonShield > 0 ? "#sh" + std::to_string(summonShield) : std::string{});
    auto source = monsterArtSources.find(key);
    if (source == monsterArtSources.end()) source = monsterArtSources.find(monsterClass);
    // Unimplemented classes stay out of the index and keep the shared substitute art.
    if (source == monsterArtSources.end()) {
        auto &target = monsterAnimations[kind];
        if (target.empty()) {
            const auto base = baseMonsterArt.find(kind);
            if (base == baseMonsterArt.end())
                throw std::runtime_error("Monster art set is not indexed: " +
                                         std::string(monsterClass));
            loadMonsterActor(session, base->second, target);
        }
        return target;
    }
    auto &target = source->second.base ? monsterAnimations[source->second.kind]
                                      : monsterVariantAnimations[source->first];
    if (target.empty())
        loadMonsterActor(session, source->second, target);
    return target;
}
void SceneAssets::loadMonsterActor(const GameSession &session, const MonsterArtSource &entry,
                                   std::map<std::string, GpuAnimation> &animations) const {
    const auto &content = session.monsterContent();
    const auto kind = entry.kind;
    const auto &actor = *entry.actor;
    const size_t shield = entry.shield;
    const auto definition = monsterDefinition(kind);
    if (normalize(actor.token) != definition.token)
        throw std::runtime_error("Monster token differs from implemented art: " + actor.id);
    const auto palettePath = "data/global/monsters/" + std::string(definition.token) +
                             "/cof/palshift.dat";
    std::optional<std::array<uint8_t, 256>> colors;
    if (archives_.contains(palettePath))
        colors = monsterPalshift(archives_.read(palettePath), actor.transLevel);
    else if (actor.transLevel != 0)
        throw std::runtime_error("Monster TransLvl requires missing palshift.dat: " + actor.id);
    std::array<const char *, 16> equipment;
    equipment.fill("");
    if (kind == MonsterKind::NecroSkeleton)
        equipment[7] = actor.shieldVariants.at(shield).c_str();
    if (!actor.rightHandVariant.empty()) equipment[5] = actor.rightHandVariant.c_str();
    if (!actor.leftHandVariant.empty()) equipment[6] = actor.leftHandVariant.c_str();
    for (size_t index = 0; index < actor.specialVariants.size(); ++index)
        if (!actor.specialVariants[index].empty())
            equipment[index + 8] = actor.specialVariants[index].c_str();
    if (kind == MonsterKind::Skeleton || kind == MonsterKind::CorruptRogue) {
        equipment[7] = "buc";
        if (!equipment[8][0]) equipment[8] = "lit";
        if (!equipment[9][0]) equipment[9] = "lit";
    }
    for (auto mode : {"nu", "wl", "rn", "a1", "dt", "a2", "sc", "gh", "dd", "s1", "s2"}) {
        if (kind == MonsterKind::FoulCrowNest &&
            (std::string_view(mode) == "wl" || std::string_view(mode) == "a1" ||
             std::string_view(mode) == "gh")) continue;
        if (std::string_view(mode) == "rn" &&
            ((kind != MonsterKind::CorruptRogue && kind != MonsterKind::CorruptLancer &&
              kind != MonsterKind::CorruptArcher) ||
             !actor.runMode)) continue;
        if (std::string_view(mode) == "a2" && !content.attackTiming(kind, 2) &&
            !((kind == MonsterKind::FallenShaman || kind == MonsterKind::Arach) &&
              content.attackTiming(kind, 3))) continue;
        if (std::string_view(mode) == "sc" &&
            (!actor.castMode || !content.attackTiming(kind, 3))) continue;
        if (std::string_view(mode) == "gh" && !actor.getHitMode) continue;
        if (std::string_view(mode) == "dd" && !actor.deadMode) continue;
        if (std::string_view(mode) == "s2" && (kind != MonsterKind::Fallen || !actor.skill2Mode)) continue;
        if (std::string_view(mode) == "s1" && kind != MonsterKind::FoulCrowNest &&
            kind != MonsterKind::Fallen && kind != MonsterKind::NecroSkeleton) continue;
        const auto weapon = content.modeWeapon(kind, mode);
        if (weapon.empty())
            throw std::runtime_error("Monster mode COF missing: " + actor.id + "/" + mode);
        auto modeEquipment = equipment;
        if (kind == MonsterKind::Fallen && std::string_view(mode) == "s1")
            for (size_t index = 2; index < actor.specialVariants.size(); ++index)
                if (actor.specialVariants[index].empty())
                    modeEquipment[index + 8] = "nil";
        auto animation = graphics_.composite("monsters", definition.token, mode,
                                             std::string(weapon), &modeEquipment,
                                             colors ? &*colors : nullptr);
        if (animation.frames.empty() || !animation.completeComposite)
            throw std::runtime_error("Monster animation incomplete: " + actor.id + "/" + mode);
        if (!actor.castsShadow)
            for (auto &frame : animation.frames) frame.shadowTexture = {};
        animations.emplace(mode, std::move(animation));
    }
    if (auto timing = content.attackTiming(kind);
        timing && animations.at("a1").count != timing->frames)
        throw std::runtime_error("Monster AnimData/COF frame mismatch: " + actor.id);
    if ((kind == MonsterKind::CorruptArcher || kind == MonsterKind::SkeletonBow ||
         kind == MonsterKind::SkeletonMage) &&
        actor.attack1Projectile &&
        !content.attackTiming(kind))
        throw std::runtime_error("Monster ranged A1 event missing: " + actor.id);
    if (auto timing = content.attackTiming(kind, 2);
        timing && animations.at("a2").count != timing->frames)
        throw std::runtime_error("Monster A2 AnimData/COF frame mismatch: " + actor.id);
    if (actor.castMode && kind == MonsterKind::Vampire) {
        const auto *timing = content.attackTiming(kind, 3);
        if (!timing || !animations.contains("sc") ||
            animations.at("sc").count != timing->frames ||
            !actor.spells[0] || !actor.spells[3])
            throw std::runtime_error("Original monster SC spell resources missing: " + actor.id);
    }
    if (actor.sequenceMode && kind == MonsterKind::FallenShaman) {
        const auto *timing = content.attackTiming(kind, 3);
        if (!timing || !animations.contains("a2") ||
            animations.at("a2").count != timing->frames ||
            !actor.resurrection || !actor.spells[1])
            throw std::runtime_error("Original Shaman sequence resources missing: " + actor.id);
    }
    if (kind == MonsterKind::FoulCrowNest) {
        const auto *timing = content.attackTiming(kind, 3);
        if (!timing || !animations.contains("s1") ||
            animations.at("s1").count != timing->frames)
            throw std::runtime_error("Original nest sequence resources missing: " + actor.id);
    }
    if (kind == MonsterKind::Arach) {
        const auto *timing = content.attackTiming(kind, 3);
        if (!timing || !animations.contains("a2") ||
            animations.at("a2").count != timing->frames)
            throw std::runtime_error("Original Arach SpiderLay resources missing: " + actor.id);
    }
    if (actor.skill2Mode && kind == MonsterKind::Fallen) {
        const auto *timing = content.motion(kind, "s2");
        if (!timing || animations.at("s2").count != timing->frames)
            throw std::runtime_error("Monster S2 AnimData/COF frame mismatch: " + actor.id);
    }
    if (kind == MonsterKind::Fallen || kind == MonsterKind::NecroSkeleton) {
        const auto *timing = content.motion(kind, "s1");
        if (!timing || !animations.contains("s1") ||
            animations.at("s1").count != timing->frames)
            throw std::runtime_error("Original monster resurrection animation missing: " + actor.id);
    }
    for (auto mode : {"nu", "wl", "rn", "gh", "dt", "dd"})
        if (auto animation = animations.find(mode); animation != animations.end()) {
            auto *timing = content.motion(kind, mode);
            if ((kind == MonsterKind::Brute || kind == MonsterKind::Zombie ||
                 kind == MonsterKind::Skeleton || kind == MonsterKind::CorruptRogue ||
                 kind == MonsterKind::Goatman || kind == MonsterKind::QuillRat ||
                 kind == MonsterKind::Wraith || kind == MonsterKind::CorruptLancer ||
                 kind == MonsterKind::CorruptArcher || kind == MonsterKind::SkeletonBow ||
                 kind == MonsterKind::Bighead || kind == MonsterKind::HellBovine ||
                 kind == MonsterKind::SkeletonMage || kind == MonsterKind::Fetish ||
                 kind == MonsterKind::Vampire || kind == MonsterKind::NecroSkeleton ||
                 kind == MonsterKind::FallenShaman ||
                 kind == MonsterKind::FoulCrowNest ||
                 kind == MonsterKind::BloodHawk ||
                 kind == MonsterKind::Arach) && !timing)
                throw std::runtime_error("Original monster AnimData entry missing: " +
                                         actor.id + "/" + mode);
            if (timing && animation->second.count != timing->frames)
                throw std::runtime_error("Monster AnimData/COF frame mismatch: " +
                                         actor.id + "/" + mode);
        }
}
} // namespace d2x
