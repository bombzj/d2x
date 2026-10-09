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
        const MonsterRecord *record = nullptr;
        if (source.type == 1) {
            const auto preset = catalog.preset(area.act, source.id, prepared.terrain.map->terrain.data.version, source.nativeIdentity);
            record = catalog.find(preset.id);
        } else if (source.type == 2 && int(area.id) == 1 && source.nativeIdentity) {
            // OBJECTS_InitFunction54_CainStartPosition places Cain5 at the
            // actual DS1 marker. Quest visibility controls his availability.
            const auto &objects = data.tables.at("objects");
            for (size_t row = 0; row < objects.rows().size(); ++row)
                if (objects.number(row,"Id") == source.id && objects.number(row,"InitFn") == 54) record = catalog.find("cain5");
        }
        if (!record || !record->npc || !record->interact || record->hostile()) continue;
        server::NpcRule rule; rule.code = record->id; rule.nativeClass = record->index; rule.size = record->collisionSize;
        rule.vendor = data.vendors.contains(rule.code); rule.repair = npcCanRepair(rule.code);
        rule.identify = npcCanIdentify(rule.code); rule.heal = npcCanHeal(rule.code);
        rule.gamble = npcCanGamble(rule.code);
        rule.introduction = npcIntroductionKey(rule.code, area.act);
        // Original ACT1 callback message IDs, validated against current MPQ.
        for(int index=64;index<=184;++index) if(!strings.speech(index).empty()) rule.questMessages.insert(uint16_t(index));
        for (const auto &character : data.characters) if (const auto text = message(introSpeech(data.npcDialogues, rule.code, character.name, area.act))) rule.introductions.emplace(character.name, *text);
        for (size_t i = 0; i < 8; ++i) if (const auto text = message(gossipSpeech(data.npcDialogues, rule.code, i, area.act)); text && std::find(rule.gossip.begin(),rule.gossip.end(),*text)==rule.gossip.end()) rule.gossip.push_back(*text);
        const Vec marker{float(source.x)+.5f,float(source.y)+.5f};
        const auto position = area.collision.nearest(marker,record->movementRule());
        if (!area.collision.walkable(position,record->movementRule()) || (position-marker).length()>15)
            throw std::runtime_error("Original NPC spawn is obstructed: "+record->id);
        area.npcs.push_back({{}, position, std::move(rule)});
    }
}
}
