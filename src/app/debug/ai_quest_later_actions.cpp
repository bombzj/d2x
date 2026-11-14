#include "ai_impl.hpp"
#include "content/world/object_mode.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace d2x {
bool DebugAi::Impl::groundItemSeen(const Context &c,std::string_view code) const {
    return observedDrop(c,code,true)!=nullptr;
}
bool DebugAi::Impl::questSearchDrop(Context &c,const QuestStep &step) {
    if(owned(c,step.item)) return true;
    const auto now=Clock::now();
    Goal leaf;leaf.kind=Kind::Acquire;leaf.item=step.item;
    if(rememberedDrops.contains(step.item) || observedDrop(c,step.item) || pickup) return acquireItem(c,leaf);
    // A withdrawn opportunity cannot turn a different quest into a search.
    if(goals[cursor].routeDrop==questState.questRun.step && goals[cursor].questId!=step.stateQuest) return false;
    if(step.witness) {
        if(step.target) if(const auto enemy=find(c,*step.target);enemy && c.combat.hostile(*enemy) &&
            c.combat.monsterTargetEligible(*enemy,false)) {
            approachEnemy(c,*enemy,"assigned quest drop carrier");return false;
        }
        const auto source=find(c,*step.witness,true);
        const auto point=source?source->position:area?c.town.knownTargetPosition(*area,step.witness->type,step.witness->identity):std::nullopt;
        if(!point) {explore(c,"the observed quest drop source");return false;}
        if((Vec{float(point->x),float(point->y)}-c.position).length()>12) {
            approachPoint(c,*point,"timed quest drop source",10);return false;
        }
        if(questState.questRun.sweepReady==Clock::time_point{}) {cancelWalk(c);questState.questRun.sweepReady=now+std::chrono::seconds(90);}
        if(now>=questState.questRun.sweepReady) throw std::runtime_error("Timed quest source produced no observed drop; retaining source and scene");
        wait("quest-drop-spawn","Holding the actual altar's area while normal combat handles its timed monster");
        continuation=Continuation::WorldResult;return false;
    }
    if(step.searchAreas.empty()) throw std::runtime_error("Quest drop has no verified search areas");
    if(questState.questRun.materialPhase>=step.searchAreas.size())
        throw std::runtime_error("No quest drop observed after the configured normal Act-III areas; no source or ownership fabricated");
    const auto destination=step.searchAreas[questState.questRun.materialPhase];
    if(area!=destination) {travel(c,destination);return false;}
    // A pure search can approach an assigned leader outside attack range.
    // On another quest only an on-route engagement or a real drop interrupts.
    const OnlineUnit *candidate=nullptr;
    float nearest=std::numeric_limits<float>::max();
    if(policy.enabled) for(const auto &[key,unit]:c.observation.read().world.units) {
        if(!unit.position || !c.combat.jadeFigurineCandidate(unit) || !c.combat.hostile(unit) ||
            !c.combat.monsterTargetEligible(unit,false) || battle.bypassedThreats.contains({key,unit.assignmentRevision})) continue;
        const auto distance=(Vec{float(unit.position->x),float(unit.position->y)}-c.position).length();
        if(distance<nearest) {nearest=distance;candidate=&unit;}
    }
    if(candidate && approachEnemy(c,*candidate,"observed figurine candidate")) {
        questState.questRun.sweepReady={};return false;
    }
    explore(c,"eligible elites for "+step.item);
    if(phase=="explore-wait" && !c.walk.active()) {
        if(questState.questRun.sweepReady==Clock::time_point{}) questState.questRun.sweepReady=now+std::chrono::seconds(5);
        if(now>=questState.questRun.sweepReady) {
            ++questState.questRun.materialPhase;questState.questRun.sweepReady={};cancelWalk(c);
            navigation.frontierRoute.reset();
            note("quest-drop-search-next-area",std::to_string(destination));
        }
    } else questState.questRun.sweepReady={};
    return false;
}
bool DebugAi::Impl::questEquipmentRestored(const Context &c) const {
    const auto &view=c.observation.read();
    for(size_t hand=0;hand<questState.questHandsRestore.size();++hand) {
        if(!questState.questHandsRestore[hand]) continue;
        const auto item=view.world.items.find(*questState.questHandsRestore[hand]);
        if(item==view.world.items.end() || !onlineItemOwnedBy(item->second,view.load.playerUnitId) ||
            item->second.mode!=1 || item->second.body!=4+hand) return false;
    }
    return true;
}
bool DebugAi::Impl::questEquipWeapon(Context &c,const QuestStep &step) {
    const auto &view=c.observation.read();const auto &world=view.world;
    const auto ours=[&](const OnlineItem &item){return onlineItemOwnedBy(item,view.load.playerUnitId);};
    if(questState.questRun.pending) {
        checkQuestPending(c);
        const auto &request=world.itemRequest;
        if(!request || request->sequence!=questState.questRun.pending->sequence) return false;
        const auto wire=world.items.find(questState.questRun.pending->item);
        const auto action=request->command.action;
        const bool observed=wire!=world.items.end() && ours(wire->second) &&
            ((action==OnlineItemAction::Take || action==OnlineItemAction::Unequip)?wire->second.mode==4:
             action==OnlineItemAction::Equip?wire->second.mode==1 && wire->second.body==request->command.body:
             action==OnlineItemAction::Place?wire->second.mode==0 && wire->second.page==1:false);
        if(!observed) {wait("quest-equipment-result","Waiting for actual cursor / active-hand / backpack placement");return false;}
        questState.questRun.pending.reset();
    }
    const OnlineItem *cursor=nullptr,*wanted=nullptr;
    std::array<const OnlineItem *,2> equipped{};
    for(const auto &[id,item]:world.items) if(ours(item)) {
        (void)id;if(item.mode==4) cursor=&item;
        if(item.mode==1 && (item.body==4 || item.body==5)) equipped[item.body-4]=&item;
        if(!step.item.empty() && item.code==step.item) wanted=&item;
    }
    size_t hand=0;
    if(step.item.empty()) {
        for(;hand<equipped.size();++hand) {
            if(!questState.questHandsRestore[hand] || (equipped[hand] && equipped[hand]->id==*questState.questHandsRestore[hand])) continue;
            const auto item=world.items.find(*questState.questHandsRestore[hand]);
            if(item!=world.items.end() && ours(item->second)) wanted=&item->second;
            break;
        }
        if(hand==equipped.size()) {
            if(cursor) {questStoreCursor(c,cursor->id);return false;}
            questState.questHandsRestore={};return true;
        }
    }
    if(!wanted) throw std::runtime_error("Quest weapon / original hand item is no longer actually owned");
    if(wanted==equipped[hand]) {
        if(cursor) {questStoreCursor(c,cursor->id);return false;}
        return true;
    }
    if(!step.item.empty() && !questState.questRun.materialPhase) {
        for(size_t slot=0;slot<equipped.size();++slot)
            questState.questHandsRestore[slot]=equipped[slot]?std::optional{equipped[slot]->id}:std::nullopt;
    }
    questState.questRun.materialPhase=1;
    if(cursor && cursor->id!=wanted->id) {questStoreCursor(c,cursor->id);return false;}
    OnlineItemCommand command;
    if(equipped[hand]) {
        if(cursor) {questStoreCursor(c,cursor->id);return false;}
        command.action=OnlineItemAction::Unequip;command.item=equipped[hand]->id;command.itemRevision=equipped[hand]->revision;
    } else if(wanted->mode==4) {
        command.action=OnlineItemAction::Equip;command.item=wanted->id;command.itemRevision=wanted->revision;command.body=uint8_t(4+hand);
    } else if(wanted->mode==0 && wanted->page==1) {
        command.action=OnlineItemAction::Take;command.item=wanted->id;command.itemRevision=wanted->revision;
    } else throw std::runtime_error("Quest weapon must be accessible in the backpack, cursor or active hand");
    questItemRequest(c,command,QuestPending::Action::Take,"quest-equipment-submitted");return false;
}
bool DebugAi::Impl::questSmashOrb(Context &c,const QuestStep &step) {
    const auto flags=questFlags(c,QuestId::KhalimsWill);
    if(flags && (*flags&1) && !owned(c,step.item)) return true;
    const auto now=Clock::now();
    if(questState.questRun.materialPhase>=2) {
        if(!questState.questRun.pending) {
            QuestPending pending;pending.action=QuestPending::Action::Object;
            pending.deadline=now+std::chrono::seconds(20);questState.questRun.pending=pending;
        }
        if(!questState.questRun.pending->statusRequested && c.control.refreshQuests())
            questState.questRun.pending->statusRequested=true;
        wait("quest-orb-result","Two original Operate53 clicks submitted; waiting for actual Will consumption and private reward");
        checkQuestPending(c);return false;
    }
    if(now<questState.questRun.sweepReady) {wait("quest-orb-input","Waiting between the two original orb operations");return false;}
    if(!step.target) throw std::runtime_error("Compelling Orb identity is missing");
    const auto orb=find(c,*step.target,true);
    if(!orb) {if(!approachKnown(c,*step.target)) explore(c,"the actual Compelling Orb");return false;}
    if(!settleItemInput(c)) return false;
    const DebugWalkIntent intent{.goal=DebugWalkGoal::Unit,.unit=OnlineUnitKey{orb->key.type,orb->key.id},.interact=true,.purpose="quest-orb"};
    if(walk(c,intent)) observeQuestInteraction(c,intent);
    return false;
}
} // namespace d2x

