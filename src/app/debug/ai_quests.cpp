#include "ai_impl.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace d2x {
void DebugAi::Impl::configureQuest(Goal &goal) const {
    static constexpr std::array<std::pair<std::string_view,QuestId>,18> quests{{
        {"den-of-evil",QuestId::DenOfEvil},{"sisters-burial-grounds",QuestId::SistersBurialGrounds},
        {"search-for-cain",QuestId::SearchForCain},{"forgotten-tower",QuestId::ForgottenTower},
        {"tools-of-the-trade",QuestId::ToolsOfTheTrade},{"sisters-to-the-slaughter",QuestId::SistersToTheSlaughter},
        {"radaments-lair",QuestId::RadamentsLair},{"horadric-staff",QuestId::HoradricStaff},
        {"tainted-sun",QuestId::TaintedSun},{"arcane-sanctuary",QuestId::ArcaneSanctuary},
        {"summoner",QuestId::Summoner},{"seven-tombs",QuestId::SevenTombs},
        {"golden-bird",QuestId::GoldenBird},{"blade-of-the-old-religion",QuestId::BladeOfTheOldReligion},
        {"lam-esens-tome",QuestId::LamEsensTome},{"khalims-will",QuestId::KhalimsWill},
        {"blackened-temple",QuestId::BlackenedTemple},{"guardian",QuestId::Guardian}}};
    const auto found=std::find_if(quests.begin(),quests.end(),[&](const auto &v){return v.first==goal.quest;});
    if(found==quests.end()) throw std::invalid_argument("No verified quest recipe: "+goal.quest);
    goal.questId=found->second;goal.questSlot=questDefinition(goal.questId).nativeSlot;
    if(questDefinition(goal.questId).act==1) {configureActTwoQuest(goal,goal.questId);return;}
    if(questDefinition(goal.questId).act==2) {configureActThreeQuest(goal,goal.questId);return;}
    const auto town=definitions->area("Act 1 - Town");
    const auto object=[&](std::string_view name,int operation) {
        const auto id=definitions->object(std::string(name));
        for(size_t row=0;row<definitions->objects.rows().size();++row)
            if(definitions->objects.number(row,"Id")==id && definitions->objects.number(row,"OperateFn")==operation) return id;
        throw std::invalid_argument("Current MPQ object does not match the verified quest operation: "+std::string(name));
    };
    const auto talk=[&](std::string_view npc,uint16_t topic,uint16_t flags,bool optional=false,std::string item={},std::string produced={}) {
        QuestStep s;s.action=QuestAction::Talk;s.area=town;s.target=definitions->selector(Json{{"npc",npc}});
        s.topics={topic};s.flags=flags;s.optional=optional;s.item=std::move(item);s.produced=std::move(produced);
        s.skipPending=optional;goal.steps.push_back(std::move(s));
    };
    const auto hunt=[&](std::string_view level,Json target,uint16_t flags) {
        QuestStep s;s.action=QuestAction::Hunt;s.area=definitions->area(std::string(level));
        s.target=definitions->selector(target);s.flags=flags;goal.steps.push_back(std::move(s));
    };
    const auto home=[&]() {QuestStep s;s.action=QuestAction::ReturnTown;s.skipPending=false;goal.steps.push_back(s);};
    // Native topics/operations below are A1Q1..6 ScrollMessage and ObjMode
    // contracts. Areas and object/monster identities resolve against this MPQ.
    switch(goal.questId) {
    case QuestId::DenOfEvil: {
        talk("akara",64,4,true);
        QuestStep s;s.action=QuestAction::Clear;s.area=definitions->area("Act 1 - Cave 1");s.flags=2;goal.steps.push_back(s);
        home();talk("akara",76,1);break;
    }
    case QuestId::SistersBurialGrounds:
        talk("kashya",81,4,true);hunt("Act 1 - Graveyard",{{"monster","bloodraven"}},2);
        home();talk("kashya",92,1);break;
    case QuestId::SearchForCain: {
        talk("akara",97,4,true);
        QuestStep tree;tree.action=QuestAction::Acquire;tree.area=definitions->area("Act 1 - Wilderness 4");
        tree.item="bks";tree.produced="bkd";tree.source=object("Inifuss",12);goal.steps.push_back(tree);
        home();goal.steps.back().skipPending=true;talk("akara",112,0,false,"bks","bkd");goal.steps.back().skipPending=true;
        QuestStep read;read.action=QuestAction::ReadScroll;read.item="bkd";goal.steps.push_back(read);
        QuestStep stones;stones.action=QuestAction::Stones;stones.area=definitions->area("Act 1 - Wilderness 3");
        for(const auto name:{"StoneAlpha","StoneBeta","StoneGamma","StoneDelta","StoneLambda"}) stones.objects.push_back(object(name,9));
        goal.steps.push_back(stones);
        QuestStep portal;portal.action=QuestAction::Portal;portal.area=definitions->area("Act 1 - Tristram");goal.steps.push_back(portal);
        QuestStep cage;cage.action=QuestAction::Operate;cage.area=portal.area;cage.target=Selector{2,object("Gibbet",10),"Gibbet"};
        cage.flags=2;cage.witness=definitions->selector(Json{{"monster","cain1"}});goal.steps.push_back(cage);
        home();talk("akara",118,1,false,{},"rin");break;
    }
    case QuestId::ForgottenTower: {
        QuestStep tome;tome.action=QuestAction::Operate;tome.area=definitions->area("Act 1 - Wilderness 3");
        tome.target=Selector{2,object("TowerTome",6),"TowerTome"};tome.flags=4;tome.optional=true;tome.topics={127};goal.steps.push_back(tome);
        hunt("Act 1 - Crypt 3 E",{{"superunique","The Countess"}},1);home();talk("akara",143,1);break;
    }
    case QuestId::ToolsOfTheTrade: {
        talk("charsi",146,4,true);hunt("Act 1 - Barracks",{{"superunique","The Smith"}},0);
        QuestStep malus;malus.action=QuestAction::Acquire;malus.area=definitions->area("Act 1 - Barracks");
        malus.item="hdm";malus.source=definitions->malusSource();goal.steps.push_back(malus);
        home();talk("charsi",163,2,false,"hdm");
        QuestStep imbue;imbue.action=QuestAction::Imbue;imbue.area=town;imbue.target=definitions->selector(Json{{"npc","charsi"}});
        imbue.item="cap";imbue.skipPending=false;
        if(!definitions->item(imbue.item)) throw std::invalid_argument("MPQ has no normal cap for the reserved debug imbue material");
        goal.steps.push_back(imbue);break;
    }
    case QuestId::SistersToTheSlaughter: {
        talk("cain5",166,4,true);hunt("Act 1 - Catacombs 4",{{"monster","andariel"}},2);home();
        // A1Q6 ScrollMessage183 grants the reward before the independent
        // act-change word is written by the normal transfer.
        talk("warriv1",183,1);
        QuestStep passage;passage.action=QuestAction::ActTravel;passage.area=definitions->area("Act 2 - Town");
        passage.target=definitions->selector(Json{{"npc","warriv1"}});passage.skipPending=false;goal.steps.push_back(passage);break;
    }
    default:throw std::invalid_argument("Quest recipe is outside Act I");
    }
}
std::optional<uint16_t> DebugAi::Impl::questFlags(const Context &c,QuestId id) const {
    const auto &flags=c.session.read().world.quests.playerFlags;
    return flags?std::optional{(*flags)[questDefinition(id).nativeSlot]}:std::nullopt;
}
bool DebugAi::Impl::questFinished(const Context &c,const Goal &goal) const {
    const auto flags=questFlags(c,goal.questId);
    if(!flags || !(*flags&1)) return false;
    if(goal.questId==QuestId::RadamentsLair && (*flags&0x20)) return false;
    if(goal.questId==QuestId::ArcaneSanctuary && (*flags&2)) return false;
    if(goal.questId==QuestId::GoldenBird && ((*flags&0x20) || owned(c,"xyz"))) return false;
    if(questWeaponRestore && (goal.questId==QuestId::KhalimsWill || goal.questId==QuestId::BlackenedTemple || goal.questId==QuestId::Guardian)) {
        const auto original=c.session.read().world.items.find(*questWeaponRestore);
        if(original==c.session.read().world.items.end() || original->second.mode!=1 || original->second.body!=4 ||
            !onlineItemOwnedBy(original->second,c.session.read().load.playerUnitId)) return false;
    }
    if(goal.questId==QuestId::Guardian)
        return c.session.read().world.quests.playerFlags && ((*c.session.read().world.quests.playerFlags)[23]&1);
    if(goal.questId==QuestId::SevenTombs)
        return c.session.read().world.quests.playerFlags && ((*c.session.read().world.quests.playerFlags)[15]&1);
    // Native Act-I change record (slot 7) is separate from Andariel's reward.
    return goal.questId!=QuestId::SistersToTheSlaughter ||
        (c.session.read().world.quests.playerFlags && ((*c.session.read().world.quests.playerFlags)[7]&1));
}
bool DebugAi::Impl::questTransaction(const Context &c) const {
    if(cursor>=goals.size() || goals[cursor].kind!=Kind::Quest || supplyStage!=SupplyStage::None) return false;
    if(questRun.pending && questRun.pending->sequence) return true;
    if(questRun.step>=goals[cursor].steps.size()) return false;
    const auto action=goals[cursor].steps[questRun.step].action;
    const auto &world=c.session.read().world;
    return (action==QuestAction::Imbue && questRun.materialPhase>0) ||
        (action==QuestAction::EquipWeapon && questRun.materialPhase>0) ||
        (action==QuestAction::Combine && (world.storage.kind==OnlineStorageKind::Cube || world.storage.requested==OnlineStorageKind::Cube)) ||
        (action==QuestAction::SubmitStaff && questRun.materialNpc && world.staffSource==questRun.materialNpc->id);
}
void DebugAi::Impl::questStepDone(Context &c,std::string evidence) {
    if(cursor<goals.size() && questRun.step<goals[cursor].steps.size() &&
        goals[cursor].steps[questRun.step].publishesTombSymbol && questRun.pending &&
        questRun.pending->refreshIssued && questRun.pending->statusRequested && tombSymbol && *tombSymbol<7)
        journalReadGame=c.session.read().gameGeneration;
    releaseCombat(c);
    if(c.session.read().world.npcRequested) c.session.close_npc();
    if(c.session.read().world.waypointSource) c.session.use_waypoint(0);
    cancelWalk(c);note("quest-step-completed",std::move(evidence));
    if(cursor<goals.size() && !goals[cursor].requirements.empty()) {
        auto observations=std::move(questRun.observations);
        if(goals[cursor].requirements.at(questRun.step).retainObservation) observations.insert(questRun.step);
        const auto completed=questRun.step;questRun={};questRun.initialized=true;questRun.step=completed;
        questRun.observations=std::move(observations);resetStep();return;
    }
    const auto step=questRun.step+1;questRun={};questRun.initialized=true;questRun.step=step;resetStep();
}
bool DebugAi::Impl::prepareQuest(Context &c,const Goal &goal) {
    if(goal.kind!=Kind::Quest) return true;
    if(supplyStage!=SupplyStage::None) return true;
    const auto flags=questFlags(c,goal.questId);
    if(!flags) {wait("quest-state","Waiting for actual native character quest flags");return false;}
    if(!questRun.initialized) {
        const auto &clue=c.session.read().world.quests;
        const auto arcane=questFlags(c,QuestId::ArcaneSanctuary);
        // StatusCallback includes the real tomb offset for an already-read
        // journal. The zero field sent for unrelated progress is insufficient.
        if(!tombSymbol && arcane && (*arcane&0x2001) && clue.staffTombRevision &&
            clue.staffTombOffset && *clue.staffTombOffset<7) {
            tombSymbol=clue.staffTombOffset;note("quest-initial-tomb-symbol-observed",std::to_string(*tombSymbol));
        }
        if(questFinished(c,goal)) {advance(c,"Already completed native quest: "+goal.quest);return false;}
        if(goal.questId==QuestId::SearchForCain && (*flags&0x8000) && !(*flags&3))
            throw std::runtime_error("Cain was rescued by rogues in this save; use a fresh quest baseline for the full rescue");
        if((*flags&0x4000) && !(*flags&0x2003)) throw std::runtime_error("Original quest is unavailable to this character in this game: "+goal.quest);
        if(c.session.read().load.act!=questDefinition(goal.questId).act)
            throw std::runtime_error("Unfinished quest needs its normal act scene: "+goal.quest);
        questRun.initialized=true;note("quest-started",goal.quest);
    }
    if(!goal.requirements.empty()) return selectQuestRequirement(c,goal);
    while(questRun.step<goal.steps.size() && goal.steps[questRun.step].skipPending && (*flags&3))
        questStepDone(c,"Native reward/pending flag already satisfies the field objective");
    if(questRun.step>=goal.steps.size()) {
        if(!questFinished(c,goal)) {wait("quest-result","Waiting for the actual final quest and act-change flags");return false;}
        advance(c,"Native quest/reward completion observed: "+goal.quest);return false;
    }
    return true;
}
DebugAi::Impl::Goal DebugAi::Impl::questLeaf(const Goal &goal) const {
    if(goal.kind!=Kind::Quest || questRun.step>=goal.steps.size() || supplyStage!=SupplyStage::None) return goal;
    const auto &step=goal.steps[questRun.step];Goal leaf;
    leaf.area=step.area;leaf.target=step.target;leaf.item=step.item;leaf.sourceObject=step.source;
    if(goal.questTown && step.area==goal.questTown && area!=goal.questTown &&
        step.action!=QuestAction::Portal && step.action!=QuestAction::ActTravel) {
        leaf.kind=Kind::ReturnTown;return leaf;
    }
    switch(step.action) {
    case QuestAction::Hunt:leaf.kind=Kind::Hunt;break;
    case QuestAction::Acquire:leaf.kind=Kind::Acquire;break;
    case QuestAction::ReturnTown:leaf.kind=Kind::ReturnTown;break;
    default:leaf.kind=Kind::Quest;break;
    }
    return leaf;
}
void DebugAi::Impl::checkQuestPending(const Context &c) const {
    if(!questRun.pending) return;
    const auto &p=*questRun.pending;const auto &request=c.session.read().world.itemRequest;
    if(p.resultObserved) return;
    if(p.sequence && request) {
        if(p.sequence!=request->sequence) throw std::runtime_error("Quest item operation was replaced before its result was observed");
        if(request->state==OnlineItemRequest::State::Rejected || request->state==OnlineItemRequest::State::TimedOut || request->state==OnlineItemRequest::State::Interrupted)
            throw std::runtime_error("Quest item operation did not confirm its actual result; no blind retry");
    }
    if(!questWaitRequiresInput(c) && Clock::now()>=p.deadline) throw std::runtime_error("Quest operation's actual result was not observed: "+phase);
}
void DebugAi::Impl::observeQuestResult(Context &c) {
    if(!questRun.pending || cursor>=goals.size() || goals[cursor].kind!=Kind::Quest || questRun.step>=goals[cursor].steps.size()) return;
    auto &pending=*questRun.pending;if(pending.resultObserved) return;
    const auto &goal=goals[cursor];const auto &step=goal.steps[questRun.step];const auto &world=c.session.read().world;
    const auto flags=questFlags(c,step.stateQuest.value_or(goal.questId));
    const auto item=world.items.find(pending.item);
    const bool ours=item!=world.items.end() && onlineItemOwnedBy(item->second,c.session.read().load.playerUnitId);
    const auto unit=pending.unit?world.units.find(*pending.unit):world.units.end();
    bool observed=false;
    switch(pending.action) {
    case QuestPending::Action::Take:observed=ours && item->second.mode==4;break;
    case QuestPending::Action::Place:observed=ours && item->second.mode==0 && item->second.page==1;break;
    case QuestPending::Action::StoreCube:observed=ours && item->second.mode==0 && item->second.page==4;break;
    case QuestPending::Action::Pickup:observed=ours && item->second.mode!=3;break;
    case QuestPending::Action::OpenCube:observed=world.storage.kind==OnlineStorageKind::Cube && world.storage.source==pending.item;break;
    case QuestPending::Action::CloseCube:observed=world.storage.kind==OnlineStorageKind::None && world.storage.requested==OnlineStorageKind::None;break;
    case QuestPending::Action::Transmute:
        observed=accessibleItem(c,step.item).has_value() && (step.ingredients.empty()? !owned(c,"msf") && !owned(c,"vip"):
            std::none_of(step.ingredients.begin(),step.ingredients.end(),[&](const auto &code){return owned(c,code);}));break;
    case QuestPending::Action::Use: {
        if(step.item=="xyz") {observed=!owned(c,step.item) && flags && (*flags&1) && !(*flags&0x20);break;}
        const auto points=world.playerAttributes.find(5);
        observed=!owned(c,step.item) && questRun.skillPointsBefore && points!=world.playerAttributes.end() &&
            uint64_t(points->second)>=uint64_t(*questRun.skillPointsBefore)+1;break;
    }
    case QuestPending::Action::Scroll:observed=world.quests.cainStones && world.quests.cainStonesRevision>pending.beforeRevision;break;
    case QuestPending::Action::Travel:observed=step.area && c.town.read().area==step.area && questFinished(c,goal);break;
    case QuestPending::Action::Portal:observed=step.area && c.town.read().area==step.area;break;
    case QuestPending::Action::SubmitStaff:
        if(world.staffRevision>pending.beforeRevision && world.staffResult==4) throw std::runtime_error("The original orifice rejected this staff");
        {const auto staff=questFlags(c,QuestId::HoradricStaff);observed=staff && (*staff&1) && !owned(c,step.item);}break;
    case QuestPending::Action::Introduction:observed=world.quests.revision>pending.beforeRevision;break;
    case QuestPending::Action::Topic:
        observed=flags && step.flags && (*flags&step.flags)==step.flags && !(*flags&step.clearFlags);
        if(step.optional) observed|=world.quests.revision>pending.beforeRevision && world.quests.statuses[questDefinition(step.stateQuest.value_or(goal.questId)).nativeSlot].value_or(0)>0;
        if(!step.item.empty() && step.produced.empty()) observed=observed && !owned(c,step.item);
        if(!step.produced.empty()) observed=observed && owned(c,step.produced);
        if(!step.produced.empty() && step.produced!="rin")
            observed=owned(c,step.produced) && (step.item.empty() || !owned(c,step.item));
        if(step.produced=="rin" && questRun.rewardTracked) {
            const bool reward=std::any_of(world.items.begin(),world.items.end(),[&](const auto &entry) {
                const auto &[id,ring]=entry;return ring.code=="rin" && !questRun.rewardBefore.contains(id) &&
                    (onlineItemOwnedBy(ring,c.session.read().load.playerUnitId) || ring.mode==3);
            });
            observed=observed && reward;
        }
        if(goal.questId==QuestId::DenOfEvil && step.flags==1 && questRun.skillPointsBefore) {
            const auto points=world.playerAttributes.find(5);
            observed=observed && points!=world.playerAttributes.end() && uint64_t(points->second)>=uint64_t(*questRun.skillPointsBefore)+1;
        }
        if(goal.questId==QuestId::SistersBurialGrounds && step.flags==1 && questRun.rewardTracked)
            observed=observed && (world.hireling || world.deadHirelingName);
        break;
    case QuestPending::Action::Object:
        if(!goal.requirements.empty() && std::any_of(goal.requirements.at(questRun.step).alternatives.begin(),goal.requirements.at(questRun.step).alternatives.end(),[](const auto &all) {
            return std::any_of(all.begin(),all.end(),[](const auto &fact){return fact.kind==QuestFact::Kind::ObjectOpened;});
        })) {observed=questRequirementSatisfied(c,goal,questRun.step);break;}
        if(step.action==QuestAction::SubmitStaff) observed=pending.unit && world.staffSource==pending.unit->id;
        else if(step.action==QuestAction::Portal) {
            if(step.witness) {
                const auto entrance=step.target?find(c,*step.target,true):std::nullopt;
                observed=entrance && c.town.permitsInteraction(c.session.read(),entrance->key);
            } else observed=unit!=world.units.end() && unit->second.assignmentRevision==pending.assignment &&
                unit->second.mode && *unit->second.mode!=0 &&
                std::any_of(c.town.read().mapTargets.begin(),c.town.read().mapTargets.end(),[&](const auto &target) {
                    return target.unit==unit->first && target.interaction==OnlineMapInteraction::Portal;
                });
        }
        else if(step.publishesTombSymbol) observed=pending.refreshIssued && flags && (*flags&1) &&
            world.quests.staffTombRevision>pending.beforeRevision && world.quests.staffTombOffset && *world.quests.staffTombOffset<7;
        else if(step.action==QuestAction::SmashOrb) observed=flags && (*flags&1) && !owned(c,step.item);
        else if(step.flags) observed=flags && (*flags&step.flags)==step.flags;
        else observed=unit!=world.units.end() && unit->second.assignmentRevision==pending.assignment && unit->second.mode && *unit->second.mode!=0;
        if(step.witness && step.action!=QuestAction::Portal) observed|=find(c,*step.witness).has_value();
        if(step.optional) observed|=world.quests.statuses[goal.questSlot].value_or(0)>0;
        break;
    case QuestPending::Action::Imbue:
        observed=questRun.materialPhase==4 && flags && (*flags&1);
        if(!observed && questRun.materialPhase==2 && world.itemRequest && world.itemRequest->sequence==pending.sequence &&
            world.itemRequest->state==OnlineItemRequest::State::Updated)
            for(const auto &[id,entry]:c.inventory.read().items) {
                const auto wire=world.items.find(id);
                if(wire!=world.items.end() && wire->second.code==step.item &&
                    (questRun.material==id || !questRun.materialBefore.contains(id)) && entry.decoded && entry.quality==6 &&
                    onlineItemOwnedBy(wire->second,c.session.read().load.playerUnitId)) {observed=true;break;}
            }
        break;
    }
    if(observed) {pending.resultObserved=true;note("quest-pending-result-observed");}
    else checkQuestPending(c);
}
void DebugAi::Impl::observeQuestInteraction(Context &c,const Json &intent) {
    if(questRun.pending || cursor>=goals.size() || goals[cursor].kind!=Kind::Quest) return;
    if(intent.value("purpose",std::string{})=="quest-orb") {
        if(questRun.step>=goals[cursor].steps.size() || goals[cursor].steps[questRun.step].action!=QuestAction::SmashOrb ||
            questRun.materialPhase>=2) throw std::runtime_error("Unexpected extra Compelling Orb operation");
        const auto now=Clock::now();++questRun.materialPhase;++interactions;
        questRun.sweepReady=now+std::chrono::milliseconds(600);
        if(questRun.materialPhase==2) {
            QuestPending pending;pending.action=QuestPending::Action::Object;
            pending.unit=OnlineUnitKey{intent.at("unitType").get<uint8_t>(),intent.at("unitId").get<uint32_t>()};
            const auto unit=c.session.read().world.units.find(*pending.unit);
            if(unit!=c.session.read().world.units.end()) pending.assignment=unit->second.assignmentRevision;
            pending.deadline=now+std::chrono::seconds(20);questRun.pending=pending;
        }
        note("quest-orb-operation-submitted",std::to_string(questRun.materialPhase));cancelWalk(c);return;
    }
    QuestPending p;p.action=intent.value("purpose",std::string{})=="quest-portal"?QuestPending::Action::Portal:QuestPending::Action::Object;
    if(intent.value("activation",false)) p.action=QuestPending::Action::Object;
    p.unit=OnlineUnitKey{intent.at("unitType").get<uint8_t>(),intent.at("unitId").get<uint32_t>()};
    const auto unit=c.session.read().world.units.find(*p.unit);
    if(unit!=c.session.read().world.units.end()) p.assignment=unit->second.assignmentRevision;
    p.deadline=Clock::now()+std::chrono::seconds(30);questRun.pending=p;
    ++interactions;note("quest-interaction-submitted",std::to_string(p.unit->id));cancelWalk(c);
}
void DebugAi::Impl::quest(Context &c,const Goal &goal) {
    if(questRun.step>=goal.steps.size()) return;
    auto step=goal.steps[questRun.step];
    const auto stateQuest=step.stateQuest.value_or(goal.questId);
    const auto state=questFlags(c,stateQuest);
    const auto parentState=questFlags(c,goal.questId);
    const auto tombOffset=tombSymbol;
    if(step.skipGranted) if(const auto prerequisite=questFlags(c,*step.skipGranted);prerequisite && (*prerequisite&1)) {
        questStepDone(c,"Actually granted prerequisite skips its old material/introductory steps");return;
    }
    if(step.item=="ass" && !questRun.pending && !questRun.skillPointsBefore && state &&
        (*state&0x2003) && !(*state&0x20) && !owned(c,"ass")) {
        questStepDone(c,"Actual Radament flags confirm the skill book was already used");return;
    }
    if((step.skipAny && state && (*state&step.skipAny)) ||
        (step.parentSkipAny && parentState && (*parentState&step.parentSkipAny)) ||
        (step.skipTombKnown && tombOffset && *tombOffset<7) ||
        (area && std::find(step.skipAreas.begin(),step.skipAreas.end(),*area)!=step.skipAreas.end())) {
        questStepDone(c,"Actual prerequisite state already satisfies this step");return;
    }
    if(step.trueTomb) {
        if(!tombOffset || *tombOffset>=7) {wait("quest-tomb-symbol","Waiting for Horazon's actual published tomb symbol");return;}
        step.area=definitions->area("Act 2 - Tomb Tal "+std::to_string(*tombOffset+1));
    }
    if(step.optional && step.flags) {
        const auto flags=state;
        if(flags && (*flags&step.flags)==step.flags) {
            questStepDone(c,"Optional introduction already confirmed by native quest flags");return;
        }
    }
    if(step.action==QuestAction::Talk && step.flags && state && (*state&step.flags)==step.flags &&
        !(*state&step.clearFlags) && step.item.empty() && step.produced.empty() && !questRun.pending &&
        !questRun.rewardTracked && !questRun.skillPointsBefore) {
        // Reused prerequisites can already be satisfied while we are in a
        // special area with no static route back to that NPC. Test the same
        // replicated dialogue postcondition before arranging needless travel.
        questStepDone(c,"Actual dialogue prerequisite already confirmed");return;
    }
    if(step.action==QuestAction::Acquire && (owned(c,step.item) || (!step.produced.empty() && owned(c,step.produced)))) {
        questStepDone(c,"Already-owned quest item satisfies acquisition: "+step.item);return;
    }
    if(step.action!=QuestAction::Portal && step.action!=QuestAction::ActTravel && step.area && (area!=step.area || travelDestination)) {
        const bool arrived=goal.questTown && step.area==goal.questTown?reachTown(c):travel(c,*step.area);
        if(!arrived) return;
    }
    bool done=false;
    switch(step.action) {
    case QuestAction::Travel:done=true;break;
    case QuestAction::UseItem:done=questUseItem(c,step);break;
    case QuestAction::Combine:done=questCombine(c,step);break;
    case QuestAction::SubmitStaff:done=questSubmitStaff(c,step);break;
    case QuestAction::SearchDrop:done=questSearchDrop(c,step);break;
    case QuestAction::EquipWeapon:done=questEquipWeapon(c,step);break;
    case QuestAction::SmashOrb:done=questSmashOrb(c,step);break;
    case QuestAction::Talk:done=questTalk(c,goal,step);break;
    case QuestAction::ReturnTown:done=reachTown(c);break;
    case QuestAction::Acquire:
        done=(!step.produced.empty() && owned(c,step.produced)) || acquireItem(c,questLeaf(goal));break;
    case QuestAction::ReadScroll:done=questReadScroll(c,step);break;
    case QuestAction::Stones:case QuestAction::Operate:done=questOperate(c,step);break;
    case QuestAction::Portal:done=questPortal(c,step);break;
    case QuestAction::Imbue:done=questImbue(c,goal,step);break;
    case QuestAction::ActTravel:done=questActTravel(c,goal,step);break;
    case QuestAction::Clear: {
        const auto flags=state;
        if((flags && (*flags&0x2002)) || (c.session.read().world.quests.denRemaining==0 &&
            c.session.read().world.quests.statuses[goal.questSlot]==5)) {done=true;break;}
        const auto remaining=c.session.read().world.quests.denRemaining;
        if(remaining!=questRun.denRemaining) {questRun.denRemaining=remaining;note("den-counter-observed",remaining?std::to_string(*remaining):"unknown");}
        std::optional<OnlineUnit> enemy;float nearest=std::numeric_limits<float>::max();
        for(const auto &[key,unit]:c.session.read().world.units) if(key.type==1 && unit.position && c.combat.hostile(unit) && c.combat.monsterTargetEligible(unit,false)) {
            const float distance=(Vec{float(unit.position->x),float(unit.position->y)}-c.position).length();
            if(distance<nearest) {nearest=distance;enemy=unit;}
        }
        if(enemy) {
            if(openDoor(c)) break;
            if(approachPoint(c,*enemy->position,"remaining assigned Den hostile",2)) break;
        }
        explore(c,"all Den of Evil hostiles, including reconstructed rooms outside the screen");
        if(phase=="explore-wait" && !c.walk.active() && Clock::now()>=questRun.sweepReady) {
            visited.clear();frontierAttempts.clear();checkedHints.clear();unavailableRooms.clear();++questRun.sweeps;
            questRun.sweepReady=Clock::now()+std::chrono::seconds(2);note("den-resweep",std::to_string(questRun.sweeps));
        }
        break;
    }
    case QuestAction::Hunt: {
        const auto flags=state;
        if(step.flags && flags && (*flags&step.flags)==step.flags) {done=true;break;}
        const auto unit=find(c,*step.target,true);
        if(unit && onlineMonsterCorpse(*unit)) {
            // Return to the real reward NPC even if the full player flag
            // snapshot is deferred until the next conversation. The outer
            // quest never completes from a corpse alone.
            done=true;break;
        }
        if(!unit) {if(!approachKnown(c,*step.target)) explore(c,step.target->name);break;}
        if(!c.combat.hostile(*unit)) throw std::runtime_error("Quest hunt target is not an actual hostile");
        target=unit->key;targetAssignment=unit->assignmentRevision;
        if(!approachPoint(c,*unit->position,"quest hunt target",2)) explore(c,"reachable quest target");
        break;
    }
    }
    if(done && !goal.requirements.empty()) {
        const auto &node=goal.requirements.at(questRun.step);
        const bool linkedObject=std::any_of(node.alternatives.begin(),node.alternatives.end(),[](const auto &all) {
            return std::any_of(all.begin(),all.end(),[](const auto &fact){return fact.kind==QuestFact::Kind::ObjectOpened;});
        });
        if(linkedObject && !questRequirementSatisfied(c,goal,questRun.step)) {
            if(!questRun.pending) {
                QuestPending pending;pending.action=QuestPending::Action::Object;pending.deadline=Clock::now()+std::chrono::seconds(20);
                if(step.target) if(const auto source=find(c,*step.target,true)) {pending.unit=source->key;pending.assignment=source->assignmentRevision;}
                questRun.pending=pending;
            }
            wait("quest-linked-object","Waiting for the linked quest object's actual opened mode: "+node.name);
            checkQuestPending(c);return;
        }
    }
    if(done) questStepDone(c,goal.quest+" step "+std::to_string(questRun.step));
}
}
