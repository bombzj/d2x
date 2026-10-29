#include "ai_impl.hpp"
#include "gameplay/quest/catalog.hpp"
#include "network/protocol/client_profile.hpp"
#include "resources/archive.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace d2x {
namespace {
std::string lower(std::string s) {for(auto &c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
int integer(const nlohmann::json &j,const char *key,int fallback,int lo,int hi) {
    if(!j.contains(key)) return fallback;
    const auto &v=j.at(key);if(!v.is_number_integer()) throw std::invalid_argument(std::string(key)+" must be an integer");
    const auto n=v.get<int64_t>();if(n<lo || n>hi) throw std::invalid_argument(std::string(key)+" outside supported range");
    return int(n);
}
uint16_t tableId(const DataTable &table,const nlohmann::json &value,std::string_view column,std::string_view idColumn,bool monster=false) {
    const bool numeric=value.is_number_integer();
    if(!numeric && !value.is_string()) throw std::invalid_argument("AI identity must be an MPQ name or integer ID");
    const int64_t number=numeric?value.get<int64_t>():-1;const auto name=numeric?std::string{}:lower(value.get<std::string>());
    std::optional<uint16_t> result;
    for(size_t row=0;row<table.rows().size();++row) {
        if(monster && table.value(row,"Id")=="Expansion") continue;
        const auto id=monster?std::optional{compiledMonsterIndex(table,row)}:table.number(row,idColumn);
        if(!id || *id<0 || *id>UINT16_MAX) continue;
        const auto label=lower(std::string(table.value(row,column)));
        const bool matches=numeric?number==*id:label==name || (column=="Name" && label.ends_with(" - "+name));
        if(!matches) continue;
        if(result && *result!=*id) throw std::invalid_argument("Ambiguous MPQ identity: "+name);
        result=uint16_t(*id);
    }
    if(!result) throw std::invalid_argument("AI identity is absent from the current MPQ: "+(numeric?std::to_string(number):name));
    return *result;
}
}
DebugAi::Impl::Definitions::Definitions(Archives &a):
    monsters(a.read("data/global/excel/monstats.txt")),objects(a.read("data/global/excel/objects.txt")),
    skills(a.read("data/global/excel/skills.txt")),levels(a.read("data/global/excel/levels.txt")),
    misc(a.read("data/global/excel/misc.txt")),weapons(a.read("data/global/excel/weapons.txt")),armor(a.read("data/global/excel/armor.txt")) {}
uint16_t DebugAi::Impl::Definitions::area(const Json &v) const {
    const auto id=tableId(levels,v,"Name","Id");
    if(id<1 || id>136) throw std::invalid_argument("AI travel currently supports ordinary retail areas 1-136");
    return id;
}
uint16_t DebugAi::Impl::Definitions::skill(const Json &v) const {return tableId(skills,v,"skill","Id");}
uint16_t DebugAi::Impl::Definitions::object(const Json &v) const {return tableId(objects,v,"Name","Id");}
uint16_t DebugAi::Impl::Definitions::malusSource() const {
    const auto id=object("Malus");
    for(size_t row=0;row<objects.rows().size();++row)
        if(objects.number(row,"Id")==id && objects.number(row,"OperateFn")==21 && item("hdm")) return id;
    throw std::invalid_argument("Current MPQ does not match the verified Malus source recipe");
}
std::optional<uint16_t> DebugAi::Impl::Definitions::hintArea(const Selector &s) const {
    if(s.type==1) {
        const auto row=findCompiledMonsterRow(monsters,s.identity);
        if(row && monsters.value(*row,"Id")=="smith") return area("Act 1 - Barracks");
        if(row && monsters.value(*row,"Id")=="charsi") return area("Act 1 - Town");
    }
    if(s.type==2) for(size_t row=0;row<objects.rows().size();++row)
        if(objects.number(row,"Id")==s.identity && objects.value(row,"Name")=="Malus") return area("Act 1 - Barracks");
    return {};
}
bool DebugAi::Impl::Definitions::item(std::string_view code) const {
    for(const auto *table:{&misc,&weapons,&armor}) for(size_t row=0;row<table->rows().size();++row)
        if(table->value(row,"code")==code) return true;
    return false;
}
DebugAi::Impl::Selector DebugAi::Impl::Definitions::selector(const Json &j) const {
    const unsigned count=unsigned(j.contains("monster"))+unsigned(j.contains("npc"))+unsigned(j.contains("object"));
    if(count!=1) throw std::invalid_argument("Exactly one of monster, npc or object is required");
    if(j.contains("object")) return {2,object(j.at("object")),j.at("object").dump()};
    const auto &v=j.at(j.contains("npc")?"npc":"monster");
    const auto id=tableId(monsters,v,"Id","",true);
    if(j.contains("npc")) {
        const auto row=findCompiledMonsterRow(monsters,id);
        if(!row || monsters.number(*row,"interact").value_or(0)==0) throw std::invalid_argument("NPC identity has no native interaction");
    }
    return {1,id,v.dump()};
}
std::vector<DebugAi::Impl::Goal> DebugAi::Impl::parse(const Json &request) {
    Json list;
    if(request.contains("scenario")) {
        if(request.contains("goals") || request.at("scenario")!="tools-of-the-trade")
            throw std::invalid_argument("Supported scenario: tools-of-the-trade; do not also supply goals");
        definitions->malusSource();
        // D2MOO A1Q3: Malus is created by Objects Operate21, not a Smith drop.
        list=Json::array({{{"action","hunt"},{"monster","smith"},{"area","Act 1 - Barracks"}},
            {{"action","acquire"},{"item","hdm"},{"source","Malus"},{"area","Act 1 - Barracks"}},
            {{"action","return-town"}},{{"action","deliver"},{"quest","tools-of-the-trade"}}});
    } else {
        if(!request.contains("goals")) throw std::invalid_argument("Supply a scenario or a nonempty goals array");
        list=request.at("goals");
    }
    if(!list.is_array() || list.empty() || list.size()>32) throw std::invalid_argument("AI plan requires 1-32 high-level goals");
    std::vector<Goal> result;
    for(const auto &entry:list) {
        if(!entry.is_object()) throw std::invalid_argument("AI goals must be objects");
        Goal goal;const auto action=entry.at("action").get<std::string>();
        goal.timeout=integer(entry,"timeoutSeconds",integer(request,"goalTimeoutSeconds",600,10,3600),10,3600);
        if(entry.contains("area")) goal.area=definitions->area(entry.at("area"));
        if(action=="travel") {goal.kind=Kind::Travel;if(!goal.area) throw std::invalid_argument("travel requires an area");}
        else if(action=="find" || action=="hunt") {
            goal.kind=action=="find"?Kind::Find:Kind::Hunt;goal.target=definitions->selector(entry);
            if(!goal.area) goal.area=definitions->hintArea(*goal.target);
            goal.interact=entry.value("interact",false);
            if(goal.interact && !entry.contains("npc")) throw std::invalid_argument("find interact=true requires an NPC");
            if(goal.kind==Kind::Hunt && (!entry.contains("monster") || goal.target->type!=1))
                throw std::invalid_argument("hunt requires a monster identity");
        } else if(action=="acquire") {
            goal.kind=Kind::Acquire;goal.item=lower(entry.at("item").get<std::string>());
            if(!definitions->item(goal.item)) throw std::invalid_argument("Item code absent from the current MPQ");
            if(entry.contains("source")) goal.sourceObject=definitions->object(entry.at("source"));
            if(goal.item=="hdm") {
                if(!goal.sourceObject) goal.sourceObject=definitions->malusSource();
                if(!goal.area) goal.area=definitions->area("Act 1 - Barracks");
            }
        } else if(action=="return-town") goal.kind=Kind::ReturnTown;
        else if(action=="deliver") {
            if(entry.value("quest",std::string{})!="tools-of-the-trade")
                throw std::invalid_argument("No verified delivery recipe for this quest; supported: tools-of-the-trade");
            goal.kind=Kind::Deliver;goal.item="hdm";goal.target=definitions->selector(Json{{"npc","charsi"}});
            goal.area=definitions->area("Act 1 - Town");
            goal.quest="tools-of-the-trade";goal.questSlot=questDefinition(QuestId::ToolsOfTheTrade).nativeSlot;
            goal.topics={163};
        } else throw std::invalid_argument("Unknown high-level AI action: "+action);
        result.push_back(std::move(goal));
    }
    return result;
}
DebugAi::Impl::CombatPolicy DebugAi::Impl::parseCombat(const Json &j) const {
    CombatPolicy p;if(!j.is_object()) throw std::invalid_argument("combat must be an object");
    p.enabled=j.value("enabled",true);if(j.contains("skill")) p.skill=definitions->skill(j.at("skill"));
    p.scope=j.value("scope",std::string("defensive"));
    if(p.scope!="defensive" && p.scope!="clear") throw std::invalid_argument("combat.scope must be defensive or clear");
    if(j.contains("aura")) {
        p.aura=definitions->skill(j.at("aura"));bool aura=false;
        for(size_t row=0;row<definitions->skills.rows().size();++row)
            if(definitions->skills.number(row,"Id")==p.aura) aura=definitions->skills.number(row,"aura").value_or(0)!=0;
        if(!aura) throw std::invalid_argument("combat.aura requires a native aura skill");
    }
    p.range=integer(j,"range",16,2,30);p.radius=integer(j,"crowdRadius",6,2,16);
    p.crowd=integer(j,"crowdCount",4,2,20);p.retreat=integer(j,"retreatDistance",8,3,20);
    p.engage=integer(j,"engageRadius",18,4,30);return p;
}
DebugAi::Impl::MobilityPolicy DebugAi::Impl::parseMobility(const Json &j) const {
    if(!j.is_object()) throw std::invalid_argument("mobility must be an object");
    MobilityPolicy p;p.mode=j.value("mode",std::string("auto"));p.vigor=j.value("vigor",true);
    if(p.mode!="auto" && p.mode!="walk" && p.mode!="teleport" && p.mode!="charge")
        throw std::invalid_argument("mobility.mode must be auto, walk, teleport or charge");
    p.distance=integer(j,"distance",24,8,30);return p;
}
void DebugAi::Impl::note(std::string event,std::string detail) {
    history.push_back({{"sequence",++sequence},{"goal",cursor},{"event",std::move(event)},{"detail",std::move(detail)}});
    while(history.size()>128) history.pop_front();
}
DebugAi::Impl::Json DebugAi::Impl::snapshot() const {
    Json plan=Json::array();static constexpr const char *names[]{"travel","find","hunt","acquire","return-town","deliver"};
    for(size_t i=0;i<goals.size();++i) {
        const auto &g=goals[i];Json v{{"index",i},{"action",names[size_t(g.kind)]},{"state",i<cursor?"completed":i==cursor?state:"queued"},
            {"timeoutSeconds",g.timeout},{"item",g.item},{"quest",g.quest}};
        if(g.area) v["area"]=*g.area;
        if(g.target) v["target"]={{"type",g.target->type},{"identity",g.target->identity},{"name",g.target->name}};
        plan.push_back(std::move(v));
    }
    return {{"state",state},{"active",active()},{"phase",phase},{"reason",reason},{"goalIndex",cursor},{"goals",std::move(plan)},
        {"gameGeneration",game},{"area",area?Json(*area):Json(nullptr)},{"decisionIntervalMs",300},{"decisions",decisions},
        {"combatTarget",combatTarget?Json{{"type",combatTarget->type},{"id",combatTarget->id}}:Json(nullptr)},
        {"target",target?Json{{"type",target->type},{"id",target->id},{"assignment",targetAssignment}}:Json(nullptr)},
        {"combatPolicy",{{"enabled",policy.enabled},{"skill",policy.skill?Json(*policy.skill):Json("Attack")},{"range",policy.range},
            {"scope",policy.scope},
            {"aura",policy.aura?Json(*policy.aura):Json("auto")},
            {"engageRadius",policy.engage},{"crowdRadius",policy.radius},{"crowdCount",policy.crowd},{"retreatDistance",policy.retreat}}},
        {"pending",{{"sourceInteraction",objectIssued},{"pickup",pickupIssued},{"portal",portalIssued},{"portalUsed",portalUsed}}},
        {"mobility",{{"mode",mobilityPolicy.mode},{"vigor",mobilityPolicy.vigor},{"distance",mobilityPolicy.distance},
            {"waitingForPosition",mobilityTarget.has_value()}}},
        {"exploration",{{"visitedRooms",visited.size()},{"observedFrontiers",exploredFrontiers.size()},
            {"knownExploredRooms",knownExploredRooms},
            {"mapCacheBuilds",mapCacheBuilds},{"pathSearches",pathSearches},{"unavailableRooms",unavailableRooms.size()},
            {"room",exploring?Json::array({std::get<0>(*exploring),std::get<1>(*exploring),std::get<2>(*exploring)}):Json(nullptr)},
            {"target",interest?Json{{"x",interest->x},{"y",interest->y}}:Json(nullptr)}}},
        {"castRequests",castRequests},{"navigationGoals",moveGoals},{"interactions",interactions},{"history",history}};
}
void DebugAi::Impl::stop(DebugAutoWalk &walk,RemoteControl &control,RemoteCombat &combat) {
    walk.manualInput(control);control.cancelMovement();
    OnlineCombatCommand command;command.action=OnlineCombatCommand::Action::Stop;
    try {if(!combat.submit(command)) note("stop-unavailable",combat.reason());}
    catch(const std::exception &e) {note("stop-unavailable",e.what());}
    combatOwned=false;mobilityHold=false;mobilityTarget.reset();
}
void DebugAi::Impl::cancelWalk(Context &c) {c.walk.manualInput(c.control);c.control.cancelMovement();walkIntent=nullptr;}
void DebugAi::Impl::finish(std::string value,std::string detail,Context &c) {
    stop(c.walk,c.control,c.combat);state=std::move(value);reason=std::move(detail);phase.clear();note(state,reason);
}
void DebugAi::Impl::resetStep() {
    phase.clear();reason.clear();target.reset();targetAssignment=0;visited.clear();exploredFrontiers.clear();checkedHints.clear();unavailableRooms.clear();exploring.reset();interest.reset();
    walkIntent=nullptr;operatedDoors.clear();pendingDoor.reset();doorDeadline={};
    travelDestination.reset();travelPhase=TravelPhase::Route;legDestination.reset();legUnit.reset();waypointDestination.reset();
    objectIssued=pickupIssued=portalIssued=portalAttempted=portalUsed=greetingIssued=false;
    pickupId.reset();oldPortals.clear();actionDeadline={};exitRetryAt={};
    mobilityTrials.clear();
}
void DebugAi::Impl::advance(Context &c,std::string evidence) {
    if(c.session.read().world.npcRequested) c.session.close_npc();
    if(c.session.read().world.waypointSource) c.session.use_waypoint(0);
    cancelWalk(c);note("goal-completed",std::move(evidence));++cursor;resetStep();
    if(cursor==goals.size()) {finish("completed","All goal postconditions observed",c);return;}
    goalDeadline=Clock::now()+std::chrono::seconds(goals[cursor].timeout);note("goal-started");
}
DebugAi::DebugAi(Archives &a):impl_(std::make_unique<Impl>(a)) {}
DebugAi::~DebugAi()=default;
bool DebugAi::active() const {return impl_->active();}
bool DebugAi::hasPlan() const {return impl_->live();}
bool DebugAi::controlsMovement() const {return impl_->active() && impl_->mobilityHold;}
void DebugAi::manualInput(net::RealmSession &,DebugAutoWalk &walk,RemoteControl &control,RemoteCombat &combat) {
    if(!impl_->live()) return;
    impl_->stop(walk,control,combat);impl_->state="cancelled";
    impl_->reason="Manual input took control";impl_->note("cancelled",impl_->reason);
}
std::optional<std::string> DebugAi::command(const nlohmann::json &j,net::RealmSession &session,RemoteTown &town,
        DebugAutoWalk &walk,RemoteControl &control,RemoteCombat &combat,RemoteInventory &) {
    const auto name=j.at("command").get<std::string>();if(!name.starts_with("debug-ai-")) return {};
    auto &s=*impl_;const auto now=Impl::Clock::now();
    if(name=="debug-ai-status") return Impl::Json{{"ok",true},{"ai",s.snapshot()}}.dump();
    if(name=="debug-ai-cancel") {
        if(s.live()) {manualInput(session,walk,control,combat);s.reason="Cancelled by debug command";}
    }
    else if(name=="debug-ai-pause") {
        if(s.active()) {
            const bool incomplete=walk.active();s.stop(walk,control,combat);
            if(incomplete) s.walkIntent=nullptr;
            s.state="paused";s.pausedAt=now;s.note("paused");
        }
    } else if(name=="debug-ai-resume") {
        if(s.state!="paused") throw std::invalid_argument("AI is not paused");
        if(s.game!=session.read().gameGeneration) throw std::invalid_argument("AI plan belongs to a previous game");
        if(s.areaGeneration!=session.read().world.areaGeneration || s.area!=town.read().area)
            throw std::invalid_argument("Paused AI area changed; cancel and start a new high-level plan");
        const auto duration=now-s.pausedAt;s.goalDeadline+=duration;
        if(s.actionDeadline!=Impl::Clock::time_point{}) s.actionDeadline+=duration;
        if(s.doorDeadline!=Impl::Clock::time_point{}) s.doorDeadline+=duration;
        s.next=now;s.state="running";s.note("resumed");
    } else if(name=="debug-ai-start" || name=="debug-ai-append") {
        const auto &view=session.read();
        if(view.protocolProfile!=net::protocol::clientCompatibility(net::protocol::ClientProfile::Lod113c).id)
            throw std::invalid_argument("Debug task recipes currently support the retail 1.13c profile only");
        if(view.stage!=OnlineStage::ProtocolReady || onlinePlayerDead(view.world) || !town.read().nativeMapReady)
            throw std::invalid_argument("AI requires a live player in a ready native map");
        if(onlineHasCursorItem(view.world.items,view.load.playerUnitId) || view.world.playerTrade.active())
            throw std::invalid_argument("Finish the cursor item or player trade before starting AI");
        if(!s.definitions) s.definitions=std::make_unique<Impl::Definitions>(s.archives);
        auto plan=s.parse(j);auto policy=s.parseCombat(j.value("combat",Impl::Json::object()));
        const auto mobility=s.parseMobility(j.value("mobility",Impl::Json::object()));
        if(name=="debug-ai-start" && policy.enabled && !policy.skill) {
            combat.update();
            for(const auto name:{"Frozen Orb","Fire Ball","Zeal","Attack"}) {
                const auto id=s.definitions->skill(name);
                if(std::any_of(combat.skills().begin(),combat.skills().end(),[&](const auto &v){return v.id==id && (v.level || v.innate) && !v.passive;})) {
                    policy.skill=id;break;
                }
            }
        }
        const bool fighting=name=="debug-ai-append"?s.policy.enabled:policy.enabled;
        if(!fighting && std::any_of(plan.begin(),plan.end(),[](const auto &g){return g.kind==Impl::Kind::Hunt;}))
            throw std::invalid_argument("Hunt goals require combat to be enabled");
        if(name=="debug-ai-start" && policy.enabled && policy.skill) {
            combat.update();
            if(std::none_of(combat.skills().begin(),combat.skills().end(),[&](const auto &v){return v.id==policy.skill && (v.level || v.innate) && !v.passive;}))
                throw std::invalid_argument("Configured skill is not currently a known active character skill");
        }
        if(name=="debug-ai-start" && policy.aura) {
            combat.update();
            if(std::none_of(combat.skills().begin(),combat.skills().end(),[&](const auto &v){return v.id==policy.aura && v.level && !v.passive;}))
                throw std::invalid_argument("Configured battle aura is not currently learned");
        }
        if(name=="debug-ai-start" && policy.enabled) {
            combat.update();bool aura=policy.aura.has_value();
            for(const auto candidate:{"Fanaticism","Concentration","Might"}) {
                const auto id=s.definitions->skill(candidate);
                aura=aura || std::any_of(combat.skills().begin(),combat.skills().end(),[&](const auto &v){return v.id==id && v.level && !v.passive;});
            }
            const auto attack=policy.skill.value_or(s.definitions->skill("Attack"));
            if(aura && std::none_of(combat.skills().begin(),combat.skills().end(),[&](const auto &v){return v.id==attack && v.left && (v.level || v.innate) && !v.passive;}))
                throw std::invalid_argument("A battle aura requires an available left-hand attack skill");
        }
        if(j.contains("scenario")) {
            const auto level=view.world.playerAttributes.find(12);
            if(level!=view.world.playerAttributes.end() && level->second<8)
                throw std::invalid_argument("Original Malus pickup requires character level 8");
        }
        if(j.contains("scenario") && view.world.quests.playerFlags &&
            ((*view.world.quests.playerFlags)[questDefinition(QuestId::ToolsOfTheTrade).nativeSlot]&3))
            throw std::invalid_argument("Tools of the Trade already has its reward or pending imbue; no new Malus can be obtained");
        if(name=="debug-ai-append") {
            if(j.contains("combat") || j.contains("mobility")) throw std::invalid_argument("Append inherits the current plan's combat and mobility policies");
            if(!s.live() || s.game!=view.gameGeneration) throw std::invalid_argument("No live AI plan to append to");
            if(s.goals.size()+plan.size()>32) throw std::invalid_argument("AI queue exceeds 32 goals");
            s.goals.insert(s.goals.end(),plan.begin(),plan.end());s.note("goals-appended");
        } else {
            if(s.live()) throw std::invalid_argument("Cancel the current AI plan before replacing it");
            s.stop(walk,control,combat);
            if(view.world.npcRequested) session.close_npc();
            if(view.world.waypointSource) session.use_waypoint(0);
            s.policy=policy;s.mobilityPolicy=mobility;s.goals=std::move(plan);s.cursor=0;s.resetStep();s.history.clear();s.sequence=s.decisions=0;
            s.castRequests=s.moveGoals=s.interactions=0;s.game=view.gameGeneration;s.areaGeneration=view.world.areaGeneration;s.area=town.read().area;
            s.combatTarget.reset();s.retreatPoint.reset();s.combatOwned=false;
            s.lastAttack={};s.retreatUntil={};s.retreatReady={};
            s.mobilityTarget.reset();s.mobilityHold=false;s.nextMobility={};s.lastSelection={};
            s.goalDeadline=now+std::chrono::seconds(s.goals.front().timeout);s.next=now;s.state="running";s.note("plan-started");
        }
    } else throw std::invalid_argument("Unknown debug AI command");
    return Impl::Json{{"ok",true},{"ai",s.snapshot()}}.dump();
}
void DebugAi::tick(net::RealmSession &session,RemoteTown &town,DebugAutoWalk &walk,RemoteControl &control,
        RemoteCombat &combat,RemoteInventory &inventory,bool blocked,std::optional<Vec> position,bool moving) {
    auto &s=*impl_;if(!s.active()) return;
    const auto now=Impl::Clock::now();const auto &view=session.read();
    if(now<s.next) return;
    s.next=now+std::chrono::milliseconds(300);++s.decisions;
    Impl::Context c{session,town,walk,control,combat,inventory,position.value_or(Vec{}),moving};
    try {
        if(view.gameGeneration!=s.game) {
            s.finish("cancelled","Game changed",c);return;
        }
        if(now>=s.goalDeadline) {s.finish("failed","Current high-level goal exceeded its deadline",c);return;}
        if(view.error && view.error->kind!=OnlineErrorKind::Input) {
            const bool portalTimeout=s.cursor<s.goals.size() && s.goals[s.cursor].kind==Impl::Kind::ReturnTown &&
                s.portalIssued && !s.portalUsed && view.error->kind==OnlineErrorKind::Timeout &&
                view.error->message=="No new server-owned town portal was assigned";
            if(!portalTimeout) {s.finish("failed",view.error->message,c);return;}
            // The native request's result is unknown: never cast again or
            // assume a scroll was consumed. Use a separate ordinary walk home.
            s.note("town-portal-unconfirmed",view.error->message);
            s.stop(walk,control,combat);s.walkIntent=nullptr;s.portalIssued=false;
        }
        if(view.stage!=OnlineStage::ProtocolReady) {s.wait("loading","Waiting for the normal protocol/map transition");return;}
        if(onlinePlayerDead(view.world)) {s.finish("failed","Player died; AI never fabricates resurrection",c);return;}
        if(view.world.playerTrade.active()) {s.finish("cancelled","Player trade took control",c);return;}
        if(blocked || !position || !town.read().nativeMapReady || !town.read().area) return;
        if(s.area!=town.read().area || s.areaGeneration!=view.world.areaGeneration) {
            s.cancelWalk(c);s.area=town.read().area;s.areaGeneration=view.world.areaGeneration;s.visited.clear();s.exploredFrontiers.clear();s.exploring.reset();
            s.checkedHints.clear();s.unavailableRooms.clear();
            s.combatTarget.reset();s.combatOwned=false;s.retreatPoint.reset();s.note("area-observed",std::to_string(*s.area));
            s.mobilityTarget.reset();s.mobilityHold=false;
        }
        if(s.cursor>=s.goals.size()) {s.finish("completed","All goal postconditions observed",c);return;}
        inventory.update(view);combat.update();
        s.mobilityHold=false;
        const auto goal=s.goals[s.cursor];
        s.observeChild(c,goal);
        if(s.pendingDoor) {
            const auto door=view.world.units.find(s.pendingDoor->first);
            if(door==view.world.units.end() || door->second.assignmentRevision!=s.pendingDoor->second) {
                s.note("door-assignment-withdrawn");s.pendingDoor.reset();
            } else if(door->second.mode && *door->second.mode!=0) {
                s.operatedDoors.insert(*s.pendingDoor);
                s.note("door-mode-observed",std::to_string(s.pendingDoor->first.id));s.pendingDoor.reset();
            } else {
                if(now>=s.doorDeadline) throw std::runtime_error("Door interaction submitted but the assigned door stayed closed: "+std::to_string(s.pendingDoor->first.id));
                s.wait("door-wait","Waiting for the actual door mode change; submission is not an open door");return;
            }
        }
        if(s.mobilityTarget && s.mobility(c)) return;
        const bool transferPending=s.travelPhase==Impl::TravelPhase::WaypointWait ||
            (s.travelPhase==Impl::TravelPhase::CrossWait && s.legUnit) || s.portalIssued;
        if(!transferPending && s.combat(c,goal)) return;
        s.execute(c,goal);
        if(s.active() && !transferPending) s.mobility(c);
    } catch(const std::exception &e) {s.finish("failed",e.what(),c);}
}
}
