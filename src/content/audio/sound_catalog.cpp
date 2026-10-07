#include "sound_catalog.hpp"
#include "content/character/character_attributes.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace d2x {
namespace {
std::string lower(std::string value) {
    for (auto &ch : value) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}
SoundRule rule(const DataTable &table, size_t row, std::string_view name,
               std::string_view delay = {}, std::string_view probability = {},
               std::string_view volume = {}, bool interruptible = false) {
    SoundRule result;
    result.sound = table.value(row, name);
    result.delay = delay.empty() ? 0 : std::max(0, table.number(row, delay).value_or(0)) / 25.f;
    result.probability = probability.empty() ? 100 : std::clamp(table.number(row, probability).value_or(0), 0, 100);
    result.volume = volume.empty() ? -1 : std::clamp(table.number(row, volume).value_or(0), 0, 255) / 255.f;
    result.interruptible = interruptible;
    return result;
}
}
SoundCatalog::SoundCatalog(Archives &archives) : sounds_(archives.read("data/global/excel/sounds.txt")) {
    for (size_t row = 0; row < sounds_.rows().size(); ++row)
        soundRows_.emplace(sounds_.value(row, "Sound"), row);
    const DataTable voices(archives.read("data/global/excel/monsounds.txt"));
    std::map<std::string, MonsterSoundDefinition, std::less<>> profiles;
    for (size_t row = 0; row < voices.rows().size(); ++row) {
        const auto id = voices.value(row, "Id");
        if (id.empty()) continue;
        MonsterSoundDefinition profile;
        profile.attacks = {rule(voices,row,"Attack1","Att1Del","Att1Prb",{},true),
                           rule(voices,row,"Attack2","Att2Del","Att2Prb",{},true)};
        profile.weapons = {rule(voices,row,"Weapon1","Wea1Del",{},"Wea1Vol",true),
                           rule(voices,row,"Weapon2","Wea2Del",{},"Wea2Vol",true)};
        for (size_t i = 0; i < profile.skills.size(); ++i)
            profile.skills[i] = rule(voices,row,"Skill" + std::to_string(i + 1),{},{},{},true);
        profile.hit = rule(voices,row,"HitSound","HitDelay");
        profile.death = rule(voices,row,"DeathSound","DeaDelay");
        profile.footstep = rule(voices,row,"Footstep",{},"FsPrb");
        profile.footstepLayer = rule(voices,row,"FootstepLayer",{},"FsPrb");
        profile.footstepCount = std::max(0, voices.number(row,"FsCnt").value_or(0));
        profile.footstepOffset = voices.number(row,"FsOff").value_or(0);
        profile.neutral = rule(voices,row,"Neutral");
        profile.neutralInterval = std::max(0, voices.number(row,"NeuTime").value_or(0)) / 25.f;
        profiles.emplace(id, std::move(profile));
    }
    const DataTable monsters(archives.read("data/global/excel/monstats.txt"));
    for (size_t row = 0; row < monsters.rows().size(); ++row) {
        const auto id = monsters.number(row,"hcIdx");
        if (!id) continue;
        const auto voice = monsters.value(row,"MonSound");
        if (voice.empty()) monsters_.emplace(*id, MonsterSoundDefinition{}); // Intentional silent actor.
        else if (const auto found = profiles.find(voice); found != profiles.end()) monsters_.emplace(*id, found->second);
    }
    const DataTable characters(archives.read("data/global/excel/charstats.txt"));
    int identity = 0;
    for (const auto &character : loadCharacterDefinitions(characters)) {
        const auto name = lower(std::string(characters.value(character.sourceRow,"class")));
        players_.emplace(identity++, PlayerSoundDefinition{SoundRule{name + "_hit_1"},
            SoundRule{name + "_death_1"}, SoundRule{name + "_needkey_1"}});
    }
    const DataTable skills(archives.read("data/global/excel/skills.txt"));
    for (size_t row = 0; row < skills.rows().size(); ++row)
        if (const auto id = skills.number(row,"Id"))
            skills_.emplace(*id, SkillSoundDefinition{rule(skills,row,"stsound","stsounddelay",{},{},true),
                rule(skills,row,"dosound","dosounddelay",{},{},true)});
    // Objects.txt has no sound selector. Retail 1.13c's object sound table
    // (RVA F6C58, consumed by 22570) supplies mode -> Sounds identity. Bind
    // verified token families through the current MPQ rather than its class IDs.
    struct ObjectSound { const char *tokens; int mode; const char *sound; };
    constexpr ObjectSound objectSounds[]{
        {"ew",1,"explosion_medium_1"}, {"bx",1,"object_barrel_explode"},
        {"bd bj",1,"object_basket_1"}, {"qa qb ub",1,"object_bed"},
        {"xj",1,"object_bridge_mephisto"}, {"yt ys",1,"object_burialchest"},
        {"c5 c6 c1 c2 c4 c3 mc qc vb 8p 8q 8r 3b 3c 3d 3f 3g 3h 3i 3j 3k",1,"object_casket"},
        {"ca cb q1 q6 q7 q8 jc cy xb xc xd xe xf xg xh xi y7 y8 y9 ya yc yf 5f 5g",1,"object_chest_large"},
        {"q2 q3 q9 c8 c9 jz c0 cx cu cd 6q",1,"object_chest_small"},
        {"tm 6u 8c",1,"object_chest_special"}, {"cn cc",1,"object_cocoon_1"},
        {"z3 z4 gs",1,"object_corpse_drop"},
        {"z1 z2 z5 gc cp sx gf dg df qh qi qj qk qo 8j",1,"object_corpse_roll"},
        {"xp",1,"object_dungeonbasket"}, {"ea",1,"object_dungeonguy"},
        {"qs qt",1,"object_fissure_1"}, {"gp",1,"object_goopile_1"}, {"hi",1,"object_grave"},
        {"rn ra",1,"object_ratnest_1"}, {"xn yz 6b 6c 3s 3t",1,"object_rockpile"},
        {"cf",1,"object_shrine_hell_2"}, {"6n",1,"object_skel"},
        {"is ib y1",1,"object_skullpile_1"}, {"hv",1,"object_stash_hell_1"},
        {"iu 70",1,"object_stash_hell_2"}, {"hg",1,"object_stash_hell_3"}, {"ir",1,"object_stash_hell_4"},
        {"jx mt 6y 5u 5v",1,"object_stash_jungle_1"}, {"jv",1,"object_stash_jungle_3"},
        {"ju",1,"object_stash_jungle_4"}, {"mv",1,"object_stash_mephisto_1"},
        {"mu",1,"object_stash_mephisto_2"}, {"c7 cq",1,"object_stone_large"},
        {"ry rz",1,"object_stone_small"}, {"pp",1,"object_townportal"},
        {"u1 u2 u3 u4 u5 q4 q5 6i 6j 6k 8d 8e 8f 8g 8h",1,"object_urn_break_1"},
        {"b1 ct 5w 3l",1,"object_wood_break_1"}, {"yn",1,"object_xbarrel"},
        {"3q",1,"object_xbody1"}, {"3u 3v",1,"object_xbody2"},
        {"3w 5e",1,"object_xhiddenstash"}, {"3w 5e",5,"object_xhiddenstash"},
        {"yp 6a",1,"object_xwoodchest"}, {"cz",1,"skeleton_walk_1"},
        {"uy 15 18 19",1,"trappedsoul_down_1"}, {"uy 15 18 19",5,"trappedsoul_down_1"},
        {"a3 a4 3z 4c",2,"object_armorstand"}, {"b4 b5",2,"object_bookshelf"},
        {"l1 l2",2,"object_chest_large"}, {"w1 w2 3x 3y",2,"object_weaponrack"}
    };
    std::map<std::string, std::map<int, std::string>, std::less<>> tokenSounds;
    for (const auto &definition : objectSounds) {
        std::istringstream tokens(definition.tokens);
        for (std::string token; tokens >> token;) tokenSounds[token][definition.mode] = definition.sound;
    }
    const DataTable objects(archives.read("data/global/excel/objects.txt"));
    for (size_t row = 0; row < objects.rows().size(); ++row) {
        const auto id = objects.number(row,"Id");
        if (!id) continue;
        const int operation = objects.number(row,"OperateFn").value_or(0);
        switch (operation) {
        case 1: case 3: case 4: case 5: case 7: case 14: case 19: case 20:
        case 26: case 30: case 48: case 51: case 68: break;
        default: continue;
        }
        const auto token = lower(std::string(objects.value(row,"Token")));
        if (const auto found = tokenSounds.find(token); found != tokenSounds.end())
            for (const auto &[mode, sound] : found->second) objects_[{*id,mode}] = SoundRule{sound};
        // Three shared graphics families have distinct native sound selectors.
        if (token == "6t") objects_[{*id,1}] = SoundRule{operation == 30 ? "object_barrel_explode" : "object_xbarrel"};
        if (token == "jw") objects_[{*id,1}] = SoundRule{operation == 51 ? "object_stash_jungle_2" : "object_corpse_roll"};
        if (token == "5f" && objects.value(row,"Name") == "woodchest2L")
            objects_[{*id,1}] = SoundRule{"object_chest_small"};
    }
    auto itemSound = [](const DataTable &table, size_t row) {
        const auto code = table.value(row,"code");
        return ItemSoundDefinition{std::string(code.empty() ? table.value(row,"item") : code),
            std::string(table.value(row,"dropsound")), table.number(row,"dropsfxframe").value_or(-1)};
    };
    for (const auto *name : {"weapons", "armor", "misc"}) {
        const DataTable table(archives.read(std::string("data/global/excel/") + name + ".txt"));
        for (size_t row = 0; row < table.rows().size(); ++row) {
            auto definition = itemSound(table,row);
            if (!definition.code.empty()) items_.emplace(definition.code,definition);
        }
    }
    for (const auto &[name, target] : {std::pair{"uniqueitems", &uniqueItems_}, std::pair{"setitems", &setItems_}}) {
        const DataTable table(archives.read(std::string("data/global/excel/") + name + ".txt"));
        for (size_t row = 0; row < table.rows().size(); ++row)
            target->emplace(int(row),itemSound(table,row));
    }
}
std::optional<size_t> SoundCatalog::soundRow(std::string_view id) const {
    const auto found = soundRows_.find(id);
    return found == soundRows_.end() ? std::nullopt : std::optional{found->second};
}
const SoundRule *SoundCatalog::object(int identity, int mode) const {
    const auto found = objects_.find({identity, mode});
    return found == objects_.end() ? nullptr : &found->second;
}
const MonsterSoundDefinition *SoundCatalog::monster(int id) const {
    const auto found = monsters_.find(id); return found == monsters_.end() ? nullptr : &found->second;
}
const PlayerSoundDefinition *SoundCatalog::player(int id) const {
    const auto found = players_.find(id); return found == players_.end() ? nullptr : &found->second;
}
const SkillSoundDefinition *SoundCatalog::skill(int id) const {
    const auto found = skills_.find(id); return found == skills_.end() ? nullptr : &found->second;
}
std::optional<ItemSoundDefinition> SoundCatalog::item(std::string_view code, ItemQuality quality, int specialRow) const {
    const auto base = items_.find(code);
    if (base == items_.end()) return {};
    auto result = base->second;
    if (specialRow >= 0 && (quality == ItemQuality::Unique || quality == ItemQuality::Set)) {
        const auto &table = quality == ItemQuality::Unique ? uniqueItems_ : setItems_;
        const auto special = table.find(specialRow);
        if (special != table.end() && special->second.code == code) {
            if (!special->second.drop.empty()) result.drop = special->second.drop;
            if (special->second.dropFrame >= 0) result.dropFrame = special->second.dropFrame;
        }
    }
    return result;
}
} // namespace d2x