namespace d2x {
bool DebugAi::Impl::Definitions::itemHasType(std::string_view code,std::string_view wanted) const {
    std::vector<std::string> pending;std::set<std::string> visited;
    for(const auto *table:{&misc,&weapons,&armor}) for(size_t row=0;row<table->rows().size();++row)
        if(table->value(row,"code")==code) pending.push_back(std::string(table->value(row,"type")));
    while(!pending.empty()) {
        auto type=std::move(pending.back());pending.pop_back();
        if(type==wanted) return true;
        if(type.empty() || !visited.insert(type).second) continue;
        for(size_t row=0;row<types.rows().size();++row) if(types.value(row,"Code")==type)
            for(const auto field:{"Equiv1","Equiv2"}) if(!types.value(row,field).empty()) pending.push_back(std::string(types.value(row,field)));
    }
    return false;
}
bool DebugAi::Impl::questSmashForge(Context &c,const QuestStep &step) {
    const auto now=Clock::now();const auto flags=questFlags(c,QuestId::HellsForge);
    const auto forge=step.target?find(c,*step.target,true):std::nullopt;
    if(!questState.questRun.forgeStrikingStopped && ((flags && (*flags&3)) ||
        (forge && (forge->mode==3 || forge->mode==4)) ||
        (questState.questRun.materialPhase && !owned(c,"hfh")))) {
        questState.questRun.forgeStrikingStopped=true;cancelWalk(c);
        note("quest-forge-striking-stopped","Observed smash mode, hammer removal or personal completion");
    }
    if(questState.questRun.forgeStrikingStopped && !questState.questRun.pending) {
        QuestPending pending;pending.action=QuestPending::Action::Object;
        if(forge) {pending.unit=forge->key;pending.assignment=forge->assignmentRevision;}
        pending.deadline=now+std::chrono::seconds(40);questState.questRun.pending=pending;
    }
    if(questState.questRun.materialPhase>=3 || questState.questRun.forgeStrikingStopped) {
        if(!questState.questRun.pending) throw std::runtime_error("Forge hit sequence lost its submitted-result contract");
        unsigned gems=0,runes=0;
        const auto object=step.target?find(c,*step.target,true):std::nullopt;
        for(const auto &[id,item]:c.observation.read().world.items) {
            if(questState.questRun.rewardBefore.contains(id) || item.mode!=3 || !item.positionKnown || !object || !object->position) continue;
            if((Vec{float(item.groundX),float(item.groundY)}-Vec{float(object->position->x),float(object->position->y)}).length()>24) continue;
            gems+=definitions->itemHasType(item.code,"gem");runes+=definitions->itemHasType(item.code,"rune");
        }
        if(flags && (*flags&3) && !owned(c,"hfh") && gems>=4 && runes>=1) {
            note("quest-forge-drops-observed",std::to_string(gems)+" gems / "+std::to_string(runes)+" runes");return true;
        }
        if(now>=questState.questRun.pending->deadline) throw std::runtime_error("Forge result incomplete: pending/reward flag, hammer removal and four gems / one rune were not all observed");
        if(!questState.questRun.pending->statusRequested && c.control.refreshQuests()) questState.questRun.pending->statusRequested=true;
        if(questState.questRun.forgeStrikingStopped || questState.questRun.materialPhase>=6 ||
            now<questState.questRun.sweepReady || !forge ||
            questState.questRun.pending->unit!=forge->key || forge->assignmentRevision!=questState.questRun.pending->assignment) {
            wait("quest-forge-result","Observing actual hammer consumption and the native sequential reward drops");return false;
        }
    }
    if(now<questState.questRun.sweepReady) {wait("quest-forge-input","Waiting between native hammer operations");return false;}
    if(!forge) {if(!step.target || !approachKnown(c,*step.target)) explore(c,"actual Hellforge");return false;}
    const bool equipped=std::any_of(c.observation.read().world.items.begin(),c.observation.read().world.items.end(),[&](const auto &entry) {
        const auto &item=entry.second;return item.code=="hfh" && item.mode==1 && item.body==4 &&
            onlineItemOwnedBy(item,c.observation.read().load.playerUnitId);
    });
    if(!equipped) {wait("quest-forge-equipment","Waiting for the actual equipped quest hammer before striking");return false;}
    int mode=forge->mode.value_or(0);
    const auto milliseconds=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
    const float age=forge->actionReceivedMilliseconds && milliseconds>=forge->actionReceivedMilliseconds?
        float(milliseconds-forge->actionReceivedMilliseconds)/1000.f:0.f;
    for(size_t row=0;row<definitions->objects.rows().size();++row)
        if(definitions->objects.number(row,"Id")==forge->classId) {mode=objectDisplayMode(definitions->objects,row,mode,age);break;}
    if(mode!=2) {wait("quest-forge-animation","Waiting for the original client ENDANIM before another hammer operation");return false;}
    if(!settleItemInput(c)) return false;
    const DebugWalkIntent intent{.goal=DebugWalkGoal::Unit,.unit=OnlineUnitKey{forge->key.type,forge->key.id},.interact=true,.purpose="quest-forge"};
    if(walk(c,intent)) observeQuestInteraction(c,intent);
    return false;
}
} // namespace d2x
