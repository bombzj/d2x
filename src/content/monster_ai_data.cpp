#include "monster_ai_data.hpp"
#include <string>

namespace d2x {
std::optional<MonsterAiProfile> loadMonsterAiProfile(const DataTable &stats, size_t row,
                                                     std::string_view ai, int difficulty) {
    if (difficulty < 0 || difficulty > 2) return std::nullopt;
    MonsterAiProfile profile;
    if (ai == "Skeleton") profile.kind = MonsterAiKind::Skeleton;
    else if (ai == "Brute") profile.kind = MonsterAiKind::Brute;
    else if (ai == "Zombie") profile.kind = MonsterAiKind::Zombie;
    else if (ai == "Fallen") profile.kind = MonsterAiKind::Fallen;
    else if (ai == "CorruptRogue") profile.kind = MonsterAiKind::CorruptRogue;
    else if (ai == "Goatman") profile.kind = MonsterAiKind::Goatman;
    else if (ai == "QuillRat") profile.kind = MonsterAiKind::QuillRat;
    else if (ai == "Wraith") profile.kind = MonsterAiKind::Wraith;
    else if (ai == "CorruptLancer") profile.kind = MonsterAiKind::CorruptLancer;
    else if (ai == "CorruptArcher") profile.kind = MonsterAiKind::CorruptArcher;
    else if (ai == "SkeletonBow") profile.kind = MonsterAiKind::SkeletonBow;
    else if (ai == "Bighead") profile.kind = MonsterAiKind::Bighead;
    else if (ai == "SkeletonMage") profile.kind = MonsterAiKind::SkeletonMage;
    else if (ai == "Fetish") profile.kind = MonsterAiKind::Fetish;
    else if (ai == "Vampire") profile.kind = MonsterAiKind::Vampire;
    else if (ai == "FallenShaman") profile.kind = MonsterAiKind::FallenShaman;
    else if (ai == "FoulCrowNest") profile.kind = MonsterAiKind::FoulCrowNest;
    else if (ai == "BloodHawk") profile.kind = MonsterAiKind::BloodHawk;
    else return std::nullopt;
    const std::string suffix = difficulty == 0 ? "" : difficulty == 1 ? "(N)" : "(H)";
    for (int index = 0; index < 8; ++index) {
        auto value = stats.number(row, "aip" + std::to_string(index + 1) + suffix);
        if (value && (*value < 0 || *value > 65535)) return std::nullopt;
        profile.params[index] = value.value_or(0);
    }
    auto percentage = [&](int index) { return profile.params[index] <= 100; };
    if (profile.kind == MonsterAiKind::Skeleton &&
        (!percentage(0) || !percentage(2) || !percentage(3))) return std::nullopt;
    if (profile.kind == MonsterAiKind::Brute &&
        (!percentage(2) || !percentage(3))) return std::nullopt;
    if (profile.kind == MonsterAiKind::Zombie &&
        (!percentage(0) || !percentage(3))) return std::nullopt;
    if (profile.kind == MonsterAiKind::Fallen &&
        (!percentage(0) || !percentage(2) || !percentage(3))) return std::nullopt;
    if (profile.kind == MonsterAiKind::CorruptRogue &&
        (!percentage(0) || !percentage(2) || !percentage(4) || profile.params[3] > 126))
        return std::nullopt;
    if (profile.kind == MonsterAiKind::Goatman &&
        (!percentage(0) || !percentage(2))) return std::nullopt;
    if (profile.kind == MonsterAiKind::QuillRat &&
        (!percentage(1) || profile.params[0] > 255 || profile.params[3] > 255))
        return std::nullopt;
    if (profile.kind == MonsterAiKind::Wraith &&
        (!percentage(0) || !percentage(2))) return std::nullopt;
    if (profile.kind == MonsterAiKind::CorruptLancer &&
        (!percentage(0) || !percentage(1) || !percentage(3) ||
         !percentage(5) || !percentage(6) || !percentage(7) || profile.params[4] > 255))
        return std::nullopt;
    if (profile.kind == MonsterAiKind::CorruptArcher &&
        (!percentage(0) || !percentage(1) || !percentage(3) ||
         !percentage(5) || !percentage(6) || profile.params[4] > 255 ||
         profile.params[7] > 255)) return std::nullopt;
    if (profile.kind == MonsterAiKind::SkeletonBow &&
        (!percentage(0) || !percentage(2) || profile.params[1] > 255 ||
         profile.params[3] > 255 || profile.params[4] > 255)) return std::nullopt;
    if (profile.kind == MonsterAiKind::Bighead &&
        (!percentage(0) || !percentage(1) || !percentage(2) || !percentage(3)))
        return std::nullopt;
    if (profile.kind == MonsterAiKind::SkeletonMage &&
        (!percentage(0) || !percentage(2) || !percentage(4) || !percentage(6) ||
         profile.params[1] > 255 || profile.params[3] > 255 ||
         profile.params[5] > 255 || profile.params[7] > 255)) return std::nullopt;
    if (profile.kind == MonsterAiKind::Fetish &&
        (!percentage(0) || !percentage(3) || profile.params[1] > 255 ||
         profile.params[2] > 255)) return std::nullopt;
    if (profile.kind == MonsterAiKind::Vampire &&
        (!percentage(0) || !percentage(1) || !percentage(3) ||
         profile.params[2] > 255 || profile.params[4] > 7)) return std::nullopt;
    if (profile.kind == MonsterAiKind::FallenShaman &&
        (!percentage(0) || !percentage(1) || !percentage(2) ||
         profile.params[3] > 255 || profile.params[4] > 255)) return std::nullopt;
    if (profile.kind == MonsterAiKind::FoulCrowNest &&
        (profile.params[0] > 2500 || profile.params[2] > 100)) return std::nullopt;
    if (profile.kind == MonsterAiKind::BloodHawk &&
        (!percentage(0) || !percentage(1) || !percentage(2) ||
         profile.params[3] > 255 || profile.params[4] > 255)) return std::nullopt;
    return profile;
}
} // namespace d2x
