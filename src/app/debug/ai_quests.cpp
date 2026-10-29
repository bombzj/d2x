#include "ai_impl.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace d2x {
void DebugAi::Impl::configureQuest(Goal &goal) const {
    static constexpr std::array<std::pair<std::string_view,QuestId>,6> quests{{
        {"den-of-evil",QuestId::DenOfEvil},{"sisters-burial-grounds",QuestId::SistersBurialGrounds},
        {"search-for-cain",QuestId::SearchForCain},{"forgotten-tower",QuestId::ForgottenTower},
        {"tools-of-the-trade",QuestId::ToolsOfTheTrade},{"sisters-to-the-slaughter",QuestId::SistersToTheSlaughter}}};
    const auto found=std::find_if(quests.begin(),quests.end(),[&](const auto &v){return v.first==goal.quest;});
    if(found==quests.end()) throw std::invalid_argument("No verified Act I quest recipe: "+goal.quest);
    goal.questId=found->second;goal.questSlot=questDefinition(goal.questId).nativeSlot;
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
    // Native Act-I change record (slot 7) is separate from Andariel's reward.
    return goal.questId!=QuestId::SistersToTheSlaughter ||
        (c.session.read().world.quests.playerFlags && ((*c.session.read().world.quests.playerFlags)[7]&1));
}
bool DebugAi::Impl::questBusy() const {
    if(cursor>=goals.size() || goals[cursor].kind!=Kind::Quest || supplyStage!=SupplyStage::None) return false;
    return questRun.pending || (questRun.step<goals[cursor].steps.size() && goals[cursor].steps[questRun.step].action==QuestAction::Imbue);
}
void DebugAi::Impl::questStepDone(Context &c,std::string evidence) {
    if(c.session.read().world.npcRequested) c.session.close_npc();
    if(c.session.read().world.waypointSource) c.session.use_waypoint(0);
    cancelWalk(c);note("quest-step-completed",std::move(evidence));
    const auto step=questRun.step+1;questRun={};questRun.initialized=true;questRun.step=step;resetStep();
}
bool DebugAi::Impl::prepareQuest(Context &c,const Goal &goal) {
    if(goal.kind!=Kind::Quest) return true;
    if(supplyStage!=SupplyStage::None) return true;
    const auto flags=questFlags(c,goal.questId);
    if(!flags) {wait("quest-state","Waiting for actual native character quest flags");return false;}
    if(!questRun.initialized) {
        if(questFinished(c,goal)) {advance(c,"Already completed native quest: "+goal.quest);return false;}
        if(goal.questId==QuestId::SearchForCain && (*flags&0x8000) && !(*flags&3))
            throw std::runtime_error("Cain was rescued by rogues in this save; use a fresh quest baseline for the full rescue");
        if((*flags&0x4000) && !(*flags&0x2003)) throw std::runtime_error("Original quest is unavailable to this character in this game: "+goal.quest);
        if(c.session.read().load.act!=0) throw std::runtime_error("Unfinished Act I quest needs a normal Act I scene");
        questRun.initialized=true;note("quest-started",goal.quest);
    }
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
    if(p.sequence && request) {
        if(p.sequence!=request->sequence) throw std::runtime_error("Quest item operation was replaced before its result was observed");
        if(request->state==OnlineItemRequest::State::Rejected || request->state==OnlineItemRequest::State::TimedOut || request->state==OnlineItemRequest::State::Interrupted)
            throw std::runtime_error("Quest item operation did not confirm its actual result; no blind retry");
    }
    if(Clock::now()>=p.deadline) throw std::runtime_error("Quest operation's actual result was not observed: "+phase);
}
void DebugAi::Impl::observeQuestInteraction(Context &c,const Json &intent) {
    if(questRun.pending || cursor>=goals.size() || goals[cursor].kind!=Kind::Quest) return;
    QuestPending p;p.action=intent.value("purpose",std::string{})=="quest-portal"?QuestPending::Action::Portal:QuestPending::Action::Object;
    p.unit=OnlineUnitKey{intent.at("unitType").get<uint8_t>(),intent.at("unitId").get<uint32_t>()};
    const auto unit=c.session.read().world.units.find(*p.unit);
    if(unit!=c.session.read().world.units.end()) p.assignment=unit->second.assignmentRevision;
    p.deadline=Clock::now()+std::chrono::seconds(30);questRun.pending=p;
    ++interactions;note("quest-interaction-submitted",std::to_string(p.unit->id));cancelWalk(c);
}
void DebugAi::Impl::quest(Context &c,const Goal &goal) {
    if(questRun.step>=goal.steps.size()) return;
    const auto &step=goal.steps[questRun.step];
    if(step.optional && step.flags) {
        const auto flags=questFlags(c,goal.questId);
        if(flags && (*flags&step.flags)==step.flags) {
            questStepDone(c,"Optional introduction already confirmed by native quest flags");return;
        }
    }
    if(step.action==QuestAction::Acquire && (owned(c,step.item) || (!step.produced.empty() && owned(c,step.produced)))) {
        questStepDone(c,"Already-owned quest item satisfies acquisition: "+step.item);return;
    }
    if(step.action!=QuestAction::Portal && step.action!=QuestAction::ActTravel && step.area &&
        (area!=step.area || travelDestination) && !travel(c,*step.area)) return;
    bool done=false;
    switch(step.action) {
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
        const auto flags=questFlags(c,goal.questId);
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
            visited.clear();exploredFrontiers.clear();checkedHints.clear();unavailableRooms.clear();++questRun.sweeps;
            questRun.sweepReady=Clock::now()+std::chrono::seconds(2);note("den-resweep",std::to_string(questRun.sweeps));
        }
        break;
    }
    case QuestAction::Hunt: {
        const auto flags=questFlags(c,goal.questId);
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
    if(done) questStepDone(c,goal.quest+" step "+std::to_string(questRun.step));
}
}
