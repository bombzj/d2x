#pragma once
#include "resources/archive.hpp"
#include "resources/data_table.hpp"
#include <array>
#include <map>
#include <optional>
#include <string>

namespace d2x {
struct SoundRule {
    std::string sound;
    float delay = 0, volume = -1; // -1 uses Sounds.Volume; MonSounds weapon volume overrides it.
    int probability = 100;
    bool interruptible = false;
};
struct MonsterSoundDefinition {
    std::array<SoundRule, 2> attacks, weapons;
    std::array<SoundRule, 4> skills;
    SoundRule hit, death, footstep, footstepLayer, neutral;
    int footstepCount = 0, footstepOffset = 0;
    float neutralInterval = 0;
};
struct PlayerSoundDefinition { SoundRule hit, death; };
struct SkillSoundDefinition { SoundRule start, active; };
// The only MPQ configuration loader for actor sound choices and timing.
// Values have no authority, device, audio handle or network dependency.
class SoundCatalog {
    DataTable sounds_;
    std::map<std::string, size_t, std::less<>> soundRows_;
    std::map<int, MonsterSoundDefinition> monsters_;
    std::map<int, PlayerSoundDefinition> players_;
    std::map<int, SkillSoundDefinition> skills_;
  public:
    explicit SoundCatalog(Archives &);
    const DataTable &sounds() const { return sounds_; }
    std::optional<size_t> soundRow(std::string_view) const;
    const MonsterSoundDefinition *monster(int identity) const;
    const PlayerSoundDefinition *player(int identity) const;
    const SkillSoundDefinition *skill(int identity) const;
};
} // namespace d2x
