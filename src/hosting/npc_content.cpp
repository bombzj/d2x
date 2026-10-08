#include "npc_content.hpp"
#include "content/monsters/monster_catalog.hpp"
#include "content/npc/vendor_stock.hpp"
#include "content/string_table.hpp"
#include <algorithm>
namespace d2x {
void prepareNpcs(Archives &archives, const ClassicData &data, PreparedWorldArea &prepared) {
    MonsterCatalog catalog(archives, data.tables.at("monstats")); ClassicStrings strings(archives);
    const auto message = [&](const NpcSpeech *speech) -> std::optional<uint16_t> {
        if (!speech) return {};
        for (const auto &[key, value] : strings.entries()) {
            (void)value; const auto index = strings.index(key);
            if (index >= 0 && index <= UINT16_MAX && strings.speech(index) == speech->text) return uint16_t(index);
        }
        return {};
    };
    auto &area = prepared.authority;
    for (const auto &source : prepared.terrain.map->terrain.data.objects) {
        if (source.type != 1) continue;
        const auto preset = catalog.preset(area.act, source.id, prepared.terrain.map->terrain.data.version, source.nativeIdentity);
        const auto *record = catalog.find(preset.id);
        if (!record || !record->npc || !record->interact || record->hostile()) continue;
        server::NpcRule rule; rule.code = record->id; rule.nativeClass = record->index; rule.size = record->collisionSize;
        rule.vendor = data.vendors.contains(rule.code); rule.repair = npcCanRepair(rule.code);
        rule.identify = npcCanIdentify(rule.code); rule.heal = npcCanHeal(rule.code);
        rule.gamble = npcCanGamble(rule.code);
        rule.introduction = npcIntroductionKey(rule.code, area.act);
        if(rule.code=="akara") for(const int index:{64,65,71,76}) if(!strings.speech(index).empty()) rule.denMessages.insert(uint16_t(index));
        for (const auto &character : data.characters) if (const auto text = message(introSpeech(data.npcDialogues, rule.code, character.name, area.act))) rule.introductions.emplace(character.name, *text);
        for (size_t i = 0; i < 8; ++i) if (const auto text = message(gossipSpeech(data.npcDialogues, rule.code, i, area.act)); text && std::find(rule.gossip.begin(),rule.gossip.end(),*text)==rule.gossip.end()) rule.gossip.push_back(*text);
        area.npcs.push_back({{}, {float(source.x),float(source.y)}, std::move(rule)});
    }
}
}
