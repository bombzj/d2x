#include "save_codec.hpp"
#include "d2s_header.hpp"
#include "d2s_fixed_sections.hpp"
#include "d2s_inventory.hpp"
#include "content/world/world_catalog.hpp"
#include "d2s_skills.hpp"
#include "d2s_quests.hpp"
#include "gameplay/quest/acts/act_three_state.hpp"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace d2x {
namespace {
constexpr size_t fixedEnd = d2sHeaderSize + 298 + 80 + 52;
constexpr std::array<const char *, 7> classes{"Amazon", "Sorceress", "Necromancer", "Paladin", "Barbarian", "Druid", "Assassin"};
void require(bool condition, const std::string &reason) {
    if (!condition) throw std::runtime_error("D2S: " + reason);
}
uint16_t word(std::span<const uint8_t> bytes, size_t at) {
    require(at + 2 <= bytes.size(), "truncated section");
    return uint16_t(bytes[at] | unsigned(bytes[at + 1]) << 8);
}
uint32_t dword(std::span<const uint8_t> bytes, size_t at) {
    return uint32_t(word(bytes, at)) | uint32_t(word(bytes, at + 2)) << 16;
}
void putWord(std::span<uint8_t> bytes, size_t at, unsigned value) {
    bytes[at] = uint8_t(value); bytes[at + 1] = uint8_t(value >> 8);
}
void appendWord(Bytes &bytes, unsigned value) {
    bytes.push_back(uint8_t(value)); bytes.push_back(uint8_t(value >> 8));
}
void appendDword(Bytes &bytes, uint32_t value) {
    appendWord(bytes, value & 0xFFFF); appendWord(bytes, value >> 16);
}
const CharacterDefinition &characterDefinition(const ClassicData &content, const std::string &name) {
    auto found = std::find_if(content.characters.begin(), content.characters.end(),
        [&](const auto &entry) { return entry.name == name; });
    require(found != content.characters.end(), "unknown character class");
    return *found;
}
void waypoints(CharacterSaveData &snapshot, D2sFixedSections &sections, const ClassicData &content, bool writing) {
    const auto &levels = content.tables.at("levels");
    const auto offset = 8 + size_t(snapshot.difficulty) * 24 + 2;
    for (size_t row = 0; row < levels.rows().size(); ++row) {
        const auto id = levels.number(row, "Id"), waypoint = levels.number(row, "Waypoint");
        if (!id || !waypoint || *waypoint < 0 || *waypoint >= 39) continue;
        auto &value = sections.waypoints[offset + size_t(*waypoint) / 8];
        const auto bit = uint8_t(1u << (*waypoint % 8));
        if (writing) {
            if (snapshot.waypoints.contains(RegionId(*id))) value |= bit;
        } else if ((value & bit) || *waypoint == 0) {
            require(*id >= 1 && *id < 137, "unknown waypoint level");
            // WAYPOINTS_CopyAndValidateWaypointData restores the mandatory
            // first bit. Report deficient older saves; never rewrite on read.
            if (*waypoint == 0 && !(value & bit))
                std::cerr << "D2X: waypoint zero was inactive in the save; original mandatory activation restored.\n";
            snapshot.waypoints.emplace(RegionId(*id), -1.f);
        }
    }
}
std::string mercName(const HirelingDefinition &definition, unsigned offset) {
    const auto begin = definition.nameFirst.find_last_not_of("0123456789") + 1;
    require(begin < definition.nameFirst.size(), "hireling name range");
    const auto number = std::stoul(definition.nameFirst.substr(begin)) + offset;
    require(number <= std::stoul(definition.nameLast.substr(begin)), "hireling name index");
    std::ostringstream name;
    name << definition.nameFirst.substr(0, begin) << std::setw(int(definition.nameFirst.size() - begin)) << std::setfill('0') << number;
    return name.str();
}
void importMerc(CharacterRecord &player, const D2sHeader &header, const ClassicData &content) {
    if (!header.mercSeed) return;
    const HirelingDefinition *definition = nullptr;
    int level = 1;
    for (const auto &entry : content.hirelings) {
        if (entry.id != header.mercType || entry.version != 100) continue;
        int candidate = 1;
        while (candidate < 99 && deriveHirelingStats(entry, candidate + 1).experience <= header.mercExperience) ++candidate;
        if (definition && (entry.level > candidate || definition->level > entry.level)) continue;
        definition = &entry;
        level = candidate;
    }
    require(definition != nullptr, "unsupported hireling type");
    auto &merc = player.hireling;
    merc.seed = header.mercSeed;
    merc.sourceRow = definition->sourceRow; merc.classId = definition->classId;
    merc.nameKey = mercName(*definition, header.mercName);
    merc.level = level; merc.experience = header.mercExperience;
    merc.hp = header.mercFlags & 0x10000 ? 0.f : float(deriveHirelingStats(*definition, level).life);
}
void exportMerc(D2sHeader &header, const CharacterRecord &player, const ClassicData &content) {
    const auto &merc = player.hireling;
    if (merc.sourceRow < 0) { header.mercSeed = 0; return; }
    auto definition = std::find_if(content.hirelings.begin(), content.hirelings.end(),
        [&](const auto &entry) { return entry.sourceRow == merc.sourceRow; });
    require(definition != content.hirelings.end() && merc.experience <= UINT32_MAX, "hireling metadata");
    require(merc.seed != 0, "missing hireling seed");
    header.mercSeed = merc.seed;
    header.mercType = uint16_t(definition->id);
    bool found = false;
    for (unsigned index = 0; index < 256; ++index) {
        try { if (mercName(*definition, index) == merc.nameKey) { header.mercName = uint16_t(index); found = true; break; } }
        catch (const std::runtime_error &) { break; }
    }
    require(found, "hireling name");
    header.mercFlags = merc.hp <= 0 ? 0x10000 : 0;
    header.mercExperience = uint32_t(merc.experience);
}
void verifyCharacter(const CharacterSaveData &snapshot, const ClassicData &content) {
    const auto &player = snapshot.player;
    const auto &definition = characterDefinition(content, player.characterClass);
    require(snapshot.corpses.size() <= 1, "single-player corpse count");
    for (const auto &corpse : snapshot.corpses) {
        const auto storage = snapshot.inventory.containers.find(corpse.items);
        require(corpse.id && corpse.owner == player.id && storage != snapshot.inventory.containers.end() &&
            storage->second.spec.kind == ContainerKind::Corpse && storage->second.spec.owner == player.id &&
            storage->second.spec.columns == int(EquipmentSlot::Count) + 1 && storage->second.spec.rows == 1 &&
            std::isfinite(corpse.position.x) && std::isfinite(corpse.position.y) &&
            corpse.position.x >= 0 && corpse.position.y >= 0 &&
            double(corpse.position.x) <= UINT32_MAX && double(corpse.position.y) <= UINT32_MAX,
            "corpse metadata");
    }
    require(snapshot.difficulty >= 0 && snapshot.difficulty <= 2,
        "invalid difficulty");
    require(player.weaponSet < 2 && (content.stashLayout.expansion || !player.weaponSet),
        "invalid weapon set");
    require(player.level >= 1 && player.level <= 99 && player.allocated.strength >= 0 && player.allocated.dexterity >= 0 &&
        player.allocated.energy >= 0 && player.allocated.vitality >= 0 && player.unspentAttributes >= 0 &&
        allocatedPoints(player.allocated) + player.unspentAttributes == int64_t(player.level - 1) * definition.statPerLevel + questAttributePoints(player.quests),
        "unsupported character stat allocation or quest stat rewards");
    int points = player.unspentSkills;
    for (const auto &[id, rank] : player.skillRanks) {
        const auto *skill = content.skills.find(id);
        require(skill && skill->classCode == definition.code && rank > 0 && rank <= skill->maximumRank &&
            player.level >= skill->requiredLevel, "skill rank/class");
        points += rank;
        for (int prerequisite : skill->prerequisites)
            require(player.skillRanks.contains(prerequisite), "skill prerequisite missing");
    }
    for (size_t index = 0; index < player.selectedSkills.size(); ++index) {
        const auto id = player.selectedSkills[index];
        const auto *skill = content.skills.find(id);
        require(id == -1 || (skill && !skill->passive && (index % 2 || skill->leftAllowed)), "invalid mouse skill");
    }
    for (const auto &key : player.skillHotkeys) {
        const auto *skill = content.skills.find(key.skill);
        require(key.skill == -1 || key.skill == -2 ||
            (skill && !skill->passive && (key.right || skill->leftAllowed)), "invalid hotkey skill");
    }
    const int rewards = validateD2sQuestRecords(player.quests);
    require(player.unspentSkills >= 0 && points == player.level - 1 + rewards, "unsupported skill rewards or allocation");
    const auto &thresholds = content.experienceByClass.at(player.characterClass);
    require(size_t(player.level) < thresholds.size() && player.experience >= thresholds[player.level] &&
        (player.level == 99 || player.experience < thresholds[player.level + 1]), "experience/level mismatch");
    require(player.gold <= unsigned(player.level) * 10000 && player.bankGold <= (player.level <= 30
                ? 50000u * (unsigned(player.level) / 10u + 1u)
                : 50000u * (unsigned(player.level) / 2u + 1u)), "gold limit");
}
} // namespace
CharacterSaveData decodeSave(std::span<const uint8_t> bytes, const ClassicData &content) {
    require(bytes.size() <= maxSaveBytes, "file too large");
    auto header = readD2sHeader(bytes);
    require(!(header.flags & (4 | 8 | 0x40)), "hardcore, dead or ladder character unsupported");
    auto sections = readD2sFixedSections(bytes);
    CharacterSaveData snapshot;
    initializeD2sInventory(snapshot, content);
    auto &player = snapshot.player;
    player.name = header.name; player.characterClass = classes[header.characterClass]; player.level = header.level;
    player.weaponSet = header.weaponSet;
    player.nativeSaveSections.assign(reinterpret_cast<const char *>(bytes.data()), fixedEnd);
    snapshot.mapSeed = header.mapSeed;
    snapshot.difficulty = header.difficulty;
    for (unsigned difficulty = 0; difficulty < 3; ++difficulty)
        if (header.towns[difficulty] & 0x80) snapshot.difficulty = int(difficulty);
    const auto act = header.towns[size_t(snapshot.difficulty)] & 7;
    require(unsigned(act) < actTownLevels.size(), "last act is not supported");
    snapshot.lastRegion = RegionId(actTownLevels[act]);
    size_t cursor = fixedEnd;
    const auto stats = readD2sStats(bytes.subspan(cursor), content.tables.at("itemstatcost"));
    cursor += stats.bytesRead;
    std::map<unsigned, int64_t> values;
    for (const auto &stat : stats.values) {
        require(stat.id < 16 && !stat.parameter && stat.value >= 0 && values.emplace(stat.id, stat.value).second,
                "unsupported or duplicate character stat");
    }
    const auto &definition = characterDefinition(content, player.characterClass);
    require(values[12] == header.level, "header/stat level mismatch");
    player.allocated = {int(values[0]) - definition.strength, int(values[2]) - definition.dexterity,
                        int(values[3]) - definition.vitality, int(values[1]) - definition.energy};
    player.unspentAttributes = int(values[4]); player.unspentSkills = int(values[5]);
    player.hp = float(values[6]) / 256.f; player.mana = float(values[8]) / 256.f; player.stamina = float(values[10]) / 256.f;
    player.experience = uint64_t(values[13]); player.gold = unsigned(values[14]); player.bankGold = unsigned(values[15]);
    auto skills = readD2sSkills(bytes.subspan(cursor), header.characterClass, header.skillCount, content.tables.at("skills"));
    cursor += skills.bytesRead;
    for (const auto &[id, rank] : skills.ranks) player.skillRanks.emplace(id, int(rank));
    for (size_t index = 0; index < player.skillHotkeys.size(); ++index) {
        const auto key = header.hotkeys[index];
        if ((key & 0xFFFF) == 0xFFFF) continue;
        const auto skill = int(key & 0x0FFF);
        player.skillHotkeys[index] = {skill == 0 ? -1 : skill, (key & 0x8000) == 0, key>>16};
    }
    importD2sQuests(player, sections, content.npcDialogues);
    for (size_t index = 0; index < player.selectedSkills.size(); ++index) {
        const auto selected = header.selectedSkills[index];
        const auto skill=selected&0xFFFF;
        player.selectedSkills[index] = skill == 0 || skill == 0xFFFF ? -1 : int(skill);
        player.selectedSkillOwners[index]=selected>>16;
    }
    importMerc(player, header, content);
    waypoints(snapshot, sections, content, false);
    std::vector<EntityId> itemSources;
    auto items = [&](bool hireling, EntityId corpse = EntityId{}) {
        require(word(bytes, cursor) == 0x4D4A, "missing inventory JM section");
        const auto count = word(bytes, cursor + 2);
        cursor += 4;
        require(count <= 1024, "item count exceeds supported capacity");
        for (unsigned index = 0; index < count; ++index) {
            auto item = readD2sItem(bytes.subspan(cursor), content);
            cursor += item.bytesRead;
            if(!hireling && !corpse) itemSources.push_back(EntityId{snapshot.nextEntityId});
            importD2sItem(snapshot, item.item, content, hireling, corpse);
        }
    };
    items(false);
    // PlrSave2 stores one-based indices of the main inventory roots, never
    // runtime GUIDs. Nested socket entries do not advance this source list.
    const auto sourceGuid=[&](uint32_t slot)->uint32_t {
        if(!slot || slot==0xFFFF || slot==UINT32_MAX) return UINT32_MAX;
        require(slot<=itemSources.size(),"skill source item index exceeds inventory");
        return uint32_t(itemSources.at(slot-1).value);
    };
    for(auto &key:player.skillHotkeys) key.owner=sourceGuid(key.owner);
    for(auto &owner:player.selectedSkillOwners) owner=sourceGuid(owner);
    require(word(bytes, cursor) == 0x4D4A, "missing corpse JM section");
    const auto corpseCount = word(bytes, cursor + 2);
    require(corpseCount <= 1, "unsupported corpse count");
    cursor += 4;
    if (corpseCount) {
        PlayerCorpse corpse;
        corpse.id = EntityId{snapshot.nextEntityId++}; corpse.owner = player.id;
        corpse.items = EntityId{snapshot.nextEntityId++}; corpse.region = snapshot.lastRegion;
        corpse.nativeUnknown = dword(bytes, cursor);
        corpse.position = {float(dword(bytes, cursor + 4)), float(dword(bytes, cursor + 8))};
        cursor += 12;
        snapshot.inventory.containers.emplace(corpse.items,
            ContainerState{corpse.items, {player.id, ContainerKind::Corpse, int(EquipmentSlot::Count) + 1, 1}});
        snapshot.corpses.push_back(corpse);
        items(false, corpse.items);
    }
    require(word(bytes, cursor) == 0x666A, "missing hireling section"); cursor += 2;
    if (header.mercSeed) items(true);
    require(word(bytes, cursor) == 0x666B && cursor + 3 <= bytes.size(), "missing Iron Golem kf section");
    const unsigned hasIron = bytes[cursor+2]; require(hasIron <= 1, "invalid Iron Golem flag"); cursor += 3;
    if (hasIron) {
        auto encoded = readD2sItem(bytes.subspan(cursor), content); cursor += encoded.bytesRead;
        CharacterSaveData temporary; initializeD2sInventory(temporary, content);
        temporary.nextEntityId = snapshot.nextEntityId; temporary.player.level = player.level;
        encoded.item.mode = 4; encoded.item.page = 1;
        importD2sItem(temporary, encoded.item, content, false);
        require(temporary.inventory.items.size() == 1, "invalid Iron Golem manufacturing item");
        snapshot.ironGolem = temporary.inventory.items.begin()->second;
        const auto *metal = content.items.find(snapshot.ironGolem->definition);
        require(metal && snapshot.ironGolem->identified && metal->equipment.known &&
            (content.tables.at(metal->base.sourceTable).number(metal->base.sourceRow,"bitfield1").value_or(0)&2) != 0,
            "invalid native Iron Golem metal item");
        snapshot.nextEntityId = temporary.nextEntityId;
    }
    require(cursor == bytes.size(), "unexpected data after Iron Golem section");
    for (const auto &[id, item] : snapshot.inventory.items)
        reconcileD2sQuestItem(player, snapshot.difficulty, item.definition, item.nativeQuestDifficulty);
    verifyCharacter(snapshot, content);
    return snapshot;
}
Bytes encodeSave(const CharacterSaveData &source, const ClassicData &content) {
    verifyCharacter(source, content);
    auto snapshot = source;
    const auto &player = snapshot.player;
    auto found = std::find(classes.begin(), classes.end(), player.characterClass);
    require(found != classes.end(), "unknown character class");
    Bytes bytes(d2sHeaderSize, 0);
    D2sHeader header;
    D2sFixedSections sections;
    if (!player.nativeSaveSections.empty()) {
        require(player.nativeSaveSections.size() == fixedEnd, "invalid native metadata");
        Bytes original(player.nativeSaveSections.begin(), player.nativeSaveSections.end());
        std::copy_n(original.begin(), d2sHeaderSize, bytes.begin());
        sections = readD2sFixedSections(original);
        header.flags = dword(original, 0x24);
        header.created = dword(original, 0x2C);
        header.mercSeed = dword(original, 0xB3);
        for (size_t index = 0; index < 16; ++index) {
            header.hotkeys[index] = dword(original, 0x38 + index * 4);
            header.appearance[index] = original[0x88 + index];
            header.colors[index] = original[0x98 + index];
        }
        for (size_t index = 0; index < 4; ++index) header.selectedSkills[index] = dword(original, 0x78 + index * 4);
        std::copy_n(original.begin() + 0xA8, 3, header.towns.begin());
    } else {
        sections.quests[0] = 'W'; sections.quests[1] = 'o'; sections.quests[2] = 'o'; sections.quests[3] = '!';
        sections.quests[4] = 6; putWord(sections.quests, 8, 298);
        sections.waypoints[0] = 'W'; sections.waypoints[1] = 'S'; sections.waypoints[2] = 1;
        putWord(sections.waypoints, 6, 80);
        for (size_t difficulty = 0; difficulty < 3; ++difficulty) {
            putWord(sections.waypoints, 8 + difficulty * 24, 0x102);
            sections.waypoints[10 + difficulty * 24] |= 1;
        }
        sections.introductions[0] = 1; sections.introductions[1] = 0x77; sections.introductions[2] = 52;
        header.hotkeys.fill(UINT32_MAX); header.appearance.fill(0xFF); header.colors.fill(0xFF);
    }
    header.name = player.name; header.characterClass = uint8_t(found - classes.begin());
    header.weaponSet = player.weaponSet; header.level = uint8_t(player.level);
    header.mapSeed = snapshot.mapSeed; header.difficulty = uint8_t(snapshot.difficulty);
    header.saved = uint32_t(std::time(nullptr)); if (!header.created) header.created = header.saved;
    for (auto &town : header.towns) town &= 0x7F;
    unsigned currentAct = 0;
    const auto &levels = content.tables.at("levels");
    for (size_t row = 0; row < levels.rows().size(); ++row)
        if (levels.number(row, "Id") == int(snapshot.lastRegion)) {
            currentAct = unsigned(levels.number(row, "Act").value_or(0));
            break;
        }
    require(currentAct < actTownLevels.size(), "unknown saved act");
    header.towns[header.difficulty] = uint8_t(0x80 | currentAct);
    header.lastLevel = unsigned(snapshot.lastRegion); header.lastTown = unsigned(actTownLevels[currentAct]);
    // CLIENTS_UpdateCharacterProgression: five acts per expansion difficulty.
    // Keep any existing higher native progression rather than downgrading it.
    unsigned progression = (header.flags >> 8) & 31u;
    for (size_t difficulty = 0; difficulty < player.quests.size(); ++difficulty)
        for (const auto &[id, act] : {std::pair{QuestId::SistersToTheSlaughter, 1u}, std::pair{QuestId::SevenTombs, 2u},
             std::pair{QuestId::Guardian, 3u}, std::pair{QuestId::TerrorsEnd, 4u}, std::pair{QuestId::EveOfDestruction, 5u}})
            if (player.quests[difficulty][questIndex(id)].stage >= questCompletionStage(id)) progression = std::max(progression, unsigned(difficulty) * 5 + act);
    header.flags = (header.flags & ~0x1F00u) | (progression << 8);
    std::vector<const ItemInstance *> characterRoots;
    std::map<uint32_t,unsigned> sourceSlots;
    for(const auto &[id,item]:snapshot.inventory.items) if(const auto *at=std::get_if<ContainerLocation>(&item.location);
        at && at->container!=snapshot.containers.hirelingEquipment && snapshot.inventory.containers.at(at->container).spec.kind!=ContainerKind::Corpse) {
        characterRoots.push_back(&item);sourceSlots.emplace(uint32_t(id.value),unsigned(characterRoots.size()));
    }
    require(characterRoots.size()<=1024,"too many character items");
    const auto sourceSlot=[&](uint32_t guid)->uint32_t {const auto found=sourceSlots.find(guid);return found==sourceSlots.end()?0:found->second;};
    for (size_t index = 0; index < player.skillHotkeys.size(); ++index) {
        const auto &key = player.skillHotkeys[index];
        header.hotkeys[index] = key.skill == -2 ? UINT32_MAX
            : uint32_t(std::max(0, key.skill)) | (key.right ? 0 : 0x8000) | (sourceSlot(key.owner)<<16);
    }
    exportMerc(header, player, content);
    for (size_t index = 0; index < player.selectedSkills.size(); ++index)
        header.selectedSkills[index] = uint32_t(std::max(0, player.selectedSkills[index])) | (sourceSlot(player.selectedSkillOwners[index])<<16);
    exportD2sQuests(player, sections, content.npcDialogues);
    waypoints(snapshot, sections, content, true);
    writeD2sFixedSections(bytes, sections);
    const auto &definition = characterDefinition(content, player.characterClass);
    CharacterModifiers questModifiers;
    questModifiers.baseLife = questBaseLife(player.quests);
    const auto base = deriveCharacterAttributes(definition, player.level, player.allocated, questModifiers);
    auto fixed = [](float value) -> int64_t {
        require(std::isfinite(value) && value >= 0 && value < float(UINT32_MAX / 256), "resource value");
        return int64_t(value * 256.f);
    };
    std::vector<D2sStat> stats{{0, base.strength}, {1, base.energy}, {2, base.dexterity}, {3, base.vitality},
        {4, player.unspentAttributes}, {5, player.unspentSkills}, {6, fixed(player.hp)}, {7, int64_t(base.maxLife) * 256},
        {8, fixed(player.mana)}, {9, int64_t(base.maxMana) * 256}, {10, fixed(player.stamina)}, {11, int64_t(base.maxStamina) * 256},
        {12, player.level}, {13, int64_t(player.experience)}, {14, player.gold}, {15, player.bankGold}};
    writeD2sStats(bytes, stats, content.tables.at("itemstatcost"));
    std::map<int, uint8_t> ranks;
    for (const auto &[id, rank] : player.skillRanks) ranks.emplace(id, uint8_t(rank));
    writeD2sSkills(bytes, header.characterClass, header.skillCount, ranks, content.tables.at("skills"));
    auto items = [&](bool hireling, EntityId corpse = EntityId{}) {
        std::vector<const ItemInstance *> entries;
        if(!hireling && !corpse) entries=characterRoots;
        else for (const auto &[id, item] : snapshot.inventory.items)
            if (const auto *location = std::get_if<ContainerLocation>(&item.location);
                location && (corpse ? location->container == corpse :
                    snapshot.inventory.containers.at(location->container).spec.kind != ContainerKind::Corpse &&
                    (location->container == snapshot.containers.hirelingEquipment) == hireling))
                entries.push_back(&item);
        require(entries.size() <= 1024, "too many character items");
        appendWord(bytes, 0x4D4A); appendWord(bytes, unsigned(entries.size()));
        for (const auto *item : entries) {
            auto encoded = writeD2sItem(exportD2sItem(snapshot, *item, content), content);
            bytes.insert(bytes.end(), encoded.begin(), encoded.end());
        }
    };
    items(false);
    appendWord(bytes, 0x4D4A); appendWord(bytes, unsigned(snapshot.corpses.size()));
    for (const auto &corpse : snapshot.corpses) {
        appendDword(bytes, corpse.nativeUnknown);
        appendDword(bytes, uint32_t(corpse.position.x)); appendDword(bytes, uint32_t(corpse.position.y));
        items(false, corpse.items);
    }
    appendWord(bytes, 0x666A); if (header.mercSeed) items(true);
    appendWord(bytes, 0x666B); bytes.push_back(snapshot.ironGolem ? 1 : 0);
    if (snapshot.ironGolem) {
        auto item = *snapshot.ironGolem;
        const auto *definition = content.items.find(item.definition);
        require(definition && item.identified && definition->equipment.known &&
            (content.tables.at(definition->base.sourceTable).number(definition->base.sourceRow,"bitfield1").value_or(0)&2) != 0,
            "invalid Iron Golem item");
        const auto slot = std::find(definition->equipment.slots.begin(), definition->equipment.slots.end(), true);
        require(slot != definition->equipment.slots.end(), "Iron Golem item has no body slot");
        item.location = ContainerLocation{snapshot.containers.equipment, {int(slot-definition->equipment.slots.begin()),0}};
        snapshot.inventory.items.emplace(item.id, item);
        auto encoded = writeD2sItem(exportD2sItem(snapshot, item, content), content);
        bytes.insert(bytes.end(), encoded.begin(), encoded.end());
    }
    require(bytes.size() <= maxSaveBytes, "save too large");
    writeD2sHeader(bytes, header);
    return bytes;
}
} // namespace d2x
