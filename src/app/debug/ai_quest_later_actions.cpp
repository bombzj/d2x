#include "ai_impl.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
bool DebugAi::Impl::questSearchDrop(Context &c,const QuestStep &step) {
    if(owned(c,step.item)) return true;
    const auto &world=c.session.read().world;const auto now=Clock::now();
    const bool ground=std::any_of(world.items.begin(),world.items.end(),[&](const auto &entry) {
        return entry.second.code==step.item && entry.second.mode==3 && entry.second.positionKnown;
    });
    Goal leaf;leaf.kind=Kind::Acquire;leaf.item=step.item;
    if(ground || pickupIssued) return acquireItem(c,leaf);
    if(step.witness) {
        const auto source=find(c,*step.witness,true);
        const auto point=source?source->position:area?c.town.knownTargetPosition(*area,step.witness->type,step.witness->identity):std::nullopt;
        if(!point) {explore(c,"the observed quest drop source");return false;}
        if((Vec{float(point->x),float(point->y)}-c.position).length()>12) {
            approachPoint(c,*point,"timed quest drop source",10);return false;
        }
        if(questRun.sweepReady==Clock::time_point{}) questRun.sweepReady=now+std::chrono::seconds(90);
        if(now>=questRun.sweepReady) throw std::runtime_error("Timed quest source produced no observed drop; retaining source and scene");
        cancelWalk(c);wait("quest-drop-spawn","Holding the actual altar's area while normal combat handles its timed monster");return false;
    }
    if(step.searchAreas.empty()) throw std::runtime_error("Quest drop has no verified search areas");
    if(questRun.materialPhase>=step.searchAreas.size())
        throw std::runtime_error("No quest drop observed after the configured normal Act-III areas; no source or ownership fabricated");
    const auto destination=step.searchAreas[questRun.materialPhase];
    if(area!=destination) {travel(c,destination);return false;}
    acquireItem(c,leaf);
    if(phase=="explore-wait" && !c.walk.active()) {
        if(questRun.sweepReady==Clock::time_point{}) questRun.sweepReady=now+std::chrono::seconds(5);
        if(now>=questRun.sweepReady) {
            ++questRun.materialPhase;questRun.sweepReady={};cancelWalk(c);
            visited.clear();unavailableRooms.clear();backtrackAttempts.clear();
            note("quest-drop-search-next-area",std::to_string(destination));
        }
    } else questRun.sweepReady={};
    return false;
}
bool DebugAi::Impl::questEquipWeapon(Context &c,const QuestStep &step) {
    const auto &view=c.session.read();const auto &world=view.world;
    const auto ours=[&](const OnlineItem &item){return onlineItemOwnedBy(item,view.load.playerUnitId);};
    const OnlineItem *wanted=nullptr;
    for(const auto &[id,item]:world.items) if(ours(item) &&
        (step.item.empty()?questWeaponRestore && id==*questWeaponRestore:item.code==step.item)) {wanted=&item;break;}
    if(step.item.empty() && !questWeaponRestore) return true;
    if(!wanted) throw std::runtime_error("Quest weapon / original weapon is no longer actually owned");
    if(questRun.pending) {
        checkQuestPending(c);
        const auto &request=world.itemRequest;
        if(!request || request->sequence!=questRun.pending->sequence) return false;
        const auto wire=world.items.find(questRun.pending->item);
        const auto action=request->command.action;
        const bool observed=wire!=world.items.end() && ours(wire->second) &&
            ((action==OnlineItemAction::Take || action==OnlineItemAction::Unequip)?wire->second.mode==4:
             action==OnlineItemAction::Equip?wire->second.mode==1 && wire->second.body==4:
             action==OnlineItemAction::Place?wire->second.mode==0 && wire->second.page==1:false);
        if(!observed) {wait("quest-equipment-result","Waiting for actual cursor / active-hand / backpack placement");return false;}
        questRun.pending.reset();
    }
    if(wanted->mode==1 && wanted->body==4) {
        if(step.item.empty()) questWeaponRestore.reset();
        return true;
    }
    questRun.materialPhase=1;
    const OnlineItem *cursor=nullptr,*weapon=nullptr;
    for(const auto &[id,item]:world.items) if(ours(item)) {
        (void)id;if(item.mode==4) cursor=&item;
        if(item.mode==1 && item.body==4) weapon=&item;
    }
    if(cursor && cursor->id!=wanted->id) {questStoreCursor(c,cursor->id);return false;}
    OnlineItemCommand command;
    if(weapon) {
        if(!step.item.empty() && !questWeaponRestore) questWeaponRestore=weapon->id;
        command.action=OnlineItemAction::Unequip;command.item=weapon->id;command.itemRevision=weapon->revision;
    } else if(wanted->mode==4) {
        command.action=OnlineItemAction::Equip;command.item=wanted->id;command.itemRevision=wanted->revision;command.body=4;
    } else if(wanted->mode==0 && wanted->page==1) {
        command.action=OnlineItemAction::Take;command.item=wanted->id;command.itemRevision=wanted->revision;
    } else throw std::runtime_error("Quest weapon must be accessible in the backpack, cursor or active hand");
    questItemRequest(c,command,QuestPending::Action::Take,"quest-equipment-submitted");return false;
}
bool DebugAi::Impl::questSmashOrb(Context &c,const QuestStep &step) {
    const auto flags=questFlags(c,QuestId::KhalimsWill);
    if(flags && (*flags&1) && !owned(c,step.item)) return true;
    const auto now=Clock::now();
    if(questRun.materialPhase>=2) {
        if(!questRun.pending) {
            QuestPending pending;pending.action=QuestPending::Action::Object;
            pending.deadline=now+std::chrono::seconds(20);questRun.pending=pending;
        }
        if(!questRun.pending->statusRequested && c.session.request_quests(c.session.read().gameGeneration))
            questRun.pending->statusRequested=true;
        wait("quest-orb-result","Two original Operate53 clicks submitted; waiting for actual Will consumption and private reward");
        checkQuestPending(c);return false;
    }
    if(now<questRun.sweepReady) {wait("quest-orb-input","Waiting between the two original orb operations");return false;}
    if(!step.target) throw std::runtime_error("Compelling Orb identity is missing");
    const auto orb=find(c,*step.target,true);
    if(!orb) {if(!approachKnown(c,*step.target)) explore(c,"the actual Compelling Orb");return false;}
    if(!settleItemInput(c)) return false;
    const Json intent{{"goal","unit"},{"unitType",orb->key.type},{"unitId",orb->key.id},{"interact",true},{"purpose","quest-orb"}};
    if(walk(c,intent)) observeQuestInteraction(c,intent);
    return false;
}
} // namespace d2x
