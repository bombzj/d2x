#include "combat_content.hpp"
#include "content/classic_data.hpp"
#include "content/monsters/monster_catalog.hpp"
#include "core/random.hpp"
#include <stdexcept>
namespace d2x {
void prepareQuestWorld(Archives &archives,const ClassicData &data,PreparedWorldArea &prepared) {
    auto &area=prepared.authority;
    MonsterCatalog catalog(archives,data.tables.at("monstats"));
    const auto add=[&](std::string key,std::string code,std::string unique,Vec position,MonsterRank rank,std::string suffix=std::string{},std::string owner=std::string{}) {
        const auto *record=catalog.find(code);
        if(!record || !record->hostile()) {area.populationDeferred.push_back("Missing original quest monster: "+code);return;}
        MonsterIdentity identity{code,unique,"quest."+key+suffix,rank,SpawnOrigin::Preset};identity.ownerSpawnKey=std::move(owner);
        position=area.collision.nearest(position,record->spawnRule());
        auto monster=prepareCombatMonster(archives,data,prepared.terrain.request,identity,position);
        if(!monster) {
            auto substitute=identity;substitute.monster="fallen1";substitute.rank=MonsterRank::Normal;substitute.superUnique.clear();
            monster=prepareCombatMonster(archives,data,prepared.terrain.request,substitute,position);
            if(monster) {monster->identity=identity;area.populationDeferred.push_back("Quest enemy substitute: "+code+" -> fallen1");}
        }
        if(!monster || !area.collision.walkable(position,monster->rule.spawnCollision)) {
            area.populationDeferred.push_back("Quest combat/placement pending: "+key);return;
        }
        area.questGroups[key].push_back(std::move(*monster));
    };
    auto random=initialRandom(prepared.terrain.request.seed+uint32_t(area.id));
    const auto fixed=[&](std::string name,Vec position) {
        if(area.questGroups.contains(name)) return;
        const auto *boss=catalog.superUnique(name);if(!boss) {area.populationDeferred.push_back("Missing original quest SuperUnique: "+name);return;}
        const auto *record=catalog.find(boss->monster);if(!record || !record->hostile()) return;
        add(name,boss->monster,name,position,MonsterRank::SuperUnique);
        const int extra=boss->minGroup && boss->maxGroup?prepared.terrain.request.difficulty:0;
        const int count=boss->minGroup+extra+int(limitedRandom(random,unsigned(boss->maxGroup-boss->minGroup+1)));
        const auto minion=record->minions[0].empty()?record->id:record->minions[0];
        for(int i=0;i<count;++i) add(name,minion,{},position+Vec{float(i%3-1),float(i/3+1)},MonsterRank::Minion,".minion."+std::to_string(i),"quest."+name);
        if(name=="Baal Subject 2") for(int i=0;i<10;++i) add(name,"skmage_cold3",{},position+Vec{float(i%5-2),float(i/5+3)},MonsterRank::Normal,".mage."+std::to_string(i),"quest."+name);
        const size_t expected=1+size_t(count)+(name=="Baal Subject 2"?10:0);
        if(area.questGroups[name].size()!=expected) {area.questGroups.erase(name);area.populationDeferred.push_back("Incomplete original quest group: "+name);}
    };
    for(const auto &object:area.objects) if(area.act==2 && object.rule.operation==31)
        add("gidbinn","fetish11",{},object.position,MonsterRank::Unique);
    if(int(area.id)==108) for(const auto &object:area.objects) {
        if(object.rule.operation>=54 && object.rule.operation<=56) {
            constexpr std::array names{"Infector of Souls","Lord De Seis","Grand Vizier of Chaos"};
            constexpr std::array offsets{Vec{-12,-52},Vec{-39,33},Vec{32,16}};
            const size_t index=size_t(object.rule.operation-54);fixed(names[index],object.position+offsets[index]);
        }
        if(object.type==255) add("diablo","diablo",{},object.position,MonsterRank::Boss);
    }
    if(int(area.id)==124) for(const auto &object:area.objects) if(object.type==462) fixed("Nihlathak Boss",object.position);
    if(int(area.id)==120) for(const auto &object:area.objects) {
        if(object.rule.operation==63) fixed("Ancient Barbarian 1",object.position);
        if(object.rule.operation==64) fixed("Ancient Barbarian 2",object.position);
        if(object.rule.operation==62) fixed("Ancient Barbarian 3",object.position);
    }
    if(int(area.id)==131) for(const auto &npc:area.npcs) if(npc.rule.code=="baalthrone")
        for(int wave=1;wave<=5;++wave) fixed("Baal Subject "+std::to_string(wave),npc.position+Vec{0,13});
}
}
