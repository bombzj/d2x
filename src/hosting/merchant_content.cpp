#include "merchant_content.hpp"
#include "gameplay/items/charges.hpp"
#include "character_content.hpp"
#include "item_content.hpp"
#include "pd2/gheed.hpp"
#include "gameplay/items/pd2/pricing.hpp"
#include "content/npc/vendor_stock.hpp"
#include "content/items/item_pricing.hpp"
#include "content/items/item_properties.hpp"
#include "content/items/item_quality.hpp"
#include "content/items/cube_data.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <stdexcept>
#include "gameplay/quest/den_of_evil.hpp"
#include "gameplay/quest/burial_grounds.hpp"
#include "gameplay/quest/tools_of_trade.hpp"
#include "gameplay/quest/search_for_cain.hpp"
#include "gameplay/quest/forgotten_tower.hpp"
#include "gameplay/quest/sisters_to_slaughter.hpp"
#include "gameplay/quest/acts/act_two_state.hpp"
namespace d2x {
namespace {
std::vector<int> factors(const VendorDefinition &vendor,const DifficultyQuests &quests,bool repair,bool sale) {
    // The values are D2MOO QuestStateFlag IDs, not the quest log's display order.
    auto pendingOrRewarded = [&](int flag) {
        switch (flag) {
        case 1: return quests.at(questIndex(QuestId::DenOfEvil)).stage >= uint32_t(DenStage::Cleared);
        case 2: return quests.at(questIndex(QuestId::SistersBurialGrounds)).stage >= uint32_t(BurialStage::BloodRavenSlain);
        case 3: return quests.at(questIndex(QuestId::ToolsOfTheTrade)).stage >= uint32_t(ToolsStage::RewardReady);
        case 4: return quests.at(questIndex(QuestId::SearchForCain)).stage >= uint32_t(CainStage::Rescued);
        case 5: return quests.at(questIndex(QuestId::ForgottenTower)).stage >= uint32_t(TowerStage::CountessSlain);
        case 6: return quests.at(questIndex(QuestId::SistersToTheSlaughter)).stage >= uint32_t(SlaughterStage::AndarielSlain);
        case 9: return quests.at(questIndex(QuestId::RadamentsLair)).stage >= uint32_t(RadamentStage::Slain);
        case 17: return quests.at(questIndex(QuestId::LamEsensTome)).stage >= 4;
        default: return false;
        }
    };
    std::vector<int> result;
    for (const auto &price : vendor.questPrices)
        if (pendingOrRewarded(price.flag)) result.push_back(repair ? price.repair : sale ? price.buy : price.sell);
    return result;
}
server::merchant::Prepared prepare(const ClassicData &data,server::merchant::Preparation source) {
    using namespace server::merchant;
    Prepared result; result.source=std::move(source); const auto &request=result.source.pending.request;
    const auto &character=result.source.character; const auto &c=character.containers;
    if (result.source.npc.code.empty()) { result.deferred="NPC conversation expired"; return result; }
    if (request.action==Action::IdentifyAll) {
        const auto &cain=character.player.quests.at(size_t(result.source.difficulty)).at(questIndex(QuestId::SearchForCain));
        const bool free=cain.stage>=uint32_t(CainStage::Rescued) && !(cain.flags&cainRescuedByRogues);
        for (const auto &[id,item] : character.inventory.items) {
            const auto *at=std::get_if<ContainerLocation>(&item.location);
            if (!item.identified && at && (at->container==c.backpack || at->container==c.equipment || at->container==c.beltEquipment)) result.prices.emplace(id,free?0:100);
        }
        return result;
    }
    if (request.action==Action::Open || request.action==Action::Gamble) {
        if (result.source.stock.generated) return result;
        if(data.pd2Host && result.source.npc.pd2EventGheed && request.action==Action::Gamble) {
            hosting::pd2::prepareGheedStock(data,result);return result;
        }
        auto projection=character; projection.inventory.items.clear();
        const auto &inventory=data.tables.at("inventory");
        int columns=0,rows=0;
        for(size_t row=0;row<inventory.rows().size();++row) if(inventory.value(row,"class")=="Monster") {
            columns=inventory.number(row,"gridX").value_or(0);rows=inventory.number(row,"gridY").value_or(0);break;
        }
        if(columns<=0 || columns>255 || rows<=0 || rows>255) throw std::runtime_error("Missing native vendor inventory dimensions");
        Stock shelf;shelf.columns=columns;shelf.rows=rows;
        {
            if(!result.source.controlRandom || !result.source.gameRandom) throw std::runtime_error("Missing native vendor random streams");
            auto control=*result.source.controlRandom,gameRandom=*result.source.gameRandom;
            auto uniques=result.source.uniques;
            const auto create=[&](std::string_view code,std::string_view displayCode,ItemQuality quality,unsigned level,bool permanent) {
                    auto requestedQuality=quality;
                    for(int baseAttempt=0;baseAttempt<(request.gamble?1:2);++baseAttempt) {
                    for(int attempt=0;attempt<(request.gamble?1:5);++attempt) {
                        const auto identity=EntityId{++result.allocatedItems};
                        auto item=prepareNativeItem(data,code,int(level),result.source.difficulty,gameRandom,
                            nativeDropQuality(requestedQuality),uniques,NativeItemUse::Vendor);
                        if(item.quality==ItemQuality::Unique && item.specialRow>=0 && !data.tables.at("uniqueitems").number(size_t(item.specialRow),"nolimit").value_or(0)) {
                            result.limitedUniques.insert(size_t(item.specialRow));uniques.insert(size_t(item.specialRow));
                        }
                        if(!request.gamble && item.quality==ItemQuality::Inferior && item.gradeRow>=0) {
                            auto name=std::string(data.tables.at("lowqualityitems").value(size_t(item.gradeRow),"Name"));
                            for(auto &letter:name) letter=char(std::tolower(static_cast<unsigned char>(letter)));
                            if(name=="cracked") {if(attempt==4) return false;continue;}
                        }
                        if(!request.gamble && item.definition!=code) {requestedQuality=ItemQuality::Normal;break;}
                        const auto *base=data.items.find(item.definition);
                        if(!base) throw std::runtime_error("Missing native vendor base");
                        int storePage=0;
                        if(!request.gamble) {
                            const auto &types=data.tables.at("itemtypes"),&pages=data.tables.at("storepage");
                            std::string_view page;
                            for(size_t row=0;row<types.rows().size();++row)
                                if(types.value(row,"Code")==base->base.type) {page=types.value(row,"StorePage");break;}
                            bool valid=false;
                            for(size_t row=0;row<pages.rows().size();++row)
                                if(!page.empty() && pages.value(row,"Code")==page) {valid=true;storePage=int(row);break;}
                            if(!valid) return false;
                        }
                        item.identified=true;
                        const auto stats=resolveItemStats(data,item,int(level));
                        item.durability=itemMaximumDurability(data,item,stats);rechargeItemSkills(item);
                        if((base->equipment.throwable && base->equipment.repairable) || (permanent && !base->equipment.quiver.empty())) {
                            int extra=0;for(const auto &stat:stats) if(stat.effect=="item_extra_stack") extra+=stat.value;
                            item.quantity=unsigned(std::clamp(int(base->maxStack)+extra,1,511));
                        }
                         item.id=identity;item.nativeFlags|=0x82010;
                        auto cell=shelfPosition(shelf,*base,data.items,storePage);
                        if(!cell && !request.gamble && storePage==1) {storePage=2;cell=shelfPosition(shelf,*base,data.items,storePage);}
                        if(!cell) return false;
                        projection.inventory.items.emplace(item.id,item);result.offers.push_back({std::move(item),{},permanent,std::string(displayCode),storePage,*cell});
                        shelf.offers.emplace(identity,result.offers.back());
                        return true;
                    }
                    }
                    return false;
                };
            if(request.gamble) generateGambleStock(data,unsigned(character.player.level),result.source.difficulty,control,
                [&](std::string_view code,std::string_view displayCode,ItemQuality quality,unsigned level) {
                    return create(code,displayCode,quality,level,false);
                });
            else generateVendorStock(data,data.vendors.at(result.source.npc.code),unsigned(character.player.level),result.source.difficulty,control,
                [&](std::string_view code,ItemQuality quality,unsigned level,bool permanent) {
                    return create(code,{},quality,level,permanent);
                });
            result.controlRandom=control;result.gameRandom=gameRandom;
        }
        result.columns=columns;result.rows=rows;
        const auto rules=prepareEquipmentRules(data,projection);
        result.equipment=rules;
        for (auto &offer : result.offers) offer.equipment=rules->items.at(offer.item.id);
        return result;
    }
    const auto &vendor=data.vendors.at(result.source.npc.code);
    const bool repair=request.action==Action::Repair || request.action==Action::RepairAll, sale=request.action==Action::Sell;
    const auto discounts=factors(vendor,character.player.quests.at(size_t(result.source.difficulty)),repair,sale);
    const auto quote=[&](const ItemInstance &item) {
        const auto hiddenBase=result.source.stock.offers.contains(item.id)?result.source.stock.offers.at(item.id).displayCode:std::string{};
        std::optional<unsigned> price;
        if(data.pd2Host && request.gamble && result.source.npc.pd2EventGheed)
            price=pd2MapGamblePrice(result.source.reducedPrices);
        else if(request.gamble)
            price=itemGamblePrice(data,hiddenBase.empty()?item.definition:hiddenBase,character.player.level,item.nativeFormat,result.source.reducedPrices);
        else price=itemTradePrice(data,item,vendor,repair,discounts,sale?0:result.source.reducedPrices,sale,result.source.difficulty);
        // ProjectDiablo 2E8140 caps each repair after all discounts.
        if(data.pd2Host && repair && price) price=std::min(*price,250000u);
        if (price && (!repair || *price)) result.prices.emplace(item.id,*price);
    };
    if (request.action==Action::Buy) {
        if (request.item) if (const auto offer=result.source.stock.offers.find(request.item->id);offer!=result.source.stock.offers.end()) {
            quote(offer->second.item);
            if(request.multibuy && offer->second.permanent && data.items.find(offer->second.item.definition)->autoStack && !data.items.find(offer->second.item.definition)->bookCapacity) {
                auto unit=offer->second.item;unit.quantity=1;
                if(const auto price=itemTradePrice(data,unit,vendor,false,discounts,result.source.reducedPrices,false,result.source.difficulty)) result.unitPrices.emplace(unit.id,*price);
            }
        }
    } else for (const auto &[id,item] : character.inventory.items) {
        if (request.item && request.item->id!=id) continue;
        const auto *at=std::get_if<ContainerLocation>(&item.location); if (!at || (at->container!=c.backpack && at->container!=c.equipment && at->container!=c.beltEquipment && !(sale && at->container==c.cursor))) continue;
        const auto *definition=data.items.find(item.definition); if (!definition) continue;
        if (sale && data.tables.at(definition->base.sourceTable).number(definition->base.sourceRow,"quest").value_or(0)) continue;
        if (repair) {
            auto price=itemTradePrice(data,item,vendor,true,discounts,result.source.reducedPrices,false,result.source.difficulty);
            if(data.pd2Host && price) price=std::min(*price,250000u);
            if(!price || !*price) continue;
            auto repaired=item; const bool identified=repaired.identified; repaired.identified=true; freezeCubeItem(data,repaired);
            rechargeItemSkills(repaired);
            repaired.identified=identified; result.repairs.emplace(id,std::move(repaired));
        }
        quote(item);
        if(sale) {
            bool duplicate=!result.source.stock.conversation && !item.ear && item.personalizedName.empty() &&
                !(item.nativeFlags&(0x100u|0x1000000u|0x400000u)) && item.socketedItems.empty() &&
                !definition->equipment.isType("play");
            if(item.quality==ItemQuality::Inferior && item.gradeRow>=0) {
                auto name=std::string(data.tables.at("lowqualityitems").value(size_t(item.gradeRow),"Name"));
                for(auto &letter:name) letter=char(std::tolower(static_cast<unsigned char>(letter)));
                if(name=="cracked") duplicate=false;
            }
            if(item.quality==ItemQuality::Unique && item.specialRow>=0 &&
                data.tables.at("uniqueitems").number(size_t(item.specialRow),"carry1").value_or(0)) duplicate=false;
            for(const auto &rule:vendor.items) if(rule.permanent && rule.code==item.definition) duplicate=false;
            if(result.source.difficulty && (item.definition=="hp4" || item.definition=="hp5" ||
                item.definition=="mp4" || item.definition=="mp5")) duplicate=false;
            result.resaleDuplicate=duplicate;
            if(duplicate) {
                const auto &types=data.tables.at("itemtypes"),&pages=data.tables.at("storepage");
                std::string_view page;
                for(size_t row=0;row<types.rows().size();++row)
                    if(types.value(row,"Code")==definition->base.type) {page=types.value(row,"StorePage");break;}
                for(size_t row=0;row<pages.rows().size();++row)
                    if(!page.empty() && pages.value(row,"Code")==page) {result.resalePage=int(row);break;}
            }
        }
    }
    if (request.item && !repair && !result.prices.contains(request.item->id)) result.deferred="Item is not eligible for this vendor service";
    if(data.pd2Host && result.source.npc.pd2EventGheed && request.action==Action::Buy && request.gamble && result.deferred.empty())
        hosting::pd2::prepareGheedPurchase(data,result);
    if(!result.repairs.empty()) {auto projection=character;projection.inventory.items=result.repairs;result.equipment=prepareEquipmentRules(data,projection);}
    return result;
}
}
void prepareMerchant(GameHost &host,GameHandle game,const ClassicData &data) {
    // Re-read unique availability after each player's prepared stock commits.
    // A batch snapshot could generate the same limited unique twice.
    const auto pending=host.pendingMerchant(game);
    for (const auto &entry : pending) {
        const auto current=host.pendingMerchant(game);
        const auto found=std::find_if(current.begin(),current.end(),[&](const auto &v){return v.pending.token==entry.pending.token;});
        if(found==current.end()) continue;
        auto source=*found;
        server::merchant::Prepared result;
        try { result=prepare(data,source); }
        catch (const std::exception &error) { result.source=std::move(source); result.deferred=error.what(); }
        host.installMerchant(game,std::move(result));
    }
}
}
