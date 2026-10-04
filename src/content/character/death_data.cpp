#include "death_data.hpp"
#include "resources/anim_data.hpp"
#include "resources/data_table.hpp"
#include <cctype>
#include <stdexcept>

namespace d2x {
PlayerDeathData loadPlayerDeathData(Archives &archives, std::span<const CharacterDefinition> characters,
                                   const DataTable &difficulties) {
    PlayerDeathData result;
    const AnimDataTable animations(archives.read("data/global/animdata.d2"));
    for (const auto &character : characters) {
        const auto key = character.appearance + "dthth";
        auto upper = key;
        for (auto &letter : upper) letter = char(std::toupper(static_cast<unsigned char>(letter)));
        const auto *animation = animations.find(upper);
        if (!animation || !animation->frames || animation->frames > 144 || animation->speed <= 0 ||
            !archives.contains("data/global/chars/" + character.appearance + "/cof/" + key + ".cof"))
            throw std::runtime_error("Missing original player death timing: " + key);
        result.timings.emplace(key, PlayerDeathData::Timing{int(animation->frames), animation->speed});
    }
    for (size_t difficulty = 0; difficulty < result.experiencePenalty.size(); ++difficulty) {
        const auto penalty = difficulties.number(difficulty, "DeathExpPenalty");
        if (!penalty || *penalty < 0 || *penalty > 100)
            throw std::runtime_error("Missing original death experience penalty");
        result.experiencePenalty[difficulty] = *penalty;
    }
    return result;
}
} // namespace d2x
