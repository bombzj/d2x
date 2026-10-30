#include "ai_impl.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace d2x {
void DebugAi::Impl::refreshQuestNpc(Context &c) {
    if(!questRun.pending || !questRun.pending->unit) return;
    auto &pending=*questRun.pending;const auto source=*pending.unit;
    checkQuestPending(c);
    if(!pending.refreshIssued) {
        if(c.session.read().world.npcRequested) c.session.close_npc();
        cancelWalk(c);pending.refreshIssued=true;note("quest-flag-refresh","Querying the private NPC record without replaying the submitted topic");return;
    }
    if(c.session.read().world.npcRequested==source.id) {
        if(!pending.statusRequested) {
            pending.statusRequested=true;pending.deadline=Clock::now()+std::chrono::seconds(20);
        }
        return;
    }
    walk(c,{{"goal","unit"},{"unitType",source.type},{"unitId",source.id},{"interact",true}});
}
std::optional<OnlineItem> DebugAi::Impl::accessibleItem(const Context &c,std::string_view code) const {
    for(const auto &[id,item]:c.session.read().world.items) {
        if(item.code!=code || !onlineItemOwnedBy(item,c.session.read().load.playerUnitId) || !item.positionKnown ||
            !(item.mode==4 || item.mode==2 || (item.mode==0 && (item.page==1 || item.page==4)))) continue;
        const auto decoded=c.inventory.read().items.find(id);
        if(decoded!=c.inventory.read().items.end() && decoded->second.decoded) return item;
    }
    return {};
}
bool DebugAi::Impl::questStoreCursor(Context &c,uint32_t id) {
    const auto &world=c.session.read().world;const auto wire=world.items.find(id);
    if(wire==world.items.end() || !onlineItemOwnedBy(wire->second,c.session.read().load.playerUnitId))
        throw std::runtime_error("Quest reward/source item is no longer owned");
    if(wire->second.mode==0 && (wire->second.page==1 || wire->second.page==5)) return true;
    if(wire->second.mode!=4) return wire->second.mode==1 || wire->second.mode==2;
    if(questRun.pending && questRun.pending->action==QuestPending::Action::Place) {checkQuestPending(c);return false;}
    const auto &inventory=c.inventory.read();const auto decoded=inventory.items.find(id);
    if(decoded==inventory.items.end() || !decoded->second.decoded || decoded->second.width<1 || decoded->second.height<1)
        throw std::runtime_error("Quest cursor item shape is unknown");
    const auto &shape=decoded->second;std::optional<OnlinePoint> cell;
    for(int y=0;y+shape.height<=inventory.rows && !cell;++y) for(int x=0;x+shape.width<=inventory.columns && !cell;++x) {
        bool free=true;
        for(const auto &[otherId,item]:world.items) {
            if(otherId==id || !onlineItemOwnedBy(item,c.session.read().load.playerUnitId) || item.mode!=0 || item.page!=1) continue;
            const auto other=inventory.items.find(otherId);
            if(!item.positionKnown || other==inventory.items.end() || !other->second.decoded || other->second.width<1 || other->second.height<1)
                throw std::runtime_error("Backpack placement is unknown; no guessed quest-item placement");
            if(x<int(item.x)+other->second.width && x+shape.width>item.x && y<int(item.y)+other->second.height && y+shape.height>item.y) {free=false;break;}
        }
        if(free) cell=OnlinePoint{uint16_t(x),uint16_t(y)};
    }
    if(!cell) throw std::runtime_error("Backpack has no real free space for the quest item/reward");
    OnlineItemCommand place;place.action=OnlineItemAction::Place;place.item=id;place.itemRevision=wire->second.revision;
    place.page=0;place.x=uint8_t(cell->x);place.y=uint8_t(cell->y);
    if(!settleItemInput(c)) return false;
    if(!c.inventory.submit(c.session,place)) {wait("quest-place",c.inventory.reason());return false;}
    QuestPending p;p.action=QuestPending::Action::Place;p.item=id;p.sequence=c.session.read().world.itemRequest->sequence;
    p.deadline=Clock::now()+std::chrono::seconds(12);questRun.pending=p;note("quest-item-place-submitted",std::to_string(id));return false;
}
bool DebugAi::Impl::questTalk(Context &c,const Goal &goal,const QuestStep &step) {
    const auto &view=c.session.read();const auto &world=view.world;
    const auto stateQuest=step.stateQuest.value_or(goal.questId);const auto slot=questDefinition(stateQuest).nativeSlot;
    const auto flags=questFlags(c,stateQuest);
    bool done=flags && step.flags && (*flags&step.flags)==step.flags && !(*flags&step.clearFlags);
    if(step.produced=="bkd") done=owned(c,"bkd") && !owned(c,"bks");
    else if(!step.produced.empty() && step.produced!="rin")
        done=owned(c,step.produced) && (step.item.empty() || !owned(c,step.item));
    else if(!step.item.empty() && step.produced.empty()) done=done && !owned(c,step.item);
    if(step.optional && questRun.pending && questRun.pending->action==QuestPending::Action::Topic &&
        world.quests.revision>questRun.pending->beforeRevision &&
        world.quests.statuses[slot] && *world.quests.statuses[slot]>0) done=true;
    if(done) {
        if(goal.questId==QuestId::DenOfEvil && step.flags==1 && questRun.skillPointsBefore) {
            const auto points=world.playerAttributes.find(5);
            if(points==world.playerAttributes.end() || uint64_t(points->second)<uint64_t(*questRun.skillPointsBefore)+1) {
                wait("quest-reward","Reward flag arrived; waiting for the actual skill point");checkQuestPending(c);return false;
            }
        }
        if(goal.questId==QuestId::SistersBurialGrounds && step.flags==1 && questRun.rewardTracked && !world.hireling && !world.deadHirelingName) {
            wait("quest-reward","Waiting for the normal Rogue assignment/preserved mercenary record");checkQuestPending(c);return false;
        }
        if(!step.produced.empty() && step.produced!="rin") {
            const auto scroll=accessibleItem(c,step.produced);
            if(!scroll) throw std::runtime_error("Translated scroll is owned outside accessible inventory; put it in the backpack");
            if(scroll->mode==4) {
                if(questRun.pending && questRun.pending->action==QuestPending::Action::Topic) questRun.pending.reset();
                return questStoreCursor(c,scroll->id);
            }
        }
        if(step.produced=="rin" && questRun.rewardTracked) {
            for(const auto &[id,item]:world.items) if(item.code=="rin" && !questRun.rewardBefore.contains(id) && onlineItemOwnedBy(item,view.load.playerUnitId)) {
                if(questRun.pending && questRun.pending->action!=QuestPending::Action::Place) questRun.pending.reset();
                return questStoreCursor(c,id);
            }
            for(const auto &[id,item]:world.items) if(item.code=="rin" && item.mode==3 && item.positionKnown && !questRun.rewardBefore.contains(id)) {
                if(questRun.pending && questRun.pending->action==QuestPending::Action::Pickup) {checkQuestPending(c);return false;}
                if(questRun.pending && questRun.pending->action==QuestPending::Action::Topic) questRun.pending.reset();
                if(world.npcRequested) {c.session.close_npc();cancelWalk(c);return false;}
                if(!approachPickup(c,item)) return false;
                const uint32_t reward=id;OnlineItemCommand pickup;pickup.action=OnlineItemAction::Pickup;pickup.item=reward;pickup.itemRevision=item.revision;
                if(!c.inventory.submit(c.session,pickup)) {wait("quest-reward",c.inventory.reason());return false;}
                QuestPending p;p.action=QuestPending::Action::Pickup;p.item=reward;p.sequence=c.session.read().world.itemRequest->sequence;
                p.deadline=Clock::now()+std::chrono::seconds(12);questRun.pending=p;note("quest-reward-pickup-submitted",std::to_string(reward));return false;
            }
            wait("quest-reward","Quest granted; waiting for the actual newly created reward ring");checkQuestPending(c);return false;
        }
        return true;
    }
    if(questRun.pending) {
        if(questRun.pending->action==QuestPending::Action::Introduction) {
            checkQuestPending(c);
            if(world.quests.revision<=questRun.pending->beforeRevision) {
                if(!questRun.pending->statusRequested && c.session.request_quests(view.gameGeneration)) {
                    questRun.pending->statusRequested=true;
                    questRun.pending->deadline=Clock::now()+std::chrono::seconds(20);
                    note("quest-status-requested","Querying the actual prerequisite greeting result without replaying its topic");
                }
                wait("quest-introduction-result","Waiting for the actually offered prerequisite greeting to commit");return false;
            }
            if(world.npcRequested) c.session.close_npc();
            cancelWalk(c);questRun.pending.reset();greetingIssued=false;
            note("quest-prerequisite-greeting-observed","Reopening for the next actually offered topic");return false;
        }
        if(step.optional && questRun.pending->action==QuestPending::Action::Topic && !questRun.pending->refreshIssued) {
            // A1Q1/A1Q2 queue their introduction status until NPCDEACTIVATE.
            // End normal listening once; only the later real state/status
            // completes this optional step, never the queued acknowledgement.
            if(world.npcRequested) c.session.close_npc();
            cancelWalk(c);questRun.pending->refreshIssued=true;
            note("quest-introduction-closed","Waiting for the native post-conversation quest update");
            checkQuestPending(c);return false;
        }
        // Retail ScrollMessage callbacks (notably Cain's A2Q2 335..338)
        // change private flags without immediately sending another 0x28.
        // Close/listen again once to request the actual private record. This
        // is an ordinary NPC query, never a replay of the accepted topic or
        // evidence of completion; the checks above still require its result.
        if(questRun.pending->action==QuestPending::Action::Topic) refreshQuestNpc(c);
        wait("quest-dialogue-result","Waiting for actual quest/item state after the offered topic");checkQuestPending(c);return false;
    }
    const auto npc=find(c,*step.target);
    if(!npc) {
        if(approachKnown(c,*step.target)) return false;
        if(step.optional) {note("optional-quest-npc-not-exposed",step.target->name);return true;}
        explore(c,"quest NPC "+step.target->name);return false;
    }
    if(world.npcRequested && world.npcRequested!=npc->key.id) {c.session.close_npc();return false;}
    if(!world.npcConversation || world.npcConversation->type!=1 || world.npcConversation->source!=npc->key.id) {
        wait("quest-dialogue","Opening the actual quest NPC conversation");
        walk(c,{{"goal","unit"},{"unitType",1},{"unitId",npc->key.id},{"interact",true}});return false;
    }
    if(!greetingIssued && npc->classId) {greetingIssued=c.session.confirm_npc_voice(*npc->classId);if(greetingIssued) return false;}
    const auto conversation=*world.npcConversation;
    // Prefer the registered quest topic; automatic greetings remain normal
    // listening input, never permission to send an absent story message.
    for(const auto topic:step.topics) for(const auto &message:conversation.messages) {
        if(message.stringId!=topic || conversation.acknowledged.contains(topic)) continue;
        if(step.produced=="rin") {
            questRun.rewardBefore.clear();for(const auto &[id,item]:world.items) if(item.code=="rin") questRun.rewardBefore.insert(id);
            questRun.rewardTracked=true;
        }
        if(goal.questId==QuestId::SistersBurialGrounds && topic==92) questRun.rewardTracked=true;
        if(goal.questId==QuestId::DenOfEvil && topic==76) {
            // Retail omits zero-valued stats from its initial stat-list
            // replication. An absent newskills entry on this ready player is
            // zero, not a reason to miss the later actual +1 reward update.
            const auto points=world.playerAttributes.find(5);
            questRun.skillPointsBefore=points==world.playerAttributes.end()?0:points->second;
        }
        const auto before=world.quests.revision;
        if(c.session.acknowledge_npc_message(topic)) {
            QuestPending p;p.action=QuestPending::Action::Topic;p.unit=npc->key;p.beforeRevision=before;
            p.deadline=Clock::now()+std::chrono::seconds(20);questRun.pending=p;++interactions;note("quest-topic-submitted",std::to_string(topic));
        }
        return false;
    }
    for(const auto &message:conversation.messages) if(message.menu==0 && !conversation.acknowledged.contains(message.stringId)) {
        const auto before=world.quests.revision;
        if(c.session.acknowledge_npc_message(message.stringId)) {
            QuestPending p;p.action=QuestPending::Action::Introduction;p.unit=npc->key;p.beforeRevision=before;
            p.deadline=Clock::now()+std::chrono::seconds(20);questRun.pending=p;
            note("quest-prerequisite-greeting-submitted",std::to_string(message.stringId));
        }
        return false;
    }
    if(step.optional) {note("optional-quest-introduction-unavailable","Original objective may be done without this introduction");return true;}
    if(actionDeadline==Clock::time_point{}) setActionDeadline(std::chrono::seconds(20),false);
    if(Clock::now()>=actionDeadline) throw std::runtime_error("Quest NPC did not offer the required native topic");
    wait("quest-topic-wait","Waiting for the actual offered quest topic");return false;
}
bool DebugAi::Impl::questReadScroll(Context &c,const QuestStep &step) {
    const auto &world=c.session.read().world;
    if(questRun.pending) {
        if(world.quests.cainStones && world.quests.cainStonesRevision>questRun.pending->beforeRevision) return true;
        wait("quest-scroll-result","Waiting for the original 0x50 stone-order response");checkQuestPending(c);return false;
    }
    const auto item=accessibleItem(c,step.item);
    if(!item || item->mode!=0 || item->page!=1) throw std::runtime_error("Deciphered scroll must be in the real backpack before reading");
    const auto before=world.quests.cainStonesRevision;OnlineItemCommand use;use.action=OnlineItemAction::Use;use.item=item->id;use.itemRevision=item->revision;
    if(!c.inventory.submit(c.session,use)) {wait("quest-scroll",c.inventory.reason());return false;}
    QuestPending p;p.action=QuestPending::Action::Scroll;p.item=item->id;p.beforeRevision=before;
    p.sequence=c.session.read().world.itemRequest->sequence;p.deadline=Clock::now()+std::chrono::seconds(20);questRun.pending=p;
    note("quest-scroll-read-submitted",step.item);return false;
}
bool DebugAi::Impl::questOperate(Context &c,const QuestStep &step) {
    const auto &world=c.session.read().world;const auto flags=questFlags(c,step.stateQuest.value_or(goals[cursor].questId));
    bool modeCompletes=false;
    if(step.action==QuestAction::Operate && step.target && !step.flags && !step.witness && step.topics.empty()) {
        for(size_t row=0;row<definitions->objects.rows().size();++row) if(definitions->objects.number(row,"Id")==step.target->identity) {
            const auto operation=definitions->objects.number(row,"OperateFn");
            // ObjMode31 and45 accept neutral only. An actual non-neutral
            // assignment also resumes an activation done by another player;
            // item/drop/travel postconditions remain separate graph nodes.
            modeCompletes=operation==31 || operation==45;break;
        }
        if(modeCompletes) if(const auto object=find(c,*step.target,true);object && object->mode && *object->mode!=0) {
            note("quest-one-time-object-observed",std::to_string(object->key.id));return true;
        }
    }
    if(step.refreshFlags && !questRun.pending && step.target && !(flags && (*flags&step.flags)==step.flags)) {
        const auto object=find(c,*step.target,true);
        if(object && object->mode && *object->mode!=0) {
            // A shared open object is not personal eligibility. Query the
            // private record once; do not replay an altar operation merely
            // because its earlier inventory result arrived before packet28.
            QuestPending p;p.action=QuestPending::Action::Object;p.unit=object->key;p.assignment=object->assignmentRevision;
            p.deadline=Clock::now()+std::chrono::seconds(20);questRun.pending=p;
            note("quest-open-object-private-query");
        }
    }
    if(step.refreshFlags && questRun.pending && !questRun.pending->statusRequested) {
        if(c.session.request_quests(c.session.read().gameGeneration)) {
            questRun.pending->statusRequested=true;questRun.pending->deadline=Clock::now()+std::chrono::seconds(20);
            note("quest-status-requested","Private object eligibility, without replaying its operation");
        }
        return false;
    }
    if(step.publishesTombSymbol && !questRun.rewardTracked) {
        // Retail grants the journal flags while opening it; this payload can
        // precede the reading acknowledgement that creates its canyon portal.
        // An already completed journal may have a valid initialization payload.
        questRun.clueBefore=flags && (*flags&1)?0:world.quests.staffTombRevision;
        questRun.rewardTracked=true;
    }
    if(!step.topics.empty() && world.npcConversation && world.npcConversation->type!=1 && step.target) {
        const auto unit=world.units.find({2,world.npcConversation->source});
        if(unit!=world.units.end() && unit->second.classId==step.target->identity) for(const auto &message:world.npcConversation->messages)
            if(std::find(step.topics.begin(),step.topics.end(),message.stringId)!=step.topics.end() && !world.npcConversation->acknowledged.contains(message.stringId)) {
                if(c.session.acknowledge_npc_message(message.stringId) && step.publishesTombSymbol) {
                    QuestPending p;p.action=QuestPending::Action::Object;p.unit=unit->first;
                    p.assignment=unit->second.assignmentRevision;p.beforeRevision=questRun.clueBefore;p.refreshIssued=true;
                    p.deadline=Clock::now()+std::chrono::seconds(30);questRun.pending=p;
                    note("quest-journal-read-submitted",std::to_string(message.stringId));
                }
                return false;
            }
    }
    if(step.publishesTombSymbol && questRun.pending && questRun.pending->refreshIssued &&
        !questRun.pending->statusRequested) {
        // Original QUESTS_StatusCallback (C2S40) publishes the private flags
        // and the combined 50/id1 tomb clue after the journal callback.
        if(c.session.request_quests(c.session.read().gameGeneration)) {
            questRun.pending->statusRequested=true;note("quest-status-requested","Horazon journal: original 0x40 query");
            questRun.pending->deadline=Clock::now()+std::chrono::seconds(30);
        }
        return false;
    }
    if(step.publishesTombSymbol && questRun.pending && questRun.pending->refreshIssued &&
        world.quests.staffTombRevision>questRun.pending->beforeRevision && world.quests.staffTombOffset &&
        *world.quests.staffTombOffset<7 && flags && (*flags&1)) {
        tombSymbol=world.quests.staffTombOffset;note("quest-tomb-symbol-observed",std::to_string(*tombSymbol));return true;
    }
    if(!step.publishesTombSymbol && step.flags && flags && (*flags&step.flags)==step.flags) return true;
    if(step.witness && find(c,*step.witness)) return true;
    if(step.optional && questRun.pending && world.quests.statuses[goals[cursor].questSlot] &&
        *world.quests.statuses[goals[cursor].questSlot]>0) return true;
    if(questRun.pending) {
        const auto unit=questRun.pending->unit?world.units.find(*questRun.pending->unit):world.units.end();
        if(!step.publishesTombSymbol && !step.flags && unit!=world.units.end() && unit->second.assignmentRevision==questRun.pending->assignment && unit->second.mode && *unit->second.mode!=0) {
            note("quest-object-mode-observed",std::to_string(unit->first.id));questRun.pending.reset();
            if(modeCompletes) return true;
        } else {wait("quest-object-result","Waiting for the actual object mode/quest flag");checkQuestPending(c);return false;}
    }
    std::optional<Selector> selector=step.target;
    if(step.action==QuestAction::Stones) {
        if(!world.quests.cainStones) throw std::runtime_error("Stone order is unknown; the real deciphered scroll must be read first");
        bool all=true;
        for(const auto id:*world.quests.cainStones) {
            if(std::find(step.objects.begin(),step.objects.end(),id)==step.objects.end()) throw std::runtime_error("Native stone order does not match current MPQ objects");
            const Selector stone{2,uint16_t(id),"ordered Cairn stone"};const auto unit=find(c,stone,true);
            if(unit && unit->mode && *unit->mode!=0) continue;
            selector=stone;all=false;break;
        }
        if(all) return true;
    }
    if(!selector) throw std::runtime_error("Quest operation has no verified object identity");
    const auto unit=find(c,*selector,true);
    if(!unit) {
        if(approachKnown(c,*selector)) return false;
        if(step.optional) {note("optional-quest-object-not-exposed","Direct tower entry is permitted by A1Q5");return true;}
        explore(c,selector->name);return false;
    }
    wait("quest-object","Approaching and operating the actual quest object");
    const Json intent{{"goal","unit"},{"unitType",unit->key.type},{"unitId",unit->key.id},{"interact",true},{"purpose","quest-object"}};
    if(walk(c,intent)) observeQuestInteraction(c,intent);
    return false;
}
bool DebugAi::Impl::questPortal(Context &c,const QuestStep &step) {
    if(c.town.read().area==step.area) return true;
    if(travelDestination && (travelPhase==TravelPhase::WaypointWait || (travelPhase==TravelPhase::CrossWait && legUnit))) {
        travel(c,*travelDestination);return false;
    }
    if(!questRun.pending && step.preferWaypoint && step.area && c.session.read().world.waypointHistory &&
        std::any_of(c.town.read().waypoints.begin(),c.town.read().waypoints.end(),[&](const auto &entry){return entry.level==step.area && entry.unlocked;}))
        return travel(c,*step.area);
    if(questRun.pending && step.witness && questRun.pending->action==QuestPending::Action::Object) {
        const auto entrance=step.target?find(c,*step.target,true):std::nullopt;
        if(entrance && c.town.permitsInteraction(c.session.read(),entrance->key)) {questRun.pending.reset();cancelWalk(c);}
        else {
            wait("quest-entrance-opening","Waiting at the observed orifice for the real selectable tomb entrance");
            checkQuestPending(c);return false;
        }
    }
    if(questRun.pending) {
        const auto &pending=*questRun.pending;
        if(pending.action==QuestPending::Action::Object && pending.unit) {
            const auto unit=c.session.read().world.units.find(*pending.unit);
            if(unit!=c.session.read().world.units.end() && unit->second.assignmentRevision==pending.assignment &&
                unit->second.mode && *unit->second.mode!=0 &&
                std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &target) {
                    return target.unit==*pending.unit && target.interaction==OnlineMapInteraction::Portal;
                })) {
                note("quest-portal-activation-observed",std::to_string(pending.unit->id));
                questRun.pending.reset();cancelWalk(c);return false;
            }
            wait("quest-portal-activation","Waiting for the activated portal's normal selectable mode");
        } else wait("quest-portal-result","Waiting for the actual quest destination scene");
        checkQuestPending(c);return false;
    }
    for(const auto &target:c.town.read().mapTargets) if(target.interaction==OnlineMapInteraction::Portal &&
        (target.destination==step.area || (step.target && target.unit.type==step.target->type && [&] {
            const auto u=c.session.read().world.units.find(target.unit);
            return u!=c.session.read().world.units.end() && u->second.classId==step.target->identity;
        }()))) {
        wait("quest-portal","Using the actually assigned quest portal");
        bool activation=false;
        const auto unit=c.session.read().world.units.find(target.unit);
        if(target.unit.type==2 && unit!=c.session.read().world.units.end() && unit->second.classId && unit->second.mode==0)
            for(size_t row=0;row<definitions->objects.rows().size();++row)
                if(definitions->objects.number(row,"Id")==*unit->second.classId && definitions->objects.number(row,"OperateFn")==34)
                    activation=true;
        const Json intent{{"goal","unit"},{"unitType",target.unit.type},{"unitId",target.unit.id},{"interact",true},
            {"purpose","quest-portal"},{"activation",activation}};
        if(walk(c,intent)) observeQuestInteraction(c,intent);
        return false;
    }
    if(step.target && approachKnown(c,*step.target)) return false;
    if(area && step.area) if(const auto hint=c.town.knownConnectionPosition(*area,*step.area)) {
        if(approachPoint(c,*hint,"previously observed quest portal")) return false;
        if((c.position-Vec{float(hint->x),float(hint->y)}).length()<=8) {
            wait("quest-portal-reassignment","Waiting for the real portal assignment at its observed position");return false;
        }
    }
    if(step.sourceArea && c.town.read().area!=step.sourceArea) {
        travel(c,*step.sourceArea);return false;
    }
    if(step.witness && area) {
        const auto marker=c.town.knownTargetPosition(*area,step.witness->type,step.witness->identity);
        if(marker && marker->x>=13 && marker->y<=65532) {
            // A2Q6_Timer_UpdateObjects creates the real entrance at the
            // observed orifice (-13,+3). Approach its room normally; never
            // resume unrelated tomb exploration after staff consumption.
            const OnlinePoint birth{uint16_t(marker->x-13),uint16_t(marker->y+3)};
            if(approachPoint(c,birth,"entrance beside the observed Horadric orifice",3)) return false;
            if((c.position-Vec{float(birth.x),float(birth.y)}).length()<=8) {
                cancelWalk(c);QuestPending pending;pending.action=QuestPending::Action::Object;
                pending.deadline=Clock::now()+std::chrono::seconds(30);questRun.pending=pending;
                wait("quest-entrance-opening","Waiting for the actual timed selectable entrance assignment");return false;
            }
        }
        if(approachKnown(c,*step.witness)) return false;
    }
    explore(c,"the actual assigned quest portal");return false;
}
bool DebugAi::Impl::questImbue(Context &c,const Goal &goal,const QuestStep &step) {
    const auto &view=c.session.read();const auto &world=view.world;const auto flags=questFlags(c,goal.questId);
    const auto rare=[&](uint32_t id) {
        const auto item=world.items.find(id);const auto decoded=c.inventory.read().items.find(id);
        return item!=world.items.end() && onlineItemOwnedBy(item->second,view.load.playerUnitId) && item->second.code==step.item &&
            decoded!=c.inventory.read().items.end() && decoded->second.decoded && decoded->second.quality==6;
    };
    if(questRun.materialPhase>=2) {
        std::optional<uint32_t> result;
        if(questRun.material && rare(*questRun.material)) result=questRun.material;
        if(!result) for(const auto &[id,item]:world.items) {
            (void)item;if(!questRun.materialBefore.contains(id) && rare(id)) {result=id;break;}
        }
        const auto &request=world.itemRequest;
        if(result && questRun.materialPhase==2 && questRun.pending && request && request->sequence==questRun.pending->sequence &&
            request->state==OnlineItemRequest::State::Updated) {
            // The normal service consumes the entitlement only once; retain
            // the actual transformed/new GUID before placing the rare item.
            questRun.material=*result;questRun.materialPhase=3;questRun.pending.reset();note("imbue-rare-result-observed",std::to_string(*result));
        }
        if(result && questRun.materialPhase==3) {
            if(!questStoreCursor(c,*result)) return false;
            questRun.pending.reset();
            if(flags && (*flags&1)) return true;
            // SUnitNpc's Charsi service calls ACT1Q3_SetRewardGranted without
            // UpdatePlayerFlags. Observe the completed service and store its
            // actual output before reopening the NPC for the full snapshot.
            QuestPending p;p.action=QuestPending::Action::Imbue;p.unit=questRun.materialNpc;
            p.deadline=Clock::now()+std::chrono::seconds(20);questRun.pending=p;questRun.materialPhase=4;
        }
        if(questRun.materialPhase==4) {
            if(flags && (*flags&1)) return true;
            refreshQuestNpc(c);wait("quest-imbue-flags","Rare output stored; querying the actual consumed imbue entitlement");
            checkQuestPending(c);return false;
        }
        wait("quest-imbue-result","Waiting for native service result and the actual rare item");checkQuestPending(c);return false;
    }
    if(!flags || !(*flags&2)) throw std::runtime_error("Imbue needs the actual pending Malus reward");
    if(!questRun.material) {
        // Reserve a spare normal backpack cap. Never select equipped items,
        // rare/unique gear or an arbitrary valuable base from this character.
        for(const auto &[id,item]:world.items) {
            if(item.code!=step.item || item.mode!=0 || item.page!=1 || !item.positionKnown || !onlineItemOwnedBy(item,view.load.playerUnitId)) continue;
            const auto decoded=c.inventory.read().items.find(id);
            if(decoded==c.inventory.read().items.end() || !decoded->second.decoded || decoded->second.quality!=2 ||
                decoded->second.sockets || decoded->second.filledSockets || decoded->second.runeword || (item.flags&0x1000u)) continue;
            questRun.material=id;break;
        }
        if(!questRun.material) throw std::runtime_error("Full Tools of the Trade needs the reserved unsocketed normal backpack cap; prepare it with the debug Act-I save recipe");
        for(const auto &[id,item]:world.items) if(item.code==step.item) questRun.materialBefore.insert(id);
    }
    if(questRun.materialPhase==0) {
        const auto npc=find(c,*step.target);
        if(!npc) {if(!approachKnown(c,*step.target)) explore(c,"Charsi's original imbue service");return false;}
        if(world.npcRequested && world.npcRequested!=npc->key.id) {c.session.close_npc();return false;}
        if(!world.npcConversation || world.npcConversation->type!=1 || world.npcConversation->source!=npc->key.id) {
            walk(c,{{"goal","unit"},{"unitType",1},{"unitId",npc->key.id},{"interact",true}});return false;
        }
        const auto item=world.items.find(*questRun.material);
        if(item==world.items.end() || item->second.mode!=0 || item->second.page!=1) throw std::runtime_error("Reserved imbue base changed before take");
        const uint32_t material=item->first;OnlineItemCommand take;take.action=OnlineItemAction::Take;take.item=material;take.itemRevision=item->second.revision;
        if(!c.inventory.submit(c.session,take)) {wait("quest-imbue-take",c.inventory.reason());return false;}
        questRun.materialNpc=npc->key;
        QuestPending p;p.action=QuestPending::Action::Take;p.item=material;p.sequence=c.session.read().world.itemRequest->sequence;
        p.deadline=Clock::now()+std::chrono::seconds(12);questRun.pending=p;questRun.materialPhase=1;cancelWalk(c);
        note("imbue-base-take-submitted",std::to_string(material));return false;
    }
    if(questRun.materialPhase==1) {
        const auto item=world.items.find(*questRun.material);
        if(item==world.items.end() || item->second.mode!=4 || !onlineItemOwnedBy(item->second,view.load.playerUnitId)) {
            wait("quest-imbue-cursor","Waiting for the actual reserved base on Cursor");checkQuestPending(c);return false;
        }
        if(!questRun.materialNpc || !world.npcConversation || world.npcConversation->type!=1 ||
            world.npcRequested!=questRun.materialNpc->id || world.npcConversation->source!=questRun.materialNpc->id)
            throw std::runtime_error("Imbue NPC conversation ended with the reserved base on Cursor");
        const uint32_t material=item->first;questRun.pending.reset();OnlineItemCommand imbue;imbue.action=OnlineItemAction::QuestService;
        imbue.npc=questRun.materialNpc->id;imbue.item=material;imbue.itemRevision=item->second.revision;
        if(!c.inventory.submit(c.session,imbue)) {wait("quest-imbue",c.inventory.reason());return false;}
        QuestPending p;p.action=QuestPending::Action::Imbue;p.item=material;p.sequence=c.session.read().world.itemRequest->sequence;
        p.deadline=Clock::now()+std::chrono::seconds(20);questRun.pending=p;questRun.materialPhase=2;
        note("imbue-service-submitted",std::to_string(material));return false;
    }
    wait("quest-imbue-result","Waiting for the original service result; no repeated imbue request");checkQuestPending(c);return false;
}
bool DebugAi::Impl::questActTravel(Context &c,const Goal &goal,const QuestStep &step) {
    if(c.town.read().area==step.area) {
        if(questFinished(c,goal)) return true;
        wait("quest-act-flags","Destination town observed; waiting for final quest/change flags");checkQuestPending(c);return false;
    }
    if(questRun.pending) {wait("quest-act-travel-result","Waiting for the real normal act transfer");checkQuestPending(c);return false;}
    const auto npc=find(c,*step.target);
    if(!npc) {if(!approachKnown(c,*step.target)) explore(c,"the actual NPC act travel service");return false;}
    const auto &world=c.session.read().world;
    if(world.npcRequested && world.npcRequested!=npc->key.id) {c.session.close_npc();return false;}
    if(!world.npcConversation || world.npcConversation->type!=1 || world.npcConversation->source!=npc->key.id) {
        walk(c,{{"goal","unit"},{"unitType",1},{"unitId",npc->key.id},{"interact",true}});return false;
    }
    const auto &conversation=c.town.read().npcConversation;
    if(!conversation || !conversation->travelDestination || conversation->travelDestination!=step.area) {
        if(actionDeadline==Clock::time_point{}) setActionDeadline(std::chrono::seconds(20),false);
        if(Clock::now()>=actionDeadline) throw std::runtime_error("NPC did not offer the actual act travel service");
        wait("quest-act-service","Waiting for the eligible act travel service");return false;
    }
    if(!c.session.npc_travel(*conversation->travelDestination)) {wait("quest-act-service","Normal NPC travel was not accepted");return false;}
    QuestPending p;p.action=QuestPending::Action::Travel;p.unit=npc->key;p.deadline=Clock::now()+std::chrono::seconds(30);
    questRun.pending=p;cancelWalk(c);note("quest-act-travel-submitted",std::to_string(*step.area));return false;
}
}
