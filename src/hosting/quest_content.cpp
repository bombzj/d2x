#include "quest_content.hpp"
#include "character_content.hpp"
#include "item_content.hpp"
#include "content/items/item_magic_loot.hpp"
#include "gameplay/loot/special.hpp"
#include "content/items/item_quality.hpp"
#include "content/items/object_loot.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x {
void prepareQuests(GameHost &host,GameHandle game,Archives &archives,const ClassicData &data) {
    for(const auto &source:host.pendingQuests(game)) {
        server::quests::Prepared result;result.source=source;
        auto character=source.character;character.inventory.items.clear();auto random=source.seed;
        try {
            const auto add=[&](std::string code,unsigned level,ItemGeneration generation={},unsigned quantity=1) {
                auto item=prepareItem(data,{code,quantity,{},level,std::move(generation)},random,source.difficulty);
                item.id=EntityId{character.inventory.items.size()+1};item.location=ContainerLocation{character.containers.backpack,{}};
                item.nativeQuestDifficulty=unsigned(source.difficulty);item.identified=true;
                character.inventory.items.emplace(item.id,item);result.items.items.push_back(std::move(item));
            };
            const auto questUnique=[&](const std::string &code) {
                const auto unique=rollSpecialItem(data.uniqueItems,code,99,random);random=unique.randomState;
                if(!unique.row) throw std::runtime_error("Missing original unique quest item: "+code);
                const auto row=std::find_if(data.uniqueItems.begin(),data.uniqueItems.end(),[&](const auto &r){return r.row==*unique.row;});
                const auto properties=rollSpecialProperties(*row,random);random=properties.randomState;
                ItemGeneration generation;generation.quality=ItemQuality::Unique;generation.specialRow=int32_t(row->row);
                generation.requiredLevel=row->requiredLevel;generation.propertyRolls=properties.values;return generation;
            };
            using server::quests::RewardKind;
            switch(source.kind) {
            case RewardKind::Figurine: add(data.goldenBird.figurine,unsigned(source.character.player.level));break;
            case RewardKind::GoldenBird: add(data.goldenBird.bird,1);break;
            case RewardKind::DeliverBird: break;
            case RewardKind::LifePotion: add(data.goldenBird.potion,1);break;
            case RewardKind::Gidbinn: add(data.gidbinnCode,unsigned(source.character.player.level));break;
            case RewardKind::ReturnGidbinn: break;
            case RewardKind::SmashOrb: case RewardKind::ReturnTome: break;
            case RewardKind::PlaceSoulstone: break;
            case RewardKind::ThawAnya: break;
            case RewardKind::DefrostPotion: add(data.prisonOfIce.potion,1);break;
            case RewardKind::ResistanceScroll: add(data.prisonOfIce.scroll,1);break;
            case RewardKind::AnyaRare: {
                const auto level=source.character.player.level;
                const size_t tier=source.difficulty==2 && level>65?2:source.difficulty>=1 && level>45?1:0;
                const auto pool=data.prisonOfIce.rewards.find(source.classCode);
                if(pool==data.prisonOfIce.rewards.end() || pool->second[tier].empty()) throw std::runtime_error("Missing original Anya class reward");
                const auto &code=pool->second[tier][limitedRandom(random,unsigned(pool->second[tier].size()))];
                const auto *base=data.items.find(code);if(!base) throw std::runtime_error("Missing original Anya reward base");
                auto roll=rollAffixItem(data,*base,ItemQuality::Rare,level,random,source.classCode);
                if(!roll.deferred.empty()) throw std::runtime_error(roll.deferred);random=roll.randomState;
                add(code,unsigned(level),std::move(roll.generation));break;
            }
            case RewardKind::ForgeHammer: add(data.hellforge.hammer,unsigned(source.character.player.level),questUnique(data.hellforge.hammer));break;
            case RewardKind::SmashSoulstone:
                for(const unsigned tier:{0u,1u,1u,2u}) add(data.hellforge.gems[tier][limitedRandom(random,7)],50);
                add(data.hellforge.runes.at(size_t(source.difficulty))[limitedRandom(random,11)],50);break;
            case RewardKind::KhalimEye: case RewardKind::KhalimBrain: case RewardKind::KhalimHeart: case RewardKind::KhalimFlail:
            case RewardKind::CouncilCube: case RewardKind::LamTome: case RewardKind::Soulstone: {
                std::string code;
                if(source.kind==RewardKind::CouncilCube) code=data.cubeCode;
                else if(source.kind==RewardKind::LamTome) code=data.lamTomeCode;
                else if(source.kind==RewardKind::Soulstone) code=data.soulstoneCode;
                else {const size_t part=source.kind==RewardKind::KhalimEye?0:source.kind==RewardKind::KhalimBrain?1:source.kind==RewardKind::KhalimHeart?2:3;code=data.khalimRecipe.inputs[part];}
                for(unsigned count=0;count<source.quantity;++count) {
                    ItemGeneration generation;
                    if(source.kind==RewardKind::KhalimFlail) generation=questUnique(code);
                    add(code,unsigned(source.character.player.level),std::move(generation));
                }
                if(source.kind==RewardKind::KhalimEye || source.kind==RewardKind::KhalimBrain || source.kind==RewardKind::KhalimHeart) {
                    WorldCatalog world(archives,source.difficulty);const auto entry=resolveObjectTreasure(data,world,source.actor.area,source.difficulty);
                    if(!entry.deferred.empty()) throw std::runtime_error(entry.deferred);
                    auto loot=planItemLoot(data,data.tables.at("itemratio"),entry.treasureClass,entry.itemLevel,0,random,source.uniques,source.classCode,0,0,DropQuality::Magic);
                    if(!loot.deferred.empty()) throw std::runtime_error(loot.deferred);random=loot.randomState;
                    for(auto &drop:loot.drops) {
                        if(drop.generation.quality==ItemQuality::Unique && drop.generation.specialRow>=0 && !data.tables.at("uniqueitems").number(size_t(drop.generation.specialRow),"nolimit").value_or(0)) result.items.limitedUniques.insert(size_t(drop.generation.specialRow));
                        add(drop.code,drop.level,std::move(drop.generation),drop.quantity);
                    }
                    const auto piles=limitedRandom(random,5)+5;
                    for(unsigned i=0;i<piles;++i) {rollRandom(random);add("gld",unsigned(entry.itemLevel),{},unsigned(entry.itemLevel)+uint32_t(random)%(5*unsigned(entry.itemLevel)));}
                }
                break;
            }
            case RewardKind::GidbinnRing: {
                const auto *base=data.items.find("rin");if(!base) throw std::runtime_error("Missing original Ormus ring base");
                const unsigned level=source.difficulty==0?21:source.difficulty==1?35:75;
                auto roll=rollAffixItem(data,*base,ItemQuality::Rare,int(level),random,source.classCode);
                if(!roll.deferred.empty()) throw std::runtime_error(roll.deferred);
                random=roll.randomState;add("rin",level,std::move(roll.generation));break;
            }
            case RewardKind::Bark: add("bks",1);break;
            case RewardKind::TranslateScroll: add("bkd",1);break;
            case RewardKind::Malus: if(!source.conversation) add("hdm",1);break;
            case RewardKind::ExplainStaffScroll: break;
            case RewardKind::SkillBook: case RewardKind::HoradricScroll: case RewardKind::Cube:
            case RewardKind::StaffShaft: case RewardKind::ViperAmulet: {
                std::string code;
                if(source.kind==RewardKind::SkillBook) {
                    for(const auto &[key,item]:data.items.entries()) if(item.code=="ass") code=key;
                } else if(source.kind==RewardKind::HoradricScroll) code=data.staffRecipe.scroll;
                else if(source.kind==RewardKind::Cube) {for(const auto &[key,item]:data.items.entries()) if(item.opensCube) code=key;}
                else if(source.kind==RewardKind::StaffShaft) code=data.staffRecipe.inputs[0];
                else code=data.staffRecipe.inputs[1];
                const auto *base=data.items.find(code);if(!base) throw std::runtime_error("Missing original Act II quest item");
                WorldCatalog world(archives,source.difficulty);
                const unsigned level=unsigned(std::clamp(world.level(int(source.actor.area)).population.level.at(size_t(source.difficulty)),1,99));
                // Quest callbacks request unique quality directly, bypassing TC's
                // quest-item rejection. Resolve any original unique row/properties.
                for(unsigned i=0;i<source.quantity;++i) {
                    ItemGeneration generation;
                    const auto unique=source.kind==RewardKind::Cube?SpecialItemRoll{}:rollSpecialItem(data.uniqueItems,code,99,random);
                    if(source.kind!=RewardKind::Cube) random=unique.randomState;
                    if(unique.row) {
                        const auto row=std::find_if(data.uniqueItems.begin(),data.uniqueItems.end(),[&](const auto &r){return r.row==*unique.row;});
                        const auto properties=rollSpecialProperties(*row,random);random=properties.randomState;
                        generation.quality=ItemQuality::Unique;generation.specialRow=int32_t(row->row);
                        generation.requiredLevel=row->requiredLevel;generation.propertyRolls=properties.values;
                    }
                    add(code,level,std::move(generation));
                }
                if(source.kind!=RewardKind::SkillBook) {
                    // A2Q2 operations39/40/41 and A2Q3 altar: forced magic
                    // chest TC, followed by five through nine original gold piles.
                    const auto entry=resolveObjectTreasure(data,world,source.actor.area,source.difficulty);
                    if(!entry.deferred.empty()) throw std::runtime_error(entry.deferred);
                    auto loot=planItemLoot(data,data.tables.at("itemratio"),entry.treasureClass,entry.itemLevel,0,random,source.uniques,source.classCode,0,0,DropQuality::Magic);
                    if(!loot.deferred.empty()) throw std::runtime_error(loot.deferred);
                    random=loot.randomState;
                    for(auto &drop:loot.drops) {
                        if(drop.generation.quality==ItemQuality::Unique && drop.generation.specialRow>=0 && !data.tables.at("uniqueitems").number(size_t(drop.generation.specialRow),"nolimit").value_or(0)) result.items.limitedUniques.insert(size_t(drop.generation.specialRow));
                        add(drop.code,drop.level,std::move(drop.generation),drop.quantity);
                    }
                    const auto piles=limitedRandom(random,5)+5;
                    for(unsigned i=0;i<piles;++i) {rollRandom(random);add("gld",level,{},level+uint32_t(random)%(5*level));}
                }
                break;
            }
            case RewardKind::CainRing: {
                const auto *base=data.items.find("rin");if(!base) throw std::runtime_error("Missing original Akara ring base");
                // ACT1Q4_ScrollMessage118: Normal magic ilvl7, NM rare30,
                // Hell rare60. Properties themselves still come from MPQ.
                const unsigned level=source.difficulty==0?7:source.difficulty==1?30:60;
                auto roll=rollAffixItem(data,*base,source.difficulty==0?ItemQuality::Magic:ItemQuality::Rare,int(level),random,source.classCode);
                if(!roll.deferred.empty()) throw std::runtime_error(roll.deferred);
                random=roll.randomState;add("rin",level,std::move(roll.generation));break;
            }
            case RewardKind::Rogue: case RewardKind::IronWolf: case RewardKind::RescueRunes: {
                if(source.kind==RewardKind::RescueRunes) {
                    const auto count=source.character.player.quests.at(size_t(source.difficulty))[questIndex(QuestId::RescueOnMountArreat)].flags&15u;
                    if(count<13) throw std::runtime_error("Original rescue reward requires thirteen survivors");
                    const unsigned rewards=count==15?3:count==14?2:1;
                    for(unsigned i=0;i<rewards;++i) add(data.hellforge.runes[0][6+i],1);
                }
                if(source.character.player.hireling.sourceRow>=0) break;
                const auto &monsters=data.tables.at("monstats");std::optional<int> seller;
                const auto npc=source.kind==RewardKind::Rogue?"kashya":source.kind==RewardKind::IronWolf?"asheara":"qual-kehk";
                for(size_t row=0;row<monsters.rows().size();++row) if(monsters.value(row,"Id")==npc) seller=monsters.number(row,"hcIdx");
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
