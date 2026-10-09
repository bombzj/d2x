#include "quest_content.hpp"
#include "character_content.hpp"
#include "item_content.hpp"
#include "content/items/item_magic_loot.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x {
void prepareQuests(GameHost &host,GameHandle game,const ClassicData &data) {
    for(const auto &source:host.pendingQuests(game)) {
        server::quests::Prepared result;result.source=source;
        auto character=source.character;character.inventory.items.clear();auto random=source.seed;
        try {
            const auto add=[&](std::string code,unsigned level,ItemGeneration generation={}) {
                auto item=prepareItem(data,{code,1,{},level,std::move(generation)},random,source.difficulty);
                item.id=EntityId{character.inventory.items.size()+1};item.location=ContainerLocation{character.containers.backpack,{}};
                item.nativeQuestDifficulty=unsigned(source.difficulty);item.identified=true;
                character.inventory.items.emplace(item.id,item);result.items.items.push_back(std::move(item));
            };
            using server::quests::RewardKind;
            switch(source.kind) {
            case RewardKind::Bark: add("bks",1);break;
            case RewardKind::TranslateScroll: add("bkd",1);break;
            case RewardKind::Malus: if(!source.conversation) add("hdm",1);break;
            case RewardKind::CainRing: {
                const auto *base=data.items.find("rin");if(!base) throw std::runtime_error("Missing original Akara ring base");
                // ACT1Q4_ScrollMessage118: Normal magic ilvl7, NM rare30,
                // Hell rare60. Properties themselves still come from MPQ.
                const unsigned level=source.difficulty==0?7:source.difficulty==1?30:60;
                auto roll=rollAffixItem(data,*base,source.difficulty==0?ItemQuality::Magic:ItemQuality::Rare,int(level),random,source.classCode);
                if(!roll.deferred.empty()) throw std::runtime_error(roll.deferred);
                random=roll.randomState;add("rin",level,std::move(roll.generation));break;
            }
            case RewardKind::Rogue: {
                if(source.character.player.hireling.sourceRow>=0) break;
                const auto &monsters=data.tables.at("monstats");std::optional<int> seller;
                for(size_t row=0;row<monsters.rows().size();++row) if(monsters.value(row,"Id")=="kashya") seller=monsters.number(row,"hcIdx");
                if(!seller) throw std::runtime_error("Missing original Kashya identity");
                const auto offers=planHirelingOffers(data.hirelings,*seller,source.difficulty,source.character.player.level,random);
                if(offers.empty()) throw std::runtime_error("Missing original Rogue reward definitions");
                // SUnitNpc::AssignMercenary selects the first available offer.
                const auto &offer=offers.front();
                const auto rule=std::find_if(data.hirelings.begin(),data.hirelings.end(),[&](const auto &d){return d.sourceRow==offer.sourceRow;});
                if(rule==data.hirelings.end()) throw std::runtime_error("Expired original Rogue reward row");
                result.hireling=HirelingRecord{offer.sourceRow,rule->classId,offer.nameKey,offer.level,float(offer.stats.life),offer.stats.experience,offer.seed};
                break;
            }
            }
            result.items.equipment=prepareEquipmentRules(data,character);
        } catch(const std::exception &error) {
            result.items.items.clear();result.items.equipment=std::make_shared<const server::EquipmentRules>();result.deferred=error.what();
        }
        host.installQuests(game,std::move(result));
    }
}
}
