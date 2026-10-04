#include "gameplay/skills/spec.hpp"
#include "content/classic_data.hpp"
#include "gameplay/items/inventory.hpp"
#include "gameplay/skills/amazon_summon_spec.hpp"
#include "equipment_appearance.hpp"
#include "gameplay/monsters/implementation.hpp"
#include "gameplay/session/session.hpp"
#include "gameplay/model/state.hpp"
#include "content/monsters/monster_catalog.hpp"
#include "presentation/scene_assets.hpp"
#include "resources/monster_palshift.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x {
void SceneAssets::indexMonsterArt(const GameSession &session) {
    const auto &content = session.monsterContent();
    std::map<MonsterKind, const MonsterRecord *> baseActors;
    for (const auto &[id, actor] : content.monsters()) {
        auto implementation = monsterImplementation(id);
        if (implementation.substitute) continue;
        const bool base = baseActors.emplace(implementation.kind, &actor).second;
        monsterArtSources[id] = {implementation.kind, &actor, 0, base};
        if (implementation.kind == MonsterKind::NecroMage)
            for (size_t element = 1; element < 4; ++element)
                monsterArtSources[id + "#sh" + std::to_string(element)] = {implementation.kind, &actor, element, false};
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
    const GameSession &session, std::string_view monsterClass, MonsterKind kind, int summonShield,
    const MonsterIdentity *identity, const MonsterEnchantment *enchantment) const {
    if (kind == MonsterKind::AmazonPet && identity) {
        const auto found = std::find_if(session.state().companions.begin(), session.state().companions.end(),
            [&](const Enemy &pet) { return pet.identity.spawnKey == identity->spawnKey && pet.amazonPet; });
        if (found == session.state().companions.end()) throw std::runtime_error("Original Amazon summon appearance is unavailable");
        const auto &pet = *found->amazonPet;
        std::string cacheKey = "amazon-pet:" + pet.characterAppearance + ":" + pet.weaponClass + ":" + std::to_string(pet.weaponSet);
        for (const auto &code : pet.appearanceDefinitions) cacheKey += ":" + code;
        auto &result = monsterVariantAnimations[cacheKey];
        if (!result.empty()) return result;
        std::array<std::string, 16> parts;
        parts.fill(session.content().armorTypes.at(0)); parts[5] = parts[6] = parts[7] = "nil";
        if (pet.characterAppearance == "ne") parts[10] = "ne1";
        auto equipment = [&](EquipmentSlot slot) { return session.inventory().catalog().find(pet.appearanceDefinitions[size_t(slot)]); };
        if (const auto *head = equipment(EquipmentSlot::Head); head && !head->appearance.token.empty()) parts[0] = head->appearance.token;
        if (const auto *body = equipment(EquipmentSlot::Torso)) {
            constexpr size_t components[]{3,4,1,2,8,9};
            for (size_t i = 0; i < std::size(components); ++i) parts[components[i]] = body->appearance.body[i];
        }
        for (const bool left : {false, true}) {
            const auto *item = equipment(weaponHandSlot(left, pet.weaponSet));
            if (!item || item->appearance.component == 16) continue;
            const int component = equippedHandComponent(*item, left);
            if (component >= 0 && component < 16) parts[size_t(component)] = item->appearance.token;
        }
        std::array<const char *, 16> pointers;
        for (size_t i = 0; i < parts.size(); ++i) pointers[i] = parts[i].c_str();
        for (const auto mode : {"nu", "wl", "rn", "a1", "gh", "dt", "dd"}) {
            const bool death = std::string_view(mode) == "dt" || std::string_view(mode) == "dd";
            auto animation = death ? graphics_.composite("monsters", "vk", mode, "hth") :
                graphics_.composite("chars", pet.characterAppearance, mode, pet.weaponClass, &pointers);
            if (animation.frames.empty() || !animation.completeComposite)
                throw std::runtime_error("Original Amazon summon animation is incomplete: " + std::string(mode));
            result.emplace(mode, std::move(animation));
        }
        return result;
    }
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
    if (identity && enchantment && identity->rank == MonsterRank::SuperUnique) {
        if (const auto *fixed = session.monsterContent().superUnique(identity->superUnique)) {
            const int palette = fixed->uniqueTrans.at(size_t(session.state().population.difficulty));
            auto &target = monsterVariantAnimations[key + "#fixed" + std::to_string(palette)];
            if (target.empty()) {
                auto art = source->second;
                art.paletteOverride = palette;
                art.fixedPalette = true;
                loadMonsterActor(session, art, target);
            }
            return target;
        }
    }
    if (identity && enchantment &&
        (identity->rank == MonsterRank::Champion || identity->rank == MonsterRank::Unique)) {
        const auto &actor = *source->second.actor;
        auto [choices, inserted] = elitePaletteChoices_.try_emplace(std::string(monsterClass));
        if (inserted) {
            const std::string path = "data/global/monsters/" + normalize(actor.token) + "/cof/palshift.dat";
            if (archives_.contains(path)) {
                const auto data = archives_.read(path);
                if (data.size() % 256 == 0 && actor.transLevel >= 0 &&
                    size_t(actor.transLevel + 2) < data.size() / 256) {
                    const auto normal = data.begin() + (actor.transLevel + 2) * 256;
                    for (size_t index = 2; index < data.size() / 256; ++index) {
                        const auto candidate = data.begin() + index * 256;
                        if (data[index * 256] != 0 || std::equal(candidate, candidate + 256, normal)) continue;
                        const bool duplicate = std::any_of(choices->second.begin(), choices->second.end(), [&](int palette) {
                            return std::equal(candidate, candidate + 256, data.begin() + (palette + 2) * 256);
                        });
                        if (!duplicate) choices->second.push_back(int(index) - 2);
                    }
                }
            }
        }
        if (!choices->second.empty()) {
            uint64_t random = initialRandom(enchantment->nameSeed);
            const int configured = actor.uniqueTrans.at(size_t(session.state().population.difficulty));
            const int palette = configured >= 0 && configured != 255 &&
                std::find(choices->second.begin(), choices->second.end(), configured) != choices->second.end()
                ? configured : choices->second[limitedRandom(random, unsigned(choices->second.size()))];
            auto &target = monsterVariantAnimations[key + "#elite" + std::to_string(palette)];
            if (target.empty()) {
                auto art = source->second;
                art.paletteOverride = palette;
                loadMonsterActor(session, art, target);
            }
            return target;
        }
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
    const bool hydra = actor.ai == "Hydra";
    const size_t shield = entry.shield;
    const auto definition = monsterDefinition(kind);
    if (normalize(actor.token) != definition.token)
        throw std::runtime_error("Monster token differs from implemented art: " + actor.id);
    const auto palettePath = "data/global/monsters/" + std::string(definition.token) +
                             "/cof/palshift.dat";
    std::optional<std::array<uint8_t, 256>> colors;
    if (entry.fixedPalette && entry.paletteOverride >= 2) {
        const auto data = archives_.read("data/global/monsters/randtransforms.dat");
        const auto offset = size_t(entry.paletteOverride - 2) * 256;
        if (data.size() % 256 || offset + 256 > data.size())
            throw std::runtime_error("Invalid MPQ SuperUnique Utrans: " + actor.id);
        colors.emplace();
        std::copy_n(data.begin() + offset, colors->size(), colors->begin());
        if ((*colors)[0] != 0)
            throw std::runtime_error("SuperUnique transform changes transparent index: " + actor.id);
    } else if (archives_.contains(palettePath))
        colors = monsterPalshift(archives_.read(palettePath), entry.paletteOverride >= 0 ? entry.paletteOverride : actor.transLevel);
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
    if (kind == MonsterKind::NecroMage) {
        constexpr const char *elements[]{"pos", "cld", "fir", "lht"};
        equipment[11] = equipment[12] = elements[std::min<size_t>(shield, 3)];
    }
    if (kind == MonsterKind::Skeleton || kind == MonsterKind::CorruptRogue) {
        equipment[7] = "buc";
        if (!equipment[8][0]) equipment[8] = "lit";
        if (!equipment[9][0]) equipment[9] = "lit";
    }
    for (auto mode : {"nu", "wl", "rn", "a1", "dt", "a2", "sc", "gh", "dd", "s1", "s2"}) {
        if (kind == MonsterKind::BoneWall && std::string_view(mode) != "nu" && std::string_view(mode) != "dt" &&
            std::string_view(mode) != "dd" && std::string_view(mode) != "gh" && std::string_view(mode) != "s1") continue;
        if (kind == MonsterKind::PrisonDoor && std::string_view(mode) != "nu" && std::string_view(mode) != "dt" &&
            std::string_view(mode) != "dd" && std::string_view(mode) != "gh") continue;
        if (hydra && std::string_view(mode) != "nu" && std::string_view(mode) != "a1" &&
            std::string_view(mode) != "dt" && std::string_view(mode) != "dd" && std::string_view(mode) != "s2") continue;
        if (kind == MonsterKind::FoulCrowNest &&
            (std::string_view(mode) == "wl" || std::string_view(mode) == "a1" ||
             std::string_view(mode) == "gh")) continue;
        if (std::string_view(mode) == "rn" &&
            ((kind != MonsterKind::CorruptRogue && kind != MonsterKind::CorruptLancer &&
                            kind != MonsterKind::CorruptArcher && kind != MonsterKind::BloodRaven) ||
             !actor.runMode)) continue;
        if (std::string_view(mode) == "a2" && !content.attackTiming(kind, 2) &&
            !((kind == MonsterKind::FallenShaman || kind == MonsterKind::Arach) &&
              content.attackTiming(kind, 3))) continue;
        if (std::string_view(mode) == "sc" &&
            (!actor.castMode || !content.attackTiming(kind, 3))) continue;
        if (std::string_view(mode) == "gh" && !actor.getHitMode) continue;
        if (std::string_view(mode) == "dd" && !actor.deadMode) continue;
        if (std::string_view(mode) == "s2" && ((!hydra && kind != MonsterKind::Fallen) || !actor.skill2Mode)) continue;
        if (std::string_view(mode) == "s1" && kind != MonsterKind::FoulCrowNest &&
            kind != MonsterKind::Fallen && kind != MonsterKind::NecroSkeleton &&
            kind != MonsterKind::ClayGolem && kind != MonsterKind::BloodGolem &&
            kind != MonsterKind::IronGolem && kind != MonsterKind::FireGolem && kind != MonsterKind::NecroMage &&
            kind != MonsterKind::BloodRaven && kind != MonsterKind::BoneWall) continue;
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
    if (hydra) {
        const auto *attack = content.attackTiming(kind);
        const auto *rise = content.motion(kind, "s2");
        const auto *death = content.motion(kind, "dt");
        if (!attack || !rise || !death || !animations.contains("s2") ||
            animations.at("s2").count != rise->frames || animations.at("dt").count != death->frames)
            throw std::runtime_error("Original Hydra lifecycle animation missing: " + actor.id);
    }
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
    if (kind == MonsterKind::Andariel) {
        const auto *spray = content.attackTiming(kind, 3);
        const auto *bolt = content.attackTiming(kind, 4);
        if (!spray || spray->eventTimes.size() != 9 || !bolt || !animations.contains("sc") ||
            !actor.spells[0] || !actor.spells[1])
            throw std::runtime_error("Original Andariel skill actions are incomplete");
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
