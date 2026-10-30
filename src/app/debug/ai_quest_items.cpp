#include "ai_impl.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
bool DebugAi::Impl::questItemRequest(Context &c,OnlineItemCommand command,QuestPending::Action action,std::string event) {
    if(command.action!=OnlineItemAction::Use && !settleItemInput(c)) return false;
    const uint32_t item=command.item;
    if(!c.inventory.submit(c.session,command)) {wait("quest-item-request",c.inventory.reason());return false;}
    QuestPending p;p.action=action;p.item=item;
    if(c.session.read().world.itemRequest) p.sequence=c.session.read().world.itemRequest->sequence;
    p.deadline=Clock::now()+std::chrono::seconds(20);questRun.pending=p;
    cancelWalk(c);note(std::move(event),std::to_string(item));return true;
}
bool DebugAi::Impl::questUseItem(Context &c,const QuestStep &step) {
    const auto &world=c.session.read().world;
    const auto points=world.playerAttributes.find(5);
    const uint32_t available=points==world.playerAttributes.end()?0:points->second;
    if(questRun.pending) {
        if(!owned(c,step.item) && questRun.skillPointsBefore && uint64_t(available)>=uint64_t(*questRun.skillPointsBefore)+1) {
            note("quest-skill-book-consumed","Actual item removed and permanent skill point increment observed");return true;
        }
        wait("quest-use-result","Waiting for actual quest-item consumption and its permanent reward");checkQuestPending(c);return false;
    }
    const auto item=accessibleItem(c,step.item);
    if(!item || item->mode!=0 || item->page!=1) throw std::runtime_error("Quest skill book must be in the actual backpack before use");
    questRun.skillPointsBefore=available;
    OnlineItemCommand use;use.action=OnlineItemAction::Use;use.item=item->id;use.itemRevision=item->revision;
    questItemRequest(c,use,QuestPending::Action::Use,"quest-item-use-submitted");return false;
}
bool DebugAi::Impl::questCombine(Context &c,const QuestStep &step) {
    const auto &view=c.session.read();const auto &world=view.world;
    const auto output=accessibleItem(c,step.item);
    const auto cube=accessibleItem(c,step.produced);
    if(questRun.pending) {
        const auto &p=*questRun.pending;
        const auto wire=world.items.find(p.item);
        bool observed=false;
        switch(p.action) {
        case QuestPending::Action::OpenCube:observed=cube && world.storage.kind==OnlineStorageKind::Cube && world.storage.source==cube->id;break;
        case QuestPending::Action::CloseCube:observed=world.storage.kind==OnlineStorageKind::None && world.storage.requested==OnlineStorageKind::None;break;
        case QuestPending::Action::Take:observed=wire!=world.items.end() && wire->second.mode==4;break;
        case QuestPending::Action::StoreCube:observed=wire!=world.items.end() && wire->second.mode==0 && wire->second.page==4;break;
        case QuestPending::Action::Place:observed=wire!=world.items.end() && wire->second.mode==0 && wire->second.page==1;break;
        case QuestPending::Action::Transmute:observed=output && !owned(c,"msf") && !owned(c,"vip");break;
        default:break;
        }
        if(!observed) {wait("quest-cube-result","Waiting for the actual cube/item operation; no replay");checkQuestPending(c);return false;}
        questRun.pending.reset();
    }
    if(output && output->mode==0 && output->page==1) {
        if(world.storage.kind!=OnlineStorageKind::None || world.storage.requested!=OnlineStorageKind::None) {
            OnlineItemCommand close;close.action=OnlineItemAction::StorageClose;
            questItemRequest(c,close,QuestPending::Action::CloseCube,"quest-cube-close-submitted");return false;
        }
        note("quest-cube-output-observed",step.item);return true;
    }
    if(world.npcRequested) {c.session.close_npc();return false;}
    if(output && output->mode==4) return questStoreCursor(c,output->id);
    if(!cube || cube->mode!=0 || cube->page!=1) throw std::runtime_error("Quest transmutation needs the actual backpack cube");
    if(world.storage.kind!=OnlineStorageKind::Cube) {
        OnlineItemCommand open;open.action=OnlineItemAction::CubeOpen;open.item=cube->id;open.itemRevision=cube->revision;
        questItemRequest(c,open,QuestPending::Action::OpenCube,"quest-cube-open-submitted");return false;
    }
    if(world.storage.source!=cube->id) throw std::runtime_error("Quest cube context belongs to a different item");
    if(output) {
        if(output->mode!=0 || output->page!=4) throw std::runtime_error("Quest cube output is outside accessible storage");
        OnlineItemCommand take;take.action=OnlineItemAction::Take;take.item=output->id;take.itemRevision=output->revision;
        questItemRequest(c,take,QuestPending::Action::Take,"quest-cube-output-take-submitted");return false;
    }
    // Refuse unrelated contents instead of risking another recipe or consuming
    // the player's possessions. Recipe validity and output remain MPQ/host-owned.
    for(const auto &[id,item]:world.items) {
        (void)id;
        if(onlineItemOwnedBy(item,view.load.playerUnitId) && item.mode==0 && item.page==4 && item.code!="msf" && item.code!="vip")
            throw std::runtime_error("Quest cube contains unrelated items; clear it before running this recipe");
    }
    for(const auto code:{"msf","vip"}) {
        const auto item=accessibleItem(c,code);
        if(!item) throw std::runtime_error("Quest transmutation lacks the actual ingredient: "+std::string(code));
        if(item->mode==0 && item->page==4) continue;
        OnlineItemCommand move;move.item=item->id;move.itemRevision=item->revision;
        if(item->mode==4) {
            move.action=OnlineItemAction::CubeStore;move.target=cube->id;move.targetRevision=cube->revision;
            questItemRequest(c,move,QuestPending::Action::StoreCube,"quest-cube-ingredient-store-submitted");
        } else if(item->mode==0 && item->page==1) {
            move.action=OnlineItemAction::Take;
            questItemRequest(c,move,QuestPending::Action::Take,"quest-cube-ingredient-take-submitted");
        } else throw std::runtime_error("Quest ingredient must be in backpack, cube or Cursor");
        return false;
    }
    OnlineItemCommand transmute;transmute.action=OnlineItemAction::Transmute;
    questItemRequest(c,transmute,QuestPending::Action::Transmute,"quest-cube-transmute-submitted");return false;
}
bool DebugAi::Impl::questSubmitStaff(Context &c,const QuestStep &step) {
    const auto &world=c.session.read().world;
    const auto flags=questFlags(c,QuestId::HoradricStaff);
    if(flags && (*flags&1) && !owned(c,step.item)) {
        note("quest-staff-consumed","Actual staff submission flag and item removal observed");return true;
    }
    if(questRun.pending) {
        auto &p=*questRun.pending;
        if(p.action==QuestPending::Action::Object && p.unit && world.staffSource==p.unit->id) {
            questRun.materialNpc=p.unit;questRun.pending.reset();
        } else if(p.action==QuestPending::Action::Take) {
            const auto item=accessibleItem(c,step.item);
            if(item && item->mode==4) questRun.pending.reset();
            else {wait("quest-staff-cursor","Waiting for the real staff on Cursor");checkQuestPending(c);return false;}
        } else {
            if(p.action==QuestPending::Action::SubmitStaff && world.staffRevision>p.beforeRevision && world.staffResult==4)
                throw std::runtime_error("The original orifice rejected this staff");
            wait("quest-staff-result","Waiting for the native staff lease/submission result");checkQuestPending(c);return false;
        }
    }
    const auto item=accessibleItem(c,step.item);
    if(!item) throw std::runtime_error("Horadric staff is not accessible for actual submission");
    if(!questRun.materialNpc || world.staffSource!=questRun.materialNpc->id) {
        if(item->mode==4) throw std::runtime_error("Staff lease ended with the staff on Cursor; restore it before retrying");
        const auto unit=find(c,*step.target,true);
        if(!unit) {if(!approachKnown(c,*step.target)) explore(c,"actual Horadric orifice");return false;}
        const Json intent{{"goal","unit"},{"unitType",unit->key.type},{"unitId",unit->key.id},{"interact",true},{"purpose","quest-object"}};
        if(walk(c,intent)) observeQuestInteraction(c,intent);
        return false;
    }
    if(item->mode==0 && item->page==1) {
        OnlineItemCommand take;take.action=OnlineItemAction::Take;take.item=item->id;take.itemRevision=item->revision;
        questItemRequest(c,take,QuestPending::Action::Take,"quest-staff-take-submitted");return false;
    }
    if(item->mode!=4) throw std::runtime_error("Staff submission requires the actual completed staff on Cursor");
    const auto before=world.staffRevision;
    if(!c.session.submit_staff(*world.staffSource,item->id)) {wait("quest-staff-submit","Normal staff UI submission was not accepted");return false;}
    QuestPending p;p.action=QuestPending::Action::SubmitStaff;p.item=item->id;p.beforeRevision=before;
    p.deadline=Clock::now()+std::chrono::seconds(30);questRun.pending=p;
    note("quest-staff-submit-submitted",std::to_string(item->id));return false;
}
}
