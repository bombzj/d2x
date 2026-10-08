#include "merchant_content.hpp"
#include "character_content.hpp"
#include "content/npc/vendor_stock.hpp"
#include "content/items/item_pricing.hpp"
#include "content/items/item_properties.hpp"
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
        const bool free=character.player.quests.at(size_t(result.source.difficulty)).at(questIndex(QuestId::SearchForCain)).stage>=uint32_t(CainStage::Rescued);
        for (const auto &[id,item] : character.inventory.items) {
            const auto *at=std::get_if<ContainerLocation>(&item.location);
            if (!item.identified && at && (at->container==c.backpack || at->container==c.equipment || at->container==c.beltEquipment)) result.prices.emplace(id,free?0:100);
        }
        return result;
    }
    const auto &vendor=data.vendors.at(result.source.npc.code);
    if (request.action==Action::Open) {
        if (!result.source.stock.offers.empty()) return result;
        auto projection=character; projection.inventory.items.clear();
        for (const auto &offer : planVendorStock(data,vendor,unsigned(character.player.level),result.source.difficulty,result.source.seed)) {
            auto item=vendorItem(offer,data); item.id=EntityId{uint64_t(result.offers.size()+1)}; item.identified=true;
            projection.inventory.items.emplace(item.id,item); result.offers.push_back({std::move(item),{},offer.permanent});
        }
        const auto rules=prepareEquipmentRules(data,projection);
        for (auto &offer : result.offers) offer.equipment=rules->items.at(offer.item.id);
        return result;
    }
    const bool repair=request.action==Action::Repair || request.action==Action::RepairAll, sale=request.action==Action::Sell;
    const auto discounts=factors(vendor,character.player.quests.at(size_t(result.source.difficulty)),repair,sale);
    const auto quote=[&](const ItemInstance &item) {
        const auto price=itemTradePrice(data,item,vendor,repair,discounts,sale?0:result.source.reducedPrices,sale,result.source.difficulty);
        if (price && (!repair || *price)) result.prices.emplace(item.id,*price);
    };
    if (request.action==Action::Buy) {
        if (request.item) if (const auto offer=result.source.stock.offers.find(request.item->id);offer!=result.source.stock.offers.end()) quote(offer->second.item);
    } else for (const auto &[id,item] : character.inventory.items) {
        if (request.item && request.item->id!=id) continue;
        const auto *at=std::get_if<ContainerLocation>(&item.location); if (!at || (at->container!=c.backpack && at->container!=c.equipment && at->container!=c.beltEquipment && !(sale && at->container==c.cursor))) continue;
        const auto *definition=data.items.find(item.definition); if (!definition) continue;
        if (sale && data.tables.at(definition->base.sourceTable).number(definition->base.sourceRow,"quest").value_or(0)) continue;
        if (repair) {
            if (!definition->equipment.repairable) continue;
            bool charges=false; for(const auto &stat:resolveItemStats(data,item,character.player.level))
                if(stat.name=="item_charged_skill" && (unsigned(stat.rawValue)&255)<(unsigned(stat.rawValue)>>8)) charges=true;
            if(charges) { result.deferred="Charged-skill replenishment is not implemented"; return result; }
        }
        quote(item);
    }
    if (request.item && !repair && !result.prices.contains(request.item->id)) result.deferred="Item is not eligible for this vendor service";
    return result;
}
}
void prepareMerchant(GameHost &host,GameHandle game,const ClassicData &data) {
    for (auto &source : host.pendingMerchant(game)) {
        server::merchant::Prepared result;
        try { result=prepare(data,source); }
        catch (const std::exception &error) { result.source=std::move(source); result.deferred=error.what(); }
        host.installMerchant(game,std::move(result));
    }
}
}
