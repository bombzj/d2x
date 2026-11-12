#include "ai_impl.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
bool DebugAi::Impl::recoverQuestInventory(Context &c,const QuestStep *step) {
    const auto &view=c.observation.read();const auto &world=view.world;
    if(questState.questRun.inventoryWork.active() && !serviceInventoryWork(c)) return false;
    if(questState.questRun.pending) {
        const auto action=questState.questRun.pending->action;
        const bool recovery=action==QuestPending::Action::RecoverOpen || action==QuestPending::Action::RecoverTake ||
            action==QuestPending::Action::RecoverPlace || action==QuestPending::Action::RecoverClose;
        if(!recovery) return true;
        if(!questState.questRun.pending->resultObserved) {
            wait("quest-inventory-result","Waiting for the actual recovery operation; no item replay");checkQuestPending(c);return false;
        }
        questState.questRun.pending.reset();
    }
    if(!questState.questRun.recoveringItem) {
        std::vector<std::string> codes;
        if(step) {
            if(step->action==QuestAction::Combine) {codes={step->produced,step->item};codes.insert(codes.end(),step->ingredients.begin(),step->ingredients.end());}
            else if(step->action==QuestAction::Talk || step->action==QuestAction::UseItem || step->action==QuestAction::ReadScroll ||
                step->action==QuestAction::SubmitStaff || step->action==QuestAction::EquipWeapon || step->action==QuestAction::Operate)
                codes.push_back(step->item);
        }
        for(const auto &[id,item]:world.items) {
            if(!onlineItemOwnedBy(item,view.load.playerUnitId)) continue;
            // A new plan first stores its real Cursor. During a transaction
            // the registered action retains the cursor and its own receipt.
            if(item.mode==4 && !questState.questRun.initialized) {questState.questRun.recoveringItem=id;break;}
            if(!step || item.code.empty() || std::find(codes.begin(),codes.end(),item.code)==codes.end()) continue;
            const bool cubeHandled=step->action==QuestAction::Combine;
            if(item.mode==0 && (item.page==5 || (item.page==4 && !cubeHandled))) {
                questState.questRun.recoveringItem=id;break;
            }
        }
        if(!questState.questRun.recoveringItem) {
            if(questState.questRun.initialInventory && (world.storage.kind!=OnlineStorageKind::None || world.storage.requested!=OnlineStorageKind::None)) {
                OnlineItemCommand close;close.action=OnlineItemAction::StorageClose;
                questItemRequest(c,close,QuestPending::Action::RecoverClose,"quest-initial-storage-close-submitted");return false;
            }
            questState.questRun.initialInventory=false;return true;
        }
        // If the needed item is inside a cube that is itself in the stash,
        // retrieve the cube first, then re-evaluate this same action's needs.
        if(world.items.at(*questState.questRun.recoveringItem).page==4)
            for(const auto &[id,item]:world.items) if(item.code=="box" && item.mode==0 && item.page==5 && onlineItemOwnedBy(item,view.load.playerUnitId)) {
                questState.questRun.recoveringItem=id;break;
            }
        note("quest-inventory-recovery",std::to_string(*questState.questRun.recoveringItem));
    }
    const auto found=world.items.find(*questState.questRun.recoveringItem);
    if(found==world.items.end() || !onlineItemOwnedBy(found->second,view.load.playerUnitId))
        throw std::runtime_error("Quest recovery item ownership became unknown; no replacement or replay");
    const auto &item=found->second;
    if(item.mode==0 && item.page==1) {
        if(world.storage.kind!=OnlineStorageKind::None || world.storage.requested!=OnlineStorageKind::None) {
            OnlineItemCommand close;close.action=OnlineItemAction::StorageClose;
            questItemRequest(c,close,QuestPending::Action::RecoverClose,"quest-recovery-close-submitted");return false;
        }
        note("quest-inventory-recovered",std::to_string(item.id));questState.questRun.recoveringItem.reset();questState.questRun.initialInventory=false;return true;
    }
    if(item.mode==4) {
        if(!questStoreCursor(c,item.id) && questState.questRun.pending && questState.questRun.pending->action==QuestPending::Action::Place)
            questState.questRun.pending->action=QuestPending::Action::RecoverPlace;
        return false;
    }
    if(item.mode!=0 || (item.page!=4 && item.page!=5))
        throw std::runtime_error("Quest item is outside recoverable personal backpack/cube/stash/Cursor");
    const auto required=item.page==5?OnlineStorageKind::Stash:OnlineStorageKind::Cube;
    if(world.storage.kind!=required) {
        if(world.storage.kind!=OnlineStorageKind::None || world.storage.requested!=OnlineStorageKind::None) {
            OnlineItemCommand close;close.action=OnlineItemAction::StorageClose;
            questItemRequest(c,close,QuestPending::Action::RecoverClose,"quest-recovery-close-submitted");return false;
        }
        if(required==OnlineStorageKind::Stash) {
            if(!reachTown(c)) return false;
            const auto stash=std::find_if(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),
                [&](const auto &target){return target.interaction==OnlineMapInteraction::Stash && world.units.contains(target.unit);});
            if(stash==c.town.read().mapTargets.end()) {explore(c,"actual personal stash for an owned quest item");return false;}
            walk(c,{.goal=DebugWalkGoal::Unit,.unit=OnlineUnitKey{stash->unit.type,stash->unit.id},.interact=true,.purpose="quest-recover-stash"});
        } else {
            const auto cube=accessibleItem(c,"box");
            if(!cube || cube->mode!=0 || cube->page!=1) throw std::runtime_error("Owned cube is not accessible for quest-item recovery");
            OnlineItemCommand open;open.action=OnlineItemAction::CubeOpen;open.item=cube->id;open.itemRevision=cube->revision;
            questItemRequest(c,open,QuestPending::Action::RecoverOpen,"quest-recovery-cube-submitted");
        }
        return false;
    }
    if(!settleItemInput(c)) return false;
    OnlineItemCommand take;take.action=OnlineItemAction::Take;take.item=item.id;take.itemRevision=item.revision;
    questItemRequest(c,take,QuestPending::Action::RecoverTake,"quest-recovery-take-submitted");return false;
}
bool DebugAi::Impl::questItemRequest(Context &c,OnlineItemCommand command,QuestPending::Action action,std::string event) {
    if(command.action!=OnlineItemAction::Use && !settleItemInput(c)) return false;
    const uint32_t item=command.item;
    if(!submitItem(c,command,"quest-item-request")) return false;
    QuestPending p;p.action=action;p.item=item;
    if(c.observation.read().world.itemRequest) p.sequence=c.observation.read().world.itemRequest->sequence;
    p.deadline=Clock::now()+std::chrono::seconds(20);questState.questRun.pending=p;
    cancelWalk(c);note(std::move(event),std::to_string(item));return true;
}
bool DebugAi::Impl::questUseItem(Context &c,const QuestStep &step) {
    const auto &world=c.observation.read().world;
    const auto points=world.playerAttributes.find(5);
    const uint32_t available=points==world.playerAttributes.end()?0:points->second;
    if(questState.questRun.pending) {
        if(step.item=="tr2" && !owned(c,step.item)) {
            if(const auto flags=questFlags(c,QuestId::PrisonOfIce);flags && (*flags&0x80)) {
                note("quest-resistance-scroll-consumed","Actual scroll removal and permanent resistance quest bit observed");return true;
            }
            if(!questState.questRun.pending->statusRequested && c.control.refreshQuests()) questState.questRun.pending->statusRequested=true;
        }
        if(step.item=="xyz" && !owned(c,step.item)) {
            const auto flags=questFlags(c,QuestId::GoldenBird);
            if(!questState.questRun.pending->statusRequested) {
                if(!c.control.refreshQuests()) return false;
                questState.questRun.pending->statusRequested=true;
            }
            if(flags && (*flags&1) && !(*flags&0x20)) {
                note("quest-life-potion-consumed","Actual item removal and cleared native use-pending flag");return true;
            }
        }
        if(step.item=="ass" && !owned(c,step.item) && questState.questRun.skillPointsBefore && uint64_t(available)>=uint64_t(*questState.questRun.skillPointsBefore)+1) {
            // The native skill point/item results can precede the next28.
            // Keep this observed irreversible postcondition in the graph and
            // refresh the private record, never acquire/use another book.
            if(step.item=="ass" && !questState.questRun.pending->statusRequested) {
                if(!c.control.refreshQuests()) return false;
                questState.questRun.pending->statusRequested=true;
                note("quest-status-requested","Confirmed skill-book consumption; refreshing its private record");
            }
            note("quest-skill-book-consumed","Actual item removed and permanent skill point increment observed");return true;
        }
        wait("quest-use-result","Waiting for actual quest-item consumption and its permanent reward");checkQuestPending(c);return false;
    }
    const auto item=accessibleItem(c,step.item);
    if(!item || item->mode!=0 || item->page!=1) throw std::runtime_error("Quest consumable must be in the actual backpack before use");
    questState.questRun.skillPointsBefore=available;
    OnlineItemCommand use;use.action=OnlineItemAction::Use;use.item=item->id;use.itemRevision=item->revision;
    questItemRequest(c,use,QuestPending::Action::Use,"quest-item-use-submitted");return false;
}
bool DebugAi::Impl::serviceInventoryWork(Context &c) {
    auto &work=questState.questRun.inventoryWork;
    const auto command=work.next(c.observation.read(),c.inventory.read());
    if(work.done()) return true;
    if(!command) {wait("inventory-workflow-result",work.reason());return false;}
    if(!settleItemInput(c)) return false;
    const auto &before=c.observation.read().world.itemRequest;
    const uint64_t previousSequence=before?before->sequence:0;
    if(!submitItem(c,*command,"inventory-workflow")) return false;
    const auto &receipt=c.observation.read().world.itemRequest;
    work.submitted(receipt,previousSequence);cancelWalk(c);
    note("inventory-workflow-submitted",Json{{"action",unsigned(command->action)},{"item",command->item},
        {"target",command->target},{"cell",{command->x,command->y}},{"sequence",receipt->sequence}}.dump());
    return false;
}
bool DebugAi::Impl::questCombine(Context &c,const QuestStep &step) {
    if(!c.town.read().town) throw std::runtime_error("Quest transmutation requires a town destination");
    if(c.observation.read().world.npcRequested) {c.control.closeNpc();return false;}
    auto &work=questState.questRun.inventoryWork;
    if(!work.started() || work.kind()!=InventoryWorkflow::Kind::Combine)
        work.combine(step.produced,step.item,step.ingredients,c.observation.read(),{{"tsc","isc"}});
    if(!serviceInventoryWork(c)) return false;
    note("quest-cube-output-observed",step.item);return true;
}
bool DebugAi::Impl::questSubmitStaff(Context &c,const QuestStep &step) {
    const auto &world=c.observation.read().world;
    const auto flags=questFlags(c,QuestId::HoradricStaff);
    if(flags && (*flags&1) && !owned(c,step.item)) {
        note("quest-staff-consumed","Actual staff submission flag and item removal observed");return true;
    }
    if(questState.questRun.pending) {
        auto &p=*questState.questRun.pending;
        if(p.action==QuestPending::Action::Object && p.unit && world.staffSource==p.unit->id) {
            questState.questRun.materialNpc=p.unit;questState.questRun.pending.reset();
        } else if(p.action==QuestPending::Action::Take) {
            const auto item=accessibleItem(c,step.item);
            if(item && item->mode==4) questState.questRun.pending.reset();
            else {wait("quest-staff-cursor","Waiting for the real staff on Cursor");checkQuestPending(c);return false;}
        } else {
            if(p.action==QuestPending::Action::SubmitStaff && world.staffRevision>p.beforeRevision && world.staffResult==4)
                throw std::runtime_error("The original orifice rejected this staff");
            wait("quest-staff-result","Waiting for the native staff lease/submission result");checkQuestPending(c);return false;
        }
    }
    const auto item=accessibleItem(c,step.item);
    if(!item) throw std::runtime_error("Horadric staff is not accessible for actual submission");
    if(!questState.questRun.materialNpc || world.staffSource!=questState.questRun.materialNpc->id) {
        if(item->mode==4) throw std::runtime_error("Staff lease ended with the staff on Cursor; restore it before retrying");
        const auto unit=find(c,*step.target,true);
        if(!unit) {if(!approachKnown(c,*step.target)) explore(c,"actual Horadric orifice");return false;}
        const DebugWalkIntent intent{.goal=DebugWalkGoal::Unit,.unit=OnlineUnitKey{unit->key.type,unit->key.id},.interact=true,.purpose="quest-object"};
        if(walk(c,intent)) observeQuestInteraction(c,intent);
        return false;
    }
    if(item->mode==0 && item->page==1) {
        OnlineItemCommand take;take.action=OnlineItemAction::Take;take.item=item->id;take.itemRevision=item->revision;
        questItemRequest(c,take,QuestPending::Action::Take,"quest-staff-take-submitted");return false;
    }
    if(item->mode!=4) throw std::runtime_error("Staff submission requires the actual completed staff on Cursor");
    const auto before=world.staffRevision;
    if(!c.supplies->submitStaff || !c.supplies->submitStaff(*world.staffSource,item->id,item->revision)) {
        wait("quest-staff-submit","The client did not accept the staff submission");return false;
    }
    QuestPending p;p.action=QuestPending::Action::SubmitStaff;p.item=item->id;p.beforeRevision=before;
    p.deadline=Clock::now()+std::chrono::seconds(30);questState.questRun.pending=p;
    note("quest-staff-submit-submitted",std::to_string(item->id));return false;
}
}
