#include "scene_assets.hpp"
#include "resources/monster_palshift.hpp"

namespace d2x {
void SceneAssets::loadMonsterAnimations(Archives &archives, const GameSession &session) {
    const auto &content = session.monsterContent();
    auto loadActor = [&](MonsterKind kind, const MonsterRecord &actor,
                         std::map<std::string, GpuAnimation> &animations) {
        const auto &definition = monsterDefinition(kind);
        if (normalize(actor.token) != definition.token)
            throw std::runtime_error("Monster token differs from implemented art: " + actor.id);
        const auto palettePath = "data/global/monsters/" + std::string(definition.token) +
                                 "/cof/palshift.dat";
        const auto colors = monsterPalshift(archives.read(palettePath), actor.transLevel);
        std::array<const char *, 16> equipment;
        equipment.fill("");
        if (!actor.rightHandVariant.empty()) equipment[5] = actor.rightHandVariant.c_str();
        if (kind == MonsterKind::Skeleton || kind == MonsterKind::CorruptRogue) {
            equipment[7] = "buc";
            equipment[8] = "lit";
            equipment[9] = "lit";
        }
        for (auto mode : {"nu", "wl", "rn", "a1", "dt", "a2", "gh", "dd", "s2"}) {
            if (std::string_view(mode) == "rn" &&
                (kind != MonsterKind::CorruptRogue || !actor.runMode)) continue;
            if (std::string_view(mode) == "a2" && !content.attackTiming(kind, 2)) continue;
            if (std::string_view(mode) == "gh" && !actor.getHitMode) continue;
            if (std::string_view(mode) == "dd" && !actor.deadMode) continue;
            if (std::string_view(mode) == "s2" && (kind != MonsterKind::Fallen || !actor.skill2Mode)) continue;
            const auto weapon = content.modeWeapon(kind, mode);
            if (weapon.empty())
                throw std::runtime_error("Monster mode COF missing: " + actor.id + "/" + mode);
            auto animation = graphics_.composite("monsters", definition.token, mode,
                                                 std::string(weapon), &equipment, &colors);
            if (animation.frames.empty() || !animation.completeComposite)
                throw std::runtime_error("Monster animation incomplete: " + actor.id + "/" + mode);
            animations.emplace(mode, std::move(animation));
        }
        if (auto timing = content.attackTiming(kind);
            timing && animations.at("a1").count != timing->frames)
            throw std::runtime_error("Monster AnimData/COF frame mismatch: " + actor.id);
        if (auto timing = content.attackTiming(kind, 2);
            timing && animations.at("a2").count != timing->frames)
            throw std::runtime_error("Monster A2 AnimData/COF frame mismatch: " + actor.id);
        if (actor.skill2Mode && kind == MonsterKind::Fallen) {
            const auto *timing = content.motion(kind, "s2");
            if (!timing || animations.at("s2").count != timing->frames)
                throw std::runtime_error("Monster S2 AnimData/COF frame mismatch: " + actor.id);
        }
        for (auto mode : {"nu", "wl", "rn", "gh", "dt", "dd"})
            if (auto animation = animations.find(mode); animation != animations.end()) {
                auto *timing = content.motion(kind, mode);
                if ((kind == MonsterKind::Brute || kind == MonsterKind::Zombie ||
                     kind == MonsterKind::Skeleton || kind == MonsterKind::CorruptRogue ||
                     kind == MonsterKind::Goatman) && !timing)
                    throw std::runtime_error("Original monster AnimData entry missing: " +
                                             actor.id + "/" + mode);
                if (timing && animation->second.count != timing->frames)
                    throw std::runtime_error("Monster AnimData/COF frame mismatch: " +
                                             actor.id + "/" + mode);
            }
    };
    std::map<MonsterKind, const MonsterRecord *> baseActors;
    for (const auto &[id, actor] : content.monsters()) {
        auto implementation = monsterImplementation(id);
        if (implementation.substitute) continue;
        if (auto [it, inserted] = baseActors.emplace(implementation.kind, &actor); inserted)
            loadActor(implementation.kind, actor, monsterAnimations[implementation.kind]);
        else
            loadActor(implementation.kind, actor, monsterVariantAnimations[id]);
    }
    for (int index = 0; index < int(MonsterKind::Count); ++index)
        if (!baseActors.contains(MonsterKind(index)))
            throw std::runtime_error("Implemented monster kind has no MPQ actor");
}
} // namespace d2x
