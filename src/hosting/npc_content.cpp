#include "npc_content.hpp"
#include "content/monsters/monster_catalog.hpp"
#include "content/npc/vendor_stock.hpp"
#include "content/string_table.hpp"
#include <algorithm>
#include <cctype>
namespace d2x {
namespace {
std::string speechKey(std::string_view value) {
    std::string key="key:";
    for(const unsigned char c:value) if(std::isalnum(c)) key+=char(std::tolower(c));
    return key;
}
std::string speechText(std::string_view value) {
    std::string text="text:";
    for(const unsigned char c:value) {
        if(std::isspace(c)) continue;
        if(c==0x85) text+="...";
        else if(c==0x91 || c==0x92) text+='\'';
        else if(c==0x93 || c==0x94) text+='"';
        else text+=char(c);
    }
    return text;
}
using SpeechIndexes=std::map<std::string,uint16_t,std::less<>>;
server::NpcRule prepareNpcRule(const ClassicData &data,const SpeechIndexes &indexes,const std::set<uint16_t> &callbacks,const MonsterRecord &record,std::string_view name,int act) {
    const auto message = [&](const NpcSpeech *speech) -> std::optional<uint16_t> {
        if (!speech) return {};
        // Authored names such as MeshifAct3 differ from Sounds' speaker Meshif.
        // Accept only a verified TBL key or the same MPQ text after wrapping.
        if(!speech->quest.empty()) {
            if(const auto keyed=indexes.find(speechKey(speech->quest+speech->state+speech->speaker));keyed!=indexes.end()) return keyed->second;
            // A5 Tyrael's authored wave has no matching Sounds row. Its
            // original TBL key uses the MPQ NPC name instead of a voice ID.
            if(const auto keyed=indexes.find(speechKey(speech->quest+speech->state+std::string(name)));keyed!=indexes.end()) return keyed->second;
        }
        const auto found=indexes.find(speechText(speech->text));
        return found==indexes.end()?std::nullopt:std::optional<uint16_t>{found->second};
    };
    server::NpcRule rule; rule.code = record.id; rule.nativeClass = record.index; rule.size = record.collisionSize;
    rule.interactable=record.interact;rule.walkVelocity=record.walkVelocity.value_or(0);rule.movement=record.movementRule();
    
    rule.vendor = data.vendors.contains(rule.code); rule.repair = npcCanRepair(rule.code);
    rule.identify = npcCanIdentify(rule.code); rule.heal = npcCanHeal(rule.code);
    rule.gamble = npcCanGamble(rule.code);
    rule.introduction = npcIntroductionKey(name, act);
    // Callback identities are original rules; every offered text is validated
    // against the current MPQ rather than supplied by the authority.
    for(const auto &definition:questDefinitions) {
        const auto &key=data.questContent.at(questIndex(definition.id)).speechKey;
        for(const auto &[group,speeches]:data.npcDialogues) {
            (void)group;
            for(const auto &speech:speeches) if(speech.act==act && speech.quest==key && !rule.questSpeeches.contains(std::pair{definition.id,speech.state}))
                if(const auto text=message(questSpeech(data.npcDialogues,key,speech.state,name))) {
                    rule.questMessages.insert(*text);
                    rule.questSpeeches.emplace(std::pair{definition.id,speech.state},*text);
                }
        }
    }
    // A1/A2 acknowledgements also include messages outside journal groups.
    rule.questMessages.insert(callbacks.begin(),callbacks.end());
    for (const auto &character : data.characters) if (const auto text = message(introSpeech(data.npcDialogues, name, character.name, act))) rule.introductions.emplace(character.name, *text);
    for (size_t i = 0; i < 8; ++i) if (const auto text = message(gossipSpeech(data.npcDialogues, name, i, act)); text && std::find(rule.gossip.begin(),rule.gossip.end(),*text)==rule.gossip.end()) rule.gossip.push_back(*text);
    return rule;
}
}
void prepareNpcs(Archives &archives, const ClassicData &data, PreparedWorldArea &prepared) {
    MonsterCatalog catalog(archives, data.tables.at("monstats")); ClassicStrings strings(archives);
    SpeechIndexes speechIndexes;
    for(const auto &[key,value]:strings.entries()) {
        (void)value;const auto index=strings.index(key);
        if(index>=0 && index<=UINT16_MAX) if(const auto text=strings.speech(index);!text.empty()) {
            speechIndexes.try_emplace(speechKey(key),uint16_t(index));
            speechIndexes.try_emplace(speechText(text),uint16_t(index));
        }
    }
    std::set<uint16_t> callbacks;
    for(int index=64;index<=452;++index) if(!strings.speech(index).empty()) callbacks.insert(uint16_t(index));
    auto &area = prepared.authority;
    const auto npcRule=[&](const MonsterRecord &record) {
        const auto name=strings.find(record.name);
        return prepareNpcRule(data,speechIndexes,callbacks,record,name.empty()?std::string_view(record.name):name,area.act);
    };
    unsigned prisonerGroups=0;
    std::vector<Vec> prisonerMarkers;
    for(const auto *code:{"izualghost","drehya","tyrael3"}) {
        const bool wanted=(area.act==3 && std::string_view(code)=="izualghost") || (area.act==4 && std::string_view(code)!="izualghost");
        const auto *record=catalog.find(code);
        if(wanted && record && record->interact && !record->hostile()) area.questNpcs.emplace(code,npcRule(*record));
    }
    for (const auto &source : prepared.terrain.map->terrain.data.objects) {
        if(int(area.id)==111 && source.type==2 && source.nativeIdentity && source.id>=0) {
            const auto &objects=data.tables.at("objects");bool cage=false;
            for(size_t row=0;row<objects.rows().size();++row) if(objects.number(row,"Id")==source.id && objects.number(row,"InitFn")==62) cage=true;
            const auto *prisoner=catalog.find("act5pow");
            const Vec marker{float(source.x),float(source.y)};
            const bool duplicate=std::any_of(prisonerMarkers.begin(),prisonerMarkers.end(),[&](Vec at){return (at-marker).length()<1;});
            if(cage && !duplicate && prisonerGroups<3 && prisoner && !prisoner->hostile() && prisoner->walkVelocity.value_or(0)>0) {
                ++prisonerGroups;prisonerMarkers.push_back(marker);
                unsigned count=0;
                for(int radius=0;radius<=5 && count<5;++radius) for(int y=-radius;y<=radius && count<5;++y) for(int x=-radius;x<=radius && count<5;++x) {
                    if(std::max(std::abs(x),std::abs(y))!=radius) continue;
                    const Vec at{float(source.x+x),float(source.y+y)};
                    if(!area.collision.walkable(at,prisoner->spawnRule()) || std::any_of(area.npcs.begin(),area.npcs.end(),[&](const auto &n){return (n.position-at).length()<1;})) continue;
                    area.npcs.push_back({{},at,npcRule(*prisoner)});++count;
                }
                if(count!=5) area.populationDeferred.push_back("Incomplete original prisoner placement");
            }
        }
        const MonsterRecord *record = nullptr;
        int questInit=0;
        if (source.type == 1) {
            const auto preset = catalog.preset(area.act, source.id, prepared.terrain.map->terrain.data.version, source.nativeIdentity);
            record = catalog.find(preset.id);
        } else if (source.type == 2 && source.nativeIdentity) {
            // OBJECTS_InitFunction54_CainStartPosition places Cain5 at the
            // actual DS1 marker. Quest visibility controls his availability.
            const auto &objects = data.tables.at("objects");
            for (size_t row = 0; row < objects.rows().size(); ++row)
                if (objects.number(row,"Id") == source.id) {
                    const auto init=objects.number(row,"InitFn").value_or(0);
                    if(int(area.id)==1 && init==54) record=catalog.find("cain5");
                    if(int(area.id)==75 && (init==49 || init==50)) {record=catalog.find("hratli");questInit=init;}
                    if(int(area.id)==40 && (init==18 || init==19)) {record=catalog.find("jerhyn");questInit=init;}
                    if(int(area.id)==109 && init==68) {record=catalog.find("nihlathak");questInit=init;}
                    if(int(area.id)==109 && init==71) {record=catalog.find("larzuk");questInit=init;}
                }
        }
        if (!record || record->hostile() || ((!record->npc || !record->interact) && record->id!="baalthrone")) continue;
        auto rule=npcRule(*record);
        rule.questInitFunction=questInit;
        const Vec marker{float(source.x)+.5f,float(source.y)+.5f};
        const auto position = area.collision.nearest(marker,record->movementRule());
        if (!area.collision.walkable(position,record->movementRule()) || (position-marker).length()>15)
            throw std::runtime_error("Original NPC spawn is obstructed: "+record->id);
        area.npcs.push_back({{}, position, std::move(rule)});
    }
}
}
