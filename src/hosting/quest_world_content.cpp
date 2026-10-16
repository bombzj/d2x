#include "combat_content.hpp"
#include "content/classic_data.hpp"
#include "content/monsters/monster_catalog.hpp"
#include "core/random.hpp"
#include <stdexcept>
namespace d2x {
void prepareQuestWorld(Archives &archives,const ClassicData &data,PreparedWorldArea &prepared) {
    auto &area=prepared.authority;
    const auto catalogSource = archives.prepared<MonsterCatalog>([&] { return MonsterCatalog(archives, data.tables.at("monstats")); });
    const auto &catalog = *catalogSource;
    const auto fixed=[&](std::string name,Vec position) {
        if(area.nativeQuestMonsters.contains(name)) return;
        const auto *boss=catalog.superUnique(name);if(!boss) {area.populationDeferred.push_back("Missing original quest SuperUnique: "+name);return;}
        const auto *record=catalog.find(boss->monster);if(!record || !record->hostile()) return;
        server::AreaDefinition::NativePresetMonster source;
        source.definition=record->index;source.position=position;source.retry=true;
        source.superUnique=name;source.uniqueIndex=boss->index;source.stacks=boss->stacks;
        source.modifiers=boss->modifiers;source.automatic=boss->autoPosition;
        const int extra=boss->minGroup && boss->maxGroup?prepared.terrain.request.difficulty:0;
        source.minimum=boss->minGroup+extra;source.maximum=boss->maxGroup+extra;
        if(name=="Baal Subject 2") {
            const auto *mage=catalog.find("skmage_cold3");
            if(!mage) throw std::runtime_error("Missing original Baal wave mage");
            source.extras.push_back({mage->index,10,10,20,true,false});
        }
        area.nativeQuestMonsters.emplace(std::move(name),std::move(source));
    };
    for(const auto &object:area.objects) if(area.act==2 && object.rule.operation==31) {
        const auto *record=catalog.find("fetish11");
        if(!record) throw std::runtime_error("Missing original Gidbinn monster");
        server::AreaDefinition::NativePresetMonster source;
        source.definition=record->index;source.position=object.position;
        source.randomElite=true;source.automatic=true;source.retry=true;
        area.nativeQuestMonsters.try_emplace("gidbinn",std::move(source));
    }
    if(int(area.id)==108) for(const auto &object:area.objects) {
        if(object.rule.operation>=54 && object.rule.operation<=56) {
            constexpr std::array names{"Infector of Souls","Lord De Seis","Grand Vizier of Chaos"};
            constexpr std::array offsets{Vec{-12,-52},Vec{-39,33},Vec{32,16}};
            const size_t index=size_t(object.rule.operation-54);fixed(names[index],object.position+offsets[index]);
        }
        if(object.type==255) {
            const auto *record=catalog.find("diablo");
            if(!record) throw std::runtime_error("Missing original Diablo monster");
            server::AreaDefinition::NativePresetMonster source;
            source.definition=record->index;source.position=object.position;source.rank=MonsterRank::Boss;
            source.retry=true;source.retryAttempts=5;source.finalRetryAttempts=10;
            area.nativeQuestMonsters.try_emplace("diablo",std::move(source));
        }
    }
    if(int(area.id)==124) for(const auto &object:area.objects) if(object.type==462) fixed("Nihlathak Boss",object.position);
    if(int(area.id)==120) for(const auto &object:area.objects) {
        if(object.rule.operation==63) fixed("Ancient Barbarian 1",object.position);
        if(object.rule.operation==64) fixed("Ancient Barbarian 2",object.position);
        if(object.rule.operation==62) fixed("Ancient Barbarian 3",object.position);
    }
    if(int(area.id)==131) for(const auto &npc:area.npcs) if(npc.rule.code=="baalthrone")
        for(int wave=1;wave<=5;++wave) fixed("Baal Subject "+std::to_string(wave),npc.spawnPosition+Vec{0,13});
}
}
