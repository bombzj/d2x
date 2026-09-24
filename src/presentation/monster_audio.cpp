#include "scene_assets.hpp"
#include "resources/data_table.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace d2x {
void SceneAssets::loadMonsterAudio(Archives &archives, const MonsterCatalog &monsters) {
    const DataTable monSounds(archives.read("data/global/excel/monsounds.txt"));
    const DataTable sounds(archives.read("data/global/excel/sounds.txt"));
    std::map<std::string, size_t, std::less<>> voiceRows, soundRows;
    for (size_t row = 0; row < monSounds.rows().size(); ++row)
        voiceRows.emplace(monSounds.value(row, "Id"), row);
    for (size_t row = 0; row < sounds.rows().size(); ++row)
        soundRows.emplace(sounds.value(row, "Sound"), row);
    std::set<std::string, std::less<>> registered;
    auto resolve = [&](size_t voice, std::string_view column) -> std::string {
        const std::string name(monSounds.value(voice, column));
        if (name.empty()) return {};
        auto found = soundRows.find(name);
        if (found == soundRows.end())
            throw std::runtime_error("Monster Sounds row is missing: " + name);
        std::string path = "data/global/sfx/" + std::string(sounds.value(found->second, "FileName"));
        std::replace(path.begin(), path.end(), '\\', '/');
        if (!archives.contains(path))
            throw std::runtime_error("Monster original sound file is missing: " + path);
        const std::string key = "monster." + name;
        if (registered.insert(key).second) audio.registerOriginal(archives, key, path);
        return key;
    };
    for (const auto &[id, record] : monsters.monsters()) {
        const auto implementation = monsterImplementation(id);
        if (implementation.substitute || !record.hostile()) continue;
        if (record.sound.empty())
            throw std::runtime_error("Implemented monster MonSound is missing from the mounted MPQ: " + id);
        auto voice = voiceRows.find(record.sound);
        if (voice == voiceRows.end())
            throw std::runtime_error("Implemented monster MonSounds row is missing from the mounted MPQ: " + id);
        MonsterAudio profile;
        profile.attack1 = resolve(voice->second, "Attack1");
        profile.attack2 = resolve(voice->second, "Attack2");
        profile.hit = resolve(voice->second, "HitSound");
        profile.death = resolve(voice->second, "DeathSound");
        profile.footstep = resolve(voice->second, "Footstep");
        profile.neutral = resolve(voice->second, "Neutral");
        const auto *walk = monsters.motion(implementation.kind, "wl");
        const auto steps = monSounds.number(voice->second, "FsCnt");
        const auto neutralTime = monSounds.number(voice->second, "NeuTime");
        if (walk && steps && *steps > 0)
            profile.footstepInterval = walk->duration / *steps;
        if (neutralTime && *neutralTime > 0)
            profile.neutralInterval = float(*neutralTime) / 25.f;
        monsterAudio.emplace(id, std::move(profile));
    }
}
} // namespace d2x
