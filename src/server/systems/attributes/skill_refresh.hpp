#pragma once
#include "stat_sources.hpp"
#include "gameplay/skills/stat_callbacks.hpp"
#include "gameplay/skills/rank_sources.hpp"
#include "gameplay/skills/use_source.hpp"
namespace d2x::server::attributes {
struct PlayerSkillRefreshInput {
    EntityId owner;
    int playerClass{},attackRatingClassFactor{};
    const std::map<int,int> &learned;
    const std::set<int> &innate,&states;
    std::span<const int> innateOrder;
    std::span<const NativeSkillSlot> previousOrder;
    std::span<const ChargedSkill> previousCharges;
    const SkillFormulaCatalog &formulas;
    const StatRuleCatalog &stats;
    std::function<ItemHandle(EntityId)> itemHandle;
    std::span<const ItemStatEvent> previousItemEvents;
    std::function<void(const SkillAuraChange &)> auraChanged;
    std::function<void(const SkillPetLimitChange &)> petLimitChanged;
    bool refreshPetLists{};
};
// Synchronous candidate only. No live player, world, storage or output ports.
class PlayerSkillRefresh {
    PlayerSkillRefreshInput input_;
    std::vector<PassiveStatContribution> &passives_;
    const std::map<EntityId,StatSnapshot> &items_;
    const StatSnapshot &values_;
    StatRootRead read_;
    // Rank queries need the unit's stable ownership flags and the current
    // incremental FullStats, not another evaluation of every source list.
    std::shared_ptr<const StatList> rankOwner_;
    std::function<void(StatValue)> write_;
    std::function<std::map<StatKey,int32_t>()> resources_;
    std::map<int,int> base_;
    std::vector<NativeSkillSlot> order_;
    std::vector<ChargedSkill> charges_;
    std::vector<NativeSkillSlot> clearedSelections_;
    std::vector<SkillUseRemoval> clearedUses_;
    std::vector<SkillAuraChange> auraChanges_;
    std::vector<ItemStatEvent> itemEvents_;
    std::map<int,int> ranks() const;
    int highest(int) const;
    void refresh(int);
    void refreshAll();
    void refreshPetType(int);
    void refreshPetLimits();
  public:
    PlayerSkillRefresh(PlayerSkillRefreshInput,std::vector<PassiveStatContribution> &,
        const std::map<EntityId,StatSnapshot> &,const StatSnapshot &,StatRootRead,
        std::function<void(StatValue)>,std::function<std::map<StatKey,int32_t>()>);
    void initialize();
    void refreshPassives() {refreshAll();refreshPetLimits();}
    SkillFormulaSnapshot formulaSnapshot() const;
    void changed(StatKey,int32_t,EntityId source={});
    int level(int id) const {return highest(id);}
    const std::vector<NativeSkillSlot> &order() const {return order_;}
    const std::vector<ChargedSkill> &charges() const {return charges_;}
    const std::vector<NativeSkillSlot> &clearedSelections() const {return clearedSelections_;}
    const std::vector<SkillUseRemoval> &clearedUses() const {return clearedUses_;}
    const std::vector<SkillAuraChange> &auraChanges() const {return auraChanges_;}
    const std::vector<ItemStatEvent> &itemEvents() const {return itemEvents_;}
};
}
