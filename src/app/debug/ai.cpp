#include "ai_impl.hpp"
#include "gameplay/quest/catalog.hpp"
#include "network/protocol/client_profile.hpp"
#include "resources/archive.hpp"
#include <raylib.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <stdexcept>

namespace d2x {
namespace {
constexpr const char *goalNames[]{"travel","find","hunt","acquire","return-town","deliver","restock","quest"};
constexpr std::array classNames{"amazon","sorceress","necromancer","paladin","barbarian","druid","assassin"};
std::string lower(std::string s) {for(auto &c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
struct ActSelection {int first,last;};
std::optional<ActSelection> actSelection(std::string_view name) {
    if(!name.starts_with("act") || name.size()<4 || name[3]<'0' || name[3]>'9') return {};
    if((name.size()!=4 && name.size()!=6) || name[3]<'1' || name[3]>'5' ||
        (name.size()==6 && (name[4]!='-' || name[5]<name[3] || name[5]>'5')))
        throw std::invalid_argument("Act selection must be act1..act5 or an ascending range such as act1-5");
    return ActSelection{name[3]-'0',name.size()==4?name[3]-'0':name[5]-'0'};
}
nlohmann::json reloadPolicyNumbers(const nlohmann::json &config) {
    using Json=nlohmann::json;
    auto result=Json::object();
    const auto copy=[&](const char *section,std::initializer_list<const char *> keys) {
        if(!config.contains(section)) return;
        const auto &values=config.at(section);
        if(!values.is_object()) throw std::invalid_argument(std::string("AI policy must be an object: ")+section);
        result[section]=Json::object();
        for(const auto key:keys) if(values.contains(key)) result[section][key]=values.at(key);
    };
    copy("combat",{"range","crowdRadius","crowdCount","retreatDistance","engageRadius","noProgressSeconds",
        "stallTimeoutSeconds","retreatLifePercent","lifeDropPercent","rangedKite","kiteIntervalMs","kiteDistance",
        "kiteMinDistance","hazardLookaheadMs","hazardPadding"});
    copy("mobility",{"distance"});
    copy("supplies",{"lifePercent","manaPercent","criticalLifePercent","healthCount","manaCount","intervalMs"});
    copy("events",{"waitSeconds","initialWaitMs","doorInitialWaitMs","scrollInitialWaitMs","questLogDurationMs","waypointCloseDelayMs","encounterRadius","standoffMin","standoffMax",
        "walkDecisionMs","walkReactionMs","walkRequestIntervalMs","walkRetryMs","townTurnBaseMs","townTurnMinMs","townTurnMaxMs"});
    return result;
}
nlohmann::json classCombat(nlohmann::json combat,std::optional<uint16_t> characterClass) {
    if(!combat.is_object() || !combat.contains("byClass")) return combat;
    const auto profiles=combat.at("byClass");
    if(!profiles.is_object()) throw std::invalid_argument("combat.byClass must be an object");
    for(const auto &[name,profile]:profiles.items()) {
        if(std::find(classNames.begin(),classNames.end(),name)==classNames.end() || !profile.is_object())
            throw std::invalid_argument("combat.byClass requires one of the seven class names and an object: "+name);
        for(const auto &[key,value]:profile.items()) {
            (void)value;
            if(key!="skill" && key!="fallbackSkills" && key!="aura")
                throw std::invalid_argument("Class combat profiles only configure skill, fallbackSkills and aura: "+key);
        }
    }
    if(!characterClass || *characterClass>=classNames.size())
        throw std::invalid_argument("Current character class is unavailable for combat.byClass");
    combat.erase("byClass");
    if(profiles.contains(classNames[*characterClass])) combat.merge_patch(profiles.at(classNames[*characterClass]));
    return combat;
}
nlohmann::json configuredAiRequest(const nlohmann::json &request,std::optional<uint16_t> characterClass) {
    auto explicitRequest=request;
    const bool starting=request.value("command",std::string{})=="debug-ai-start";
    const bool appending=request.value("command",std::string{})=="debug-ai-append";
    const bool reloading=request.value("command",std::string{})=="debug-ai-reload-config";
    if(explicitRequest.contains("scenario")) {
        if(!explicitRequest.at("scenario").is_string()) throw std::invalid_argument("AI scenario must be a string");
        const auto scenario=explicitRequest.at("scenario").get<std::string>();
        if(actSelection(lower(scenario))) explicitRequest["scenario"]=lower(scenario);
        else {
            if(explicitRequest.contains("configFile") || explicitRequest.contains("goals") || explicitRequest.contains("questNumber"))
                throw std::invalid_argument("A named AI configuration cannot be combined with another file or task selector");
            if(scenario.empty() || !std::all_of(scenario.begin(),scenario.end(),[](unsigned char c) {
                return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-';
            })) throw std::invalid_argument("AI configuration names use letters, digits, underscores and hyphens");
            explicitRequest["configFile"]="debug-ai-"+scenario+".json";
            explicitRequest.erase("scenario");
        }
    }
    if(starting && explicitRequest.contains("combat"))
        explicitRequest["combat"]=classCombat(explicitRequest.at("combat"),characterClass);
    if(!explicitRequest.contains("configFile") && (starting || (appending && explicitRequest.contains("scenario"))))
        explicitRequest["configFile"]="debug-ai.json";
    if(!explicitRequest.contains("configFile")) return explicitRequest;
    if(!explicitRequest.at("configFile").is_string()) throw std::invalid_argument("configFile must be a UTF-8 path");
    const auto name=explicitRequest.at("configFile").get<std::string>();
    auto path=std::filesystem::path(std::u8string(name.begin(),name.end()));
    if(path.empty()) throw std::invalid_argument("AI configuration path must not be empty");
    if(path.is_relative()) {
        const std::string directory=GetApplicationDirectory();
        const auto besideExecutable=std::filesystem::path(std::u8string(directory.begin(),directory.end()))/path;
        if(std::filesystem::exists(besideExecutable)) path=besideExecutable;
    }
    path=std::filesystem::absolute(path).lexically_normal();
    if(std::filesystem::file_size(path)>65536) throw std::invalid_argument("AI configuration must be a JSON file of at most 64 KiB");
    std::ifstream input(path,std::ios::binary);
    if(!input) throw std::invalid_argument("Unable to read AI configuration file");
    auto config=nlohmann::json::parse(input);
    if(!config.is_object() || config.contains("configFile") || config.contains("command"))
        throw std::invalid_argument("AI configuration must be an object without command or nested configFile");
    if(config.contains("combat") && starting) config["combat"]=classCombat(config.at("combat"),characterClass);
    // A complete configuration also serves numeric reloads and inherited-policy
    // appends. Only an explicit new start can replace skills or execution modes.
    if(reloading) config=reloadPolicyNumbers(config);
    else if(appending) {
        auto plan=nlohmann::json::object();
        for(const auto key:{"goals","scenario","questNumber","goalTimeoutSeconds","useServiceRewards"})
            if(config.contains(key)) plan[key]=config.at(key);
        config=std::move(plan);
    }
    if(explicitRequest.contains("goals")) {config.erase("scenario");config.erase("questNumber");}
    else if(explicitRequest.contains("scenario") && !explicitRequest.contains("questNumber")) config.erase("questNumber");
    config.merge_patch(explicitRequest); // Explicit command arguments override even class defaults.
    // Reload the file that was actually opened, even if the working directory
    // changes or another relative candidate is created while the plan runs.
    const auto resolved=path.generic_u8string();
    config["configFile"]=std::string(resolved.begin(),resolved.end());
    return config;
}
nlohmann::json policyNumbers(nlohmann::json current,const nlohmann::json &patch) {
    if(!patch.is_object()) throw std::invalid_argument("AI policy configuration must be an object");
    for(const auto &[key,value]:patch.items()) {
        if(!current.contains(key)) throw std::invalid_argument("Live AI configuration cannot change "+key+"; submit a new plan for skills, modes or goals");
        current[key]=value;
    }
    return current;
}
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
    int monsterIndex=0;
    for(size_t row=0;row<table.rows().size();++row) {
        // Compiled MonStats identities count records with hcIdx, not source rows.
        // Advance once per record instead of rescanning every preceding row.
        if(monster && table.value(row,"hcIdx").empty()) continue;
        const auto id=monster?std::optional{monsterIndex++}:table.number(row,idColumn);
        if(monster && table.value(row,"Id")=="Expansion") continue;
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
    misc(a.read("data/global/excel/misc.txt")),weapons(a.read("data/global/excel/weapons.txt")),armor(a.read("data/global/excel/armor.txt")),
    types(a.read("data/global/excel/itemtypes.txt")),states(a.read("data/global/excel/states.txt")),npcs(a.read("data/global/excel/npc.txt")),
    superuniques(a.read("data/global/excel/superuniques.txt")),missiles(a.read("data/global/excel/missiles.txt")),
    difficulties(a.read("data/global/excel/difficultylevels.txt")) {
    loadPotions();loadCombatObservations();
    for(size_t row=0;row<objects.rows().size();++row) if(objects.number(row,"OperateFn")==61) {
        const auto id=objects.number(row,"Id");
        if(id && *id>=0 && *id<=UINT16_MAX) {harrogathGate=uint16_t(*id);break;}
    }
}
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
        if(row) {
            const auto name=monsters.value(*row,"Id");
            if(name=="charsi" || name=="akara" || name=="kashya" || name=="cain5" || name=="warriv1") return area("Act 1 - Town");
            if(name=="bloodraven") return area("Act 1 - Graveyard");
            if(name=="andariel") return area("Act 1 - Catacombs 4");
        }
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
    const unsigned count=unsigned(j.contains("monster"))+unsigned(j.contains("npc"))+unsigned(j.contains("object"))+unsigned(j.contains("superunique"));
    if(count!=1) throw std::invalid_argument("Exactly one of monster, npc, object or superunique is required");
    if(j.contains("superunique")) {
        const auto id=tableId(superuniques,j.at("superunique"),"Superunique","hcIdx");
        for(size_t row=0;row<superuniques.rows().size();++row) if(superuniques.number(row,"hcIdx")==id) {
            auto result=selector(Json{{"monster",std::string(superuniques.value(row,"Class"))}});
            result.superUnique=id;result.name=j.at("superunique").dump();return result;
        }
        throw std::invalid_argument("SuperUnique has no current MPQ class");
    }
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
    std::optional<ActSelection> acts;
    if(request.contains("scenario")) {
        acts=actSelection(request.at("scenario").get<std::string>());
        if(!acts) throw std::invalid_argument("Unknown AI act selection");
    }
    const auto questNumber=request.contains("questNumber")?std::optional{integer(request,"questNumber",0,1,6)}:std::nullopt;
    if(questNumber && (!acts || acts->first!=acts->last))
        throw std::invalid_argument("questNumber requires one act; use actN questN");
    if(!request.contains("goals"))
        throw std::invalid_argument("AI configuration requires a nonempty goals array; update the runtime debug-ai.json");
    const auto &list=request.at("goals");
    if(!list.is_array() || list.empty() || list.size()>32) throw std::invalid_argument("AI plan requires 1-32 high-level goals");
    std::vector<Goal> result;
    for(const auto &entry:list) {
        if(!entry.is_object()) throw std::invalid_argument("AI goals must be objects");
        Goal goal;const auto action=entry.at("action").get<std::string>();
        if(acts) {
            if(action!="quest") continue;
            const auto id=questIdentity(entry.at("quest").get<std::string>());
            if(!id) throw std::invalid_argument("No verified quest recipe: "+entry.at("quest").get<std::string>());
            const auto &definition=questDefinition(*id);
            if(definition.act+1<acts->first || definition.act+1>acts->last ||
                (questNumber && definition.displaySlot+1!=unsigned(*questNumber))) continue;
        }
        goal.useServiceRewards=entry.value("useServiceRewards",request.value("useServiceRewards",false));
        goal.timeout=integer(entry,"timeoutSeconds",integer(request,"goalTimeoutSeconds",action=="quest"?1800:600,10,3600),10,3600);
        if(entry.contains("area")) goal.area=definitions->area(entry.at("area"));
        if(action=="quest") {
            if(goal.area) throw std::invalid_argument("quest resolves its own native areas; omit area");
            goal.kind=Kind::Quest;goal.quest=entry.at("quest").get<std::string>();configureQuest(goal);
        }
        else if(action=="travel") {goal.kind=Kind::Travel;if(!goal.area) throw std::invalid_argument("travel requires an area");}
        else if(action=="find" || action=="hunt") {
            goal.kind=action=="find"?Kind::Find:Kind::Hunt;goal.target=definitions->selector(entry);
            if(!goal.area) goal.area=definitions->hintArea(*goal.target);
            goal.interact=entry.value("interact",false);
            if(goal.interact && !entry.contains("npc")) throw std::invalid_argument("find interact=true requires an NPC");
            if(goal.kind==Kind::Hunt && ((!entry.contains("monster") && !entry.contains("superunique")) || goal.target->type!=1))
                throw std::invalid_argument("hunt requires a monster or superunique identity");
        } else if(action=="acquire") {
            goal.kind=Kind::Acquire;goal.item=lower(entry.at("item").get<std::string>());
            if(!definitions->item(goal.item)) throw std::invalid_argument("Item code absent from the current MPQ");
            if(entry.contains("source")) goal.sourceObject=definitions->object(entry.at("source"));
            if(goal.item=="hdm") {
                if(!goal.sourceObject) goal.sourceObject=definitions->malusSource();
                if(!goal.area) goal.area=definitions->area("Act 1 - Barracks");
            }
        } else if(action=="return-town") goal.kind=Kind::ReturnTown;
        else if(action=="restock") {
            if(goal.area) throw std::invalid_argument("restock uses the current act's town; omit area");
            goal.kind=Kind::Restock;
        }
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
    if(result.empty()) throw std::invalid_argument("No configured quest matches the selected act or original MPQ quest number");
    bool birdRequested=false;
    for(auto it=result.rbegin();it!=result.rend();++it) {
        if(it->kind==Kind::Quest && it->questId==QuestId::GoldenBird) birdRequested=true;
        if(!birdRequested) it->routeDrop.reset();
    }
    return result;
}
DebugAi::Impl::CombatPolicy DebugAi::Impl::parseCombat(const Json &j) const {
    CombatPolicy p;if(!j.is_object()) throw std::invalid_argument("combat must be an object");
    p.enabled=j.value("enabled",true);
    const auto candidate=[&](const Json &value) {
        if(value.is_object()) for(const auto &[key,entry]:value.items()) {
            (void)entry;
            if(key!="skill" && key!="minLevel") throw std::invalid_argument("Unknown combat skill option: "+key);
        }
        const auto id=definitions->skill(value.is_object()?value.at("skill"):value);
        const auto minimum=unsigned(value.is_object()?integer(value,"minLevel",1,1,255):1);
        const auto row=definitions->skillRows.find(id);
        if(row==definitions->skillRows.end() || definitions->skills.number(row->second,"passive").value_or(0) ||
            definitions->skills.number(row->second,"aura").value_or(0))
            throw std::invalid_argument("Combat attacks require an active non-aura skill: "+std::to_string(id));
        const auto [found,inserted]=p.minimumSkillLevels.emplace(id,minimum);
        if(!inserted && found->second!=minimum)
            throw std::invalid_argument("Duplicate combat skill has different minLevel: "+std::to_string(id));
        return id;
    };
    if(j.contains("skill")) p.skill=candidate(j.at("skill"));
    if(j.contains("fallbackSkills")) {
        const auto &list=j.at("fallbackSkills");
        if(!list.is_array() || list.size()>8) throw std::invalid_argument("combat.fallbackSkills must contain at most eight known active skills");
        for(const auto &value:list) {
            const auto id=candidate(value);
            if(id!=p.skill && std::find(p.fallbackSkills.begin(),p.fallbackSkills.end(),id)==p.fallbackSkills.end()) p.fallbackSkills.push_back(id);
        }
    }
    p.scope=j.value("scope",std::string("defensive"));
    p.staticField=j.value("staticField",true);p.avoidHazards=j.value("avoidHazards",true);
    if(p.scope!="defensive" && p.scope!="clear" && p.scope!="objective") throw std::invalid_argument("combat.scope must be defensive, clear or objective");
    if(j.contains("aura")) {
        p.aura=definitions->skill(j.at("aura"));bool aura=false;
        for(size_t row=0;row<definitions->skills.rows().size();++row)
            if(definitions->skills.number(row,"Id")==p.aura) aura=definitions->skills.number(row,"aura").value_or(0)!=0;
        if(!aura) throw std::invalid_argument("combat.aura requires a native aura skill");
    }
    p.range=integer(j,"range",16,2,30);p.radius=integer(j,"crowdRadius",6,2,16);
    p.crowd=integer(j,"crowdCount",4,2,20);p.retreat=integer(j,"retreatDistance",8,3,20);
    p.engage=integer(j,"engageRadius",18,4,30);
    p.rangedKite=j.value("rangedKite",true);
    p.kiteIntervalMs=integer(j,"kiteIntervalMs",900,200,5000);
    p.kiteDistance=integer(j,"kiteDistance",6,3,14);
    p.kiteMinDistance=integer(j,"kiteMinDistance",8,4,20);
    p.hazardLookaheadMs=integer(j,"hazardLookaheadMs",350,100,1500);
    p.hazardPadding=integer(j,"hazardPadding",3,0,6);
    p.noProgressSeconds=integer(j,"noProgressSeconds",6,3,30);
    p.stallTimeoutSeconds=integer(j,"stallTimeoutSeconds",90,15,300);
    if(p.stallTimeoutSeconds<p.noProgressSeconds*2) throw std::invalid_argument("combat.stallTimeoutSeconds must allow at least two observation windows");
    p.retreatLifePercent=integer(j,"retreatLifePercent",45,5,90);p.lifeDropPercent=integer(j,"lifeDropPercent",10,1,80);return p;
}
DebugAi::Impl::MobilityPolicy DebugAi::Impl::parseMobility(const Json &j) const {
    if(!j.is_object()) throw std::invalid_argument("mobility must be an object");
    MobilityPolicy p;p.mode=j.value("mode",std::string("auto"));p.vigor=j.value("vigor",true);
    if(p.mode!="auto" && p.mode!="walk" && p.mode!="teleport" && p.mode!="charge")
        throw std::invalid_argument("mobility.mode must be auto, walk, teleport or charge");
    p.distance=integer(j,"distance",24,8,50);return p;
}
DebugAi::Impl::EventPolicy DebugAi::Impl::parseEvents(const Json &j) const {
    if(!j.is_object()) throw std::invalid_argument("events must be an object");
    for(const auto &[key,value]:j.items()) {
        (void)value;
        if(key!="waitSeconds" && key!="initialWaitMs" && key!="doorInitialWaitMs" && key!="scrollInitialWaitMs" && key!="questLogDurationMs" && key!="waypointCloseDelayMs" && key!="encounterRadius" && key!="standoffMin" && key!="standoffMax" &&
            key!="walkDecisionMs" && key!="walkReactionMs" && key!="walkRequestIntervalMs" && key!="walkRetryMs" &&
            key!="townTurnBaseMs" && key!="townTurnMinMs" && key!="townTurnMaxMs")
            throw std::invalid_argument("Unknown event policy: "+key);
    }
    EventPolicy p;p.waitSeconds=integer(j,"waitSeconds",90,15,300);
    p.initialWaitMs=integer(j,"initialWaitMs",1000,0,30000);
    p.doorInitialWaitMs=integer(j,"doorInitialWaitMs",250,0,30000);
    p.scrollInitialWaitMs=integer(j,"scrollInitialWaitMs",1000,0,30000);
    p.questLogDurationMs=integer(j,"questLogDurationMs",2000,0,30000);
    p.waypointCloseDelayMs=integer(j,"waypointCloseDelayMs",300,0,30000);
    p.walkDecisionMs=integer(j,"walkDecisionMs",160,1,5000);
    p.walkReactionMs=integer(j,"walkReactionMs",40,1,5000);
    p.walkRequestIntervalMs=integer(j,"walkRequestIntervalMs",200,0,5000);
    p.walkRetryMs=integer(j,"walkRetryMs",1000,0,30000);
    p.townTurnBaseMs=integer(j,"townTurnBaseMs",200,0,30000);
    p.townTurnMinMs=integer(j,"townTurnMinMs",600,0,30000);
    p.townTurnMaxMs=integer(j,"townTurnMaxMs",2000,0,30000);
    if(p.townTurnMinMs>p.townTurnMaxMs)
        throw std::invalid_argument("events.townTurnMinMs must not exceed townTurnMaxMs");
    if(std::max({p.initialWaitMs,p.doorInitialWaitMs,p.scrollInitialWaitMs})>=p.waitSeconds*1000)
        throw std::invalid_argument("Initial passage wait must be shorter than its total wait budget");
    p.encounterRadius=integer(j,"encounterRadius",32,12,64);
    p.standoffMin=integer(j,"standoffMin",8,4,20);p.standoffMax=integer(j,"standoffMax",24,8,40);
    if(p.standoffMin>=p.standoffMax || p.standoffMax+3>p.encounterRadius)
        throw std::invalid_argument("Event standoff needs min < max and max + 3 <= encounterRadius");
    return p;
}
void DebugAi::Impl::note(std::string event,std::string detail) {
    if(event=="exploration-backtrack" || event=="exploration-backtrack-cycle") ++navigationEfficiency.backtracks;
    ++sequence;behaviorLog.event(cursor,event,detail,sequence);
    history.push_back({{"sequence",sequence},{"goal",cursor},{"event",std::move(event)},{"detail",std::move(detail)}});
    while(history.size()>128) history.pop_front();
}
DebugAi::Impl::Json DebugAi::Impl::snapshot() const {
    Json passages=Json::array();
    for(const auto &[kind,pending]:passageWaits) passages.push_back({
        {"phase",passageName(kind)},{"attempts",pending.attempts},
        {"retryRemainingMs",std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(pending.retryAt-Clock::now()).count())},
        {"remainingMs",std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(pending.deadline-Clock::now()).count())}});
    Json drops=Json::array();
    for(const auto &[code,drop]:rememberedDrops)
        drops.push_back({{"code",code},{"id",drop.id},{"area",drop.area},{"x",drop.position.x},{"y",drop.position.y}});
    const auto population=area?populationMemory.find(*area):populationMemory.end();
    Json plan=Json::array();
    for(size_t i=0;i<goals.size();++i) {
        const auto &g=goals[i];Json v{{"index",i},{"action",goalNames[size_t(g.kind)]},{"state",!g.skipReason.empty()?"skipped":i<cursor?"completed":i==cursor?state:"queued"},
            {"timeoutSeconds",g.timeout},{"item",g.item},{"quest",g.quest}};
        if(!g.skipReason.empty()) v["reason"]=g.skipReason;
        if(g.kind==Kind::Quest) {
            v["useServiceRewards"]=g.useServiceRewards;
            v["act"]=questDefinition(g.questId).act+1;
            v["questNumber"]=questDefinition(g.questId).displaySlot+1;
        }
        if(g.area) v["area"]=*g.area;
        if(g.requiredWaypoint) v["requireWaypoint"]=true;
        if(g.target) v["target"]={{"type",g.target->type},{"identity",g.target->identity},{"name",g.target->name},
            {"superunique",g.target->superUnique?Json(*g.target->superUnique):Json(nullptr)}};
        plan.push_back(std::move(v));
    }
    Json attempts=Json::array();
    for(const auto &[point,attempt]:frontierAttempts) attempts.push_back({
        {"point",{point.first,point.second}},{"position",{attempt.position.x,attempt.position.y}},
        {"geometryRevision",attempt.geometryRevision},{"outcome",attempt.outcome},{"reason",attempt.reason},
        {"relayGoal",attempt.relayGoal?Json{attempt.relayGoal->x,attempt.relayGoal->y}:Json(nullptr)},
        {"reached",attempt.reached},{"retryInMs",std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(attempt.retryAt-Clock::now()).count())}});
    Json localLinks=Json::array();
    for(const auto &[key,record]:padObservations) localLinks.push_back({{"area",std::get<0>(key)},{"identity",std::get<1>(key)},
        {"source",{std::get<2>(key),std::get<3>(key)}},{"destination",{record.destination.x,record.destination.y}},{"visits",record.visits}});
    return {{"state",state},{"active",active()},{"phase",phase},{"reason",reason},{"goalIndex",cursor},{"goals",std::move(plan)},
        {"configuration",{{"file",configurationFile},{"characterClass",configurationClass},{"reload","explicit"}}},
        {"behaviorLog",{{"file",behaviorLog.path},{"error",behaviorLog.error}}},
        {"navigationEfficiency",{{"activeMs",navigationEfficiency.activeMs},{"navigationWaitMs",navigationEfficiency.navigationWaitMs},
            {"walkModeMs",navigationEfficiency.modeMs[0]},{"townWalkModeMs",navigationEfficiency.modeMs[1]},
            {"teleportModeMs",navigationEfficiency.modeMs[2]},{"walkingDistance",navigationEfficiency.walkingDistance},
            {"relocations",navigationEfficiency.relocations},{"backtracks",navigationEfficiency.backtracks},
            {"newObservedTiles",navigationEfficiency.observedTiles}}},
        {"eventPolicy",{{"waitSeconds",eventPolicy.waitSeconds},{"initialWaitMs",eventPolicy.initialWaitMs},
            {"doorInitialWaitMs",eventPolicy.doorInitialWaitMs},{"scrollInitialWaitMs",eventPolicy.scrollInitialWaitMs},{"retryMs",1000},{"encounterRadius",eventPolicy.encounterRadius},
            {"questLogDurationMs",eventPolicy.questLogDurationMs},
            {"waypointCloseDelayMs",eventPolicy.waypointCloseDelayMs},
            {"walkDecisionMs",eventPolicy.walkDecisionMs},{"walkReactionMs",eventPolicy.walkReactionMs},
            {"walkRequestIntervalMs",eventPolicy.walkRequestIntervalMs},{"walkRetryMs",eventPolicy.walkRetryMs},
            {"townTurnBaseMs",eventPolicy.townTurnBaseMs},{"townTurnMinMs",eventPolicy.townTurnMinMs},{"townTurnMaxMs",eventPolicy.townTurnMaxMs},
            {"standoffMin",eventPolicy.standoffMin},{"standoffMax",eventPolicy.standoffMax}}},
        {"worldWait",questRun.worldWait?Json{{"area",questRun.worldWait->area},
            {"anchor",{questRun.worldWait->anchor.x,questRun.worldWait->anchor.y}},{"expected",questRun.worldWait->expected},
            {"progress",questRun.worldWait->progress},{"lastProgress",questRun.worldWait->lastProgress},
            {"residualRange",questRun.worldWait->residualRange},{"movementRadius",encounterMovementRadius()},
            {"trackedEnemies",questRun.worldWait->enemies.size()},{"clock","execution"},
            {"waitRemainingMs",std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(questRun.worldWait->deadline-Clock::now()).count())}}:Json(nullptr)},
        {"gameGeneration",game},{"area",area?Json(*area):Json(nullptr)},{"decisionIntervalMs",cadence.intervalMs()},{"decisions",decisions},
        {"scheduler",{{"mode","adaptive"},{"wakeReason",cadence.reason()},{"eventWakeups",cadence.eventWakeups()},
            {"timerWakeups",cadence.timerWakeups()},{"searchContinuations",cadence.continuations()}}},
        {"combatTarget",combatTarget?Json{{"type",combatTarget->type},{"id",combatTarget->id}}:Json(nullptr)},
        {"target",target?Json{{"type",target->type},{"id",target->id},{"assignment",targetAssignment}}:Json(nullptr)},
        {"walkIntent",walkIntent},{"travel",{{"phase",unsigned(travelPhase)},
            {"destination",travelDestination?Json(*travelDestination):Json(nullptr)},{"route",travelRoute},{"visitedAreas",travelVisitedAreas},
            {"leg",legDestination?Json(*legDestination):Json(nullptr)},
            {"unit",legUnit?Json{{"type",legUnit->type},{"id",legUnit->id}}:Json(nullptr)}}},
        {"waypointDeferral",deferredWaypoint?Json{{"unit",{{"type",deferredWaypoint->unit.type},{"id",deferredWaypoint->unit.id}}},
            {"assignment",deferredWaypoint->assignment},{"from",{deferredWaypoint->position.x,deferredWaypoint->position.y}},
            {"retryRemainingMs",std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(deferredWaypoint->retryAt-Clock::now()).count())},
            {"requiresNewApproach",true}}:Json(nullptr)},
        {"combatPolicy",{{"enabled",policy.enabled},{"skill",policy.skill?Json(*policy.skill):Json("Attack")},{"range",policy.range},
            {"departureSkill",excursionSkill?Json(*excursionSkill):Json(nullptr)},
            {"selectionMode","departure"},
            {"fallbackSkills",policy.fallbackSkills},{"minimumSkillLevels",policy.minimumSkillLevels},{"noProgressSeconds",policy.noProgressSeconds},
            {"stallTimeoutSeconds",policy.stallTimeoutSeconds},{"retreatLifePercent",policy.retreatLifePercent},{"lifeDropPercent",policy.lifeDropPercent},
            {"scope",policy.scope},{"staticField",policy.staticField},{"avoidHazards",policy.avoidHazards},
            {"rangedKite",policy.rangedKite},{"kiteIntervalMs",policy.kiteIntervalMs},{"kiteDistance",policy.kiteDistance},
            {"kiteMinDistance",policy.kiteMinDistance},{"hazardLookaheadMs",policy.hazardLookaheadMs},{"hazardPadding",policy.hazardPadding},
            {"aura",policy.aura?Json(*policy.aura):Json("auto")},
            {"engageRadius",policy.engage},{"crowdRadius",policy.radius},{"crowdCount",policy.crowd},{"retreatDistance",policy.retreat}}},
        {"pending",{{"sourceInteraction",objectIssued},{"pickup",bool(pickup)},{"portal",portalIssued},{"portalUsed",portalUsed}}},
        {"waypointMemory",{{"game",waypointMemoryGame},{"historyRevision",waypointMemoryRevision},{"areas",knownWaypointAreas},{"requireBeforeDeparture",!requiredWaypointAreas.empty()}}},
        {"populationMemory",{{"game",populationMemoryGame},{"areas",populationMemory.size()},
            {"currentRooms",population==populationMemory.end()?0:population->second.rooms.size()},
            {"searchedRooms",population==populationMemory.end()?0:population->second.searchedRooms.size()},
            {"settlingRooms",population==populationMemory.end()?0:population->second.settlingRooms.size()},
            {"currentHostileHints",population==populationMemory.end()?0:population->second.enemies.size()}}},
        {"mobility",{{"mode",mobilityPolicy.mode},{"vigor",mobilityPolicy.vigor},{"distance",mobilityPolicy.distance},
            {"waitingForPosition",mobilityTarget.has_value()}}},
        {"supplies",{{"enabled",supplyPolicy.enabled},{"buy",supplyPolicy.buy},{"returnWhenEmpty",supplyPolicy.returnWhenEmpty},
            {"lifePercent",supplyPolicy.lifePercent},{"manaPercent",supplyPolicy.manaPercent},{"criticalLifePercent",supplyPolicy.criticalLifePercent},
            {"healthCount",supplyPolicy.healthCount},{"manaCount",supplyPolicy.manaCount},{"fillPortalBook",supplyPolicy.fillPortalBook},{"intervalMs",supplyPolicy.intervalMs},
            {"stage",std::array{"none","to-town","shop","return"}[size_t(supplyStage)]},
            {"returnArea",supplyOrigin?Json(*supplyOrigin):Json(nullptr)},
            {"pending",potionRequest?Json{{"item",potionRequest->item},{"code",potionRequest->code},{"buying",potionRequest->buying},
                {"sequence",potionRequest->sequence},{"expectedGain",potionRequest->expectedGain}}:Json(nullptr)},
            {"drinksConfirmed",drinks},{"purchasesConfirmed",purchases},{"vitals",supplyVitals},{"lastPotion",lastPotion}}},
        {"questProgress",cursor<goals.size() && goals[cursor].kind==Kind::Quest?Json{
            {"quest",goals[cursor].quest},{"step",questRun.step},{"steps",goals[cursor].steps.size()},
            {"encounterAnchor",questRun.encounterPosition?Json{questRun.encounterPosition->x,questRun.encounterPosition->y}:Json(nullptr)},
            {"encounterAnchored",questRun.encounterAnchored},
            {"mode",goals[cursor].requirements.empty()?"steps":"conditions"},
            {"condition",questRun.step<goals[cursor].requirements.size()?Json(goals[cursor].requirements[questRun.step].name):Json(nullptr)},
            {"action",questRun.step<goals[cursor].steps.size()?Json(std::array{"talk","clear","hunt","acquire","read-scroll","stones","operate","portal","return-town","imbue","act-travel","travel","use-item","combine","submit-staff","search-drop","equip-weapon","smash-orb","smash-forge","service","rescue","encounter"}[size_t(goals[cursor].steps[questRun.step].action)]):Json(nullptr)},
            {"pending",questRun.pending?Json{{"action",unsigned(questRun.pending->action)},{"item",questRun.pending->item},
                {"sequence",questRun.pending->sequence},{"unit",questRun.pending->unit?Json(questRun.pending->unit->id):Json(nullptr)}}:Json(nullptr)},
            {"clearStatusQueries",questRun.sweeps},{"materialPhase",questRun.materialPhase}}:Json(nullptr)},
        {"exploration",{{"visitedRooms",visited.size()},{"frontierAttempts",std::move(attempts)},
            {"preferredWallClearance",DebugNavigationSearch::explorationClearance},{"deferredViewpoints",viewpointAttempts.size()},
            {"stagedViewpoint",viewpointRoute?Json{{"target",{viewpointRoute->target.x,viewpointRoute->target.y}},
                {"legsWithoutDiscovery",viewpointRoute->origins.size()}}:Json(nullptr)},
            {"knownExploredRooms",knownExploredRooms},
            {"mapCacheBuilds",mapCacheBuilds},{"pathSearches",pathSearches},{"unavailableRooms",unavailableRooms.size()},
            {"room",exploring?Json::array({std::get<0>(*exploring),std::get<1>(*exploring),std::get<2>(*exploring)}):Json(nullptr)},
            {"target",interest?Json{{"x",interest->x},{"y",interest->y}}:Json(nullptr)}}},
        {"pickupApproachesTried",pickupApproaches.size()},
        {"goalExecutionRemainingMs",std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(goalDeadline-Clock::now()).count())},
        {"combatFeedback",combatDiagnostics},{"hazardFeedback",hazardDiagnostics},{"passageWaits",std::move(passages)},
        {"localTravel",{{"links",std::move(localLinks)},{"transfersWithoutDiscovery",padTransfersWithoutDiscovery},
            {"pending",localPad?Json{{"unit",localPad->unit.id},{"area",localPad->area},{"waiting",localPad->waiting},
                {"source",{localPad->source.x,localPad->source.y}},{"discontinuityBefore",localPad->discontinuity},
                {"requestRevision",localPad->requestRevision}}:Json(nullptr)}}},
        {"questPlan",questPlanDiagnostics},{"rememberedDrops",drops},
        {"actions",actionDiagnostics},{"navigationDiagnostics",{{"search",searchDiagnostics},{"approach",approachDiagnostics},{"mobility",mobilityDiagnostics},{"travel",travelDiagnostics},{"terrainClue",terrainClueDiagnostics}}},
        {"castRequests",castRequests},{"navigationGoals",moveGoals},{"interactions",interactions},{"history",history}};
}
void DebugAi::Impl::stop(DebugAutoWalk &walk,RemoteControl &control,RemoteCombat &combat) {
    walk.manualInput(control);control.cancelMovement();
    OnlineCombatCommand command;command.action=OnlineCombatCommand::Action::Stop;
    try {if(!combat.submit(command)) note("stop-unavailable",combat.reason());}
    catch(const std::exception &e) {note("stop-unavailable",e.what());}
    combatOwned=false;combatRun.repeating=false;mobilityHold=false;mobilityTarget.reset();
}
void DebugAi::Impl::cancelWalk(Context &c) {
    submissionWaits.erase(DebugActionOwner::Navigation);
    c.walk.manualInput(c.control);c.control.cancelMovement();walkIntent=nullptr;
}
void DebugAi::Impl::finish(std::string value,std::string detail,Context &c) {
    // Failure stops movement, so it cannot rely on the normal move-to-close
    // path to dismiss the conversation opened by this AI's current action.
    if(value=="failed" && c.session.read().stage==OnlineStage::ProtocolReady && c.session.read().gameGeneration==game &&
        c.session.read().world.npcRequested && walkIntent.is_object() && walkIntent.value("unitType",-1)==1 &&
        walkIntent.value("unitId",uint32_t{})==*c.session.read().world.npcRequested) {
        if(c.control.closeNpc()) note("failed-dialogue-closed","Released the AI's NPC conversation before stopping");
        else note("failed-dialogue-close-rejected","Client did not accept closing the current conversation");
    }
    if(c.session.read().stage==OnlineStage::ProtocolReady && c.session.read().gameGeneration==game)
        stop(c.walk,c.control,c.combat);
    else {
        // The old game's stop must neither affect a new game nor replace a
        // transport/protocol failure with an unrelated input-stage error.
        cancelWalk(c);combatOwned=false;combatRun.repeating=false;mobilityHold=false;mobilityTarget.reset();
    }
    behaviorLog.endPhase();
    state=std::move(value);reason=std::move(detail);phase.clear();note(state,reason);behaviorLog.close();
}
void DebugAi::Impl::resetStep() {
    behaviorLog.endPhase();phaseReported=false;
    explorationReturn.reset();hubReturnCoverage.clear();defendedTravel.reset();
    submissionWaits.clear();
    localPad.reset();
    backtrackAttempts.clear();
    if(!viewpointAttempts.empty()) note("observation-viewpoints-cleared",Json{
        {"reason","step-reset"},{"count",viewpointAttempts.size()}}.dump());
    viewpointAttempts.clear();viewpointRoute.reset();explorationPrefetch.reset();
    searchDiagnostics=nullptr;approachDiagnostics=nullptr;mobilityDiagnostics=nullptr;travelDiagnostics=nullptr;
    terrainClueDiagnostics=nullptr;
    vaughtSearch.aim.reset();
    directedConnectionRetry.clear();
    combatRun={};combatDiagnostics=nullptr;hazardDodge.reset();hazardDiagnostics=nullptr;continuation=Continuation::None;
    phase.clear();reason.clear();target.reset();targetAssignment=0;visited.clear();frontierAttempts.clear();checkedHints.clear();unavailableRooms.clear();exploring.reset();interest.reset();searchPosition.reset();searchRetry={};
    walkIntent=nullptr;approachHint.reset();approachFrontier.reset();approachDetour.reset();operatedDoors.clear();pendingDoor.reset();doorDeadline={};
    pendingWaypoint.reset();closeWaypoint.reset();waypointDeadline={};
    travelDestination.reset();travelRoute.clear();travelVisitedAreas.clear();travelPhase=TravelPhase::Route;legDestination.reset();legUnit.reset();waypointDestination.reset();
    objectIssued=portalIssued=portalAttempted=portalUsed=greetingIssued=false;
    pickup.reset();pickupApproaches.clear();oldPortals.clear();actionDeadline={};actionWaitSubmitted=false;exitRetryAt={};npcOpenRetries=0;
    npcOpeningRecovery=false;navigationRetries.clear();
    submittedTravel.reset();travelRetries.clear();passageWaits.clear();
    mobilityTrials.clear();mobilityGoal.reset();mobilityRoute.clear();mobilityRouteProgress=mobilityPendingProgress=mobilityRouteLength=0;
}
void DebugAi::Impl::advance(Context &c,std::string evidence,bool skipped) {
    note("goal-navigation-efficiency",Json{{"skipped",skipped},
        {"activeMs",navigationEfficiency.activeMs-goalEfficiencyStart.activeMs},
        {"navigationWaitMs",navigationEfficiency.navigationWaitMs-goalEfficiencyStart.navigationWaitMs},
        {"walkModeMs",navigationEfficiency.modeMs[0]-goalEfficiencyStart.modeMs[0]},
        {"townWalkModeMs",navigationEfficiency.modeMs[1]-goalEfficiencyStart.modeMs[1]},
        {"teleportModeMs",navigationEfficiency.modeMs[2]-goalEfficiencyStart.modeMs[2]},
        {"walkingDistance",navigationEfficiency.walkingDistance-goalEfficiencyStart.walkingDistance},
        {"relocations",navigationEfficiency.relocations-goalEfficiencyStart.relocations},
        {"backtracks",navigationEfficiency.backtracks-goalEfficiencyStart.backtracks},
        {"newObservedTiles",navigationEfficiency.observedTiles-goalEfficiencyStart.observedTiles}}.dump());
    releaseCombat(c);
    questPlanDiagnostics=nullptr;
    if(c.session.read().world.npcRequested) c.control.closeNpc();
    if(c.session.read().world.waypointSource) c.control.useWaypoint(0);
    if(skipped) goals.at(cursor).skipReason=evidence;
    if(!skipped && goals.at(cursor).kind==Kind::Quest && eventPolicy.questLogDurationMs>0) {
        questLogPending=true;questLogGame=c.session.read().gameGeneration;
        questLogConnection=c.session.read().connectionGeneration;
    }
    cancelWalk(c);behaviorLog.endPhase();note(skipped?"goal-skipped":"goal-completed",std::move(evidence));++cursor;resetStep();questRun={};padTransfersWithoutDiscovery=0;
    if(cursor==goals.size()) {
        const auto count=std::count_if(goals.begin(),goals.end(),[](const auto &g){return !g.skipReason.empty();});
        finish("completed",count?"Plan resolved; "+std::to_string(count)+" unavailable quests skipped":"All goal postconditions observed",c);return;
    }
    goalDeadline=Clock::now()+std::chrono::seconds(goals[cursor].timeout);goalStarted();
}
void DebugAi::Impl::goalStarted() {
    goalEfficiencyStart=navigationEfficiency;
    const auto &g=goals.at(cursor);
    std::string label=goalNames[size_t(g.kind)];
    if(!g.quest.empty()) label+=" "+g.quest;
    if(g.area) label+=" area="+std::to_string(*g.area);
    if(g.requiredWaypoint) label+=" waypoint";
    if(g.target) label+=" "+g.target->name;
    if(!g.item.empty()) label+=" "+g.item;
    note("goal-started",std::move(label));
}
DebugAi::DebugAi(Archives &a,std::filesystem::path logDirectory):impl_(std::make_unique<Impl>(a)) {
    impl_->logDirectory=std::move(logDirectory);
}
DebugAi::~DebugAi() {
    if(impl_->live()) {
        impl_->behaviorLog.endPhase();
        impl_->behaviorLog.event(impl_->cursor,"session-ended","Client closed with an unfinished plan");
    }
    impl_->behaviorLog.close();
}
bool DebugAi::active() const {return impl_->active();}
bool DebugAi::hasPlan() const {return impl_->live();}
bool DebugAi::controlsMovement() const {return impl_->active() && (impl_->mobilityHold || impl_->actionRecoveryBusy);}
bool DebugAi::controlsInventory() const {
    const auto &s=*impl_;
    if(s.active() && (s.supplyStage==Impl::SupplyStage::Shop ||
        (s.cursor<s.goals.size() && s.goals[s.cursor].kind==Impl::Kind::Restock))) return true;
    if(!s.active() || s.cursor>=s.goals.size() || s.goals[s.cursor].kind!=Impl::Kind::Quest ||
        s.questRun.step>=s.goals[s.cursor].steps.size()) return false;
    const auto action=s.goals[s.cursor].steps[s.questRun.step].action;
    return s.questRun.initialInventory || s.questRun.recoveringItem || action==Impl::QuestAction::Combine || action==Impl::QuestAction::SubmitStaff || action==Impl::QuestAction::Imbue || action==Impl::QuestAction::EquipWeapon;
}
void DebugAi::Impl::cancel(DebugAutoWalk &walk,RemoteControl &control,RemoteCombat &combat,std::string why) {
    if(!live()) return;
    stop(walk,control,combat);behaviorLog.endPhase();state="cancelled";
    reason=std::move(why);note(state,reason);behaviorLog.close();
}
void DebugAi::manualInput(net::RealmSession &,DebugAutoWalk &walk,RemoteControl &control,RemoteCombat &combat) {
    impl_->cancel(walk,control,combat,"Escape released AI control");
}
std::optional<std::string> DebugAi::command(const nlohmann::json &raw,net::RealmSession &session,RemoteTown &town,
        DebugAutoWalk &walk,RemoteControl &control,RemoteCombat &combat,RemoteInventory &) {
    const auto name=raw.at("command").get<std::string>();if(!name.starts_with("debug-ai-")) return {};
    auto &s=*impl_;const auto now=Impl::Clock::now();
    auto request=raw;
    if(name=="debug-ai-reload-config" && !request.contains("configFile") && !s.configurationFile.empty()) request["configFile"]=s.configurationFile;
    std::optional<uint16_t> characterClass;
    if(session.read().load.playerUnitId) {
        const auto player=session.read().world.units.find({0,*session.read().load.playerUnitId});
        if(player!=session.read().world.units.end()) characterClass=player->second.classId;
    }
    const auto j=name=="debug-ai-start" || name=="debug-ai-append" || name=="debug-ai-reload-config"?configuredAiRequest(request,characterClass):request;
    if(name=="debug-ai-status") return Impl::Json{{"ok",true},{"ai",s.snapshot()}}.dump();
    if(name=="debug-ai-cancel") {
        s.cancel(walk,control,combat,"Cancelled by debug command");
    }
    else if(name=="debug-ai-pause") {
        if(s.active()) {
            const auto submittedMovement=s.mobilityTarget;
            const bool incomplete=walk.active();s.stop(walk,control,combat);
            // Stop future input, retaining the real-time observation of an
            // already accepted movement skill. Pause cannot undo that cast.
            s.mobilityTarget=submittedMovement;
            if(incomplete) s.walkIntent=nullptr;
            s.behaviorLog.endPhase();s.state="paused";s.pausedAt=now;s.note("paused");
        }
    } else if(name=="debug-ai-resume") {
        if(s.state!="paused") throw std::invalid_argument("AI is not paused");
        if(s.game!=session.read().gameGeneration) throw std::invalid_argument("AI plan belongs to a previous game");
        if(s.areaGeneration!=session.read().world.areaGeneration || s.area!=town.read().area)
            throw std::invalid_argument("Paused AI area changed; cancel and start a new high-level plan");
        // Tick accounts only suspended execution budgets. Native requests
        // and their real-time deadlines keep running during an explicit pause.
        s.cadence.wake();s.state="running";s.note("resumed",std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now-s.pausedAt).count())+"ms paused");
    } else if(name=="debug-ai-reload-config") {
        if(!s.live() || s.game!=session.read().gameGeneration || !s.definitions) throw std::invalid_argument("No live AI plan in this game to configure");
        for(const auto &[key,value]:j.items()) {
            (void)value;
            if(key!="command" && key!="configFile" && key!="combat" && key!="mobility" && key!="supplies" && key!="events")
                throw std::invalid_argument("Live AI configuration only changes combat, mobility, supply and event policy numbers");
        }
        const auto &p=s.policy;
        auto nextCombat=s.parseCombat(policyNumbers({{"range",p.range},{"crowdRadius",p.radius},{"crowdCount",p.crowd},
            {"retreatDistance",p.retreat},{"engageRadius",p.engage},{"noProgressSeconds",p.noProgressSeconds},
            {"stallTimeoutSeconds",p.stallTimeoutSeconds},{"retreatLifePercent",p.retreatLifePercent},{"lifeDropPercent",p.lifeDropPercent},
            {"rangedKite",p.rangedKite},{"kiteIntervalMs",p.kiteIntervalMs},{"kiteDistance",p.kiteDistance},
            {"kiteMinDistance",p.kiteMinDistance},{"hazardLookaheadMs",p.hazardLookaheadMs},{"hazardPadding",p.hazardPadding}},
            j.value("combat",Impl::Json::object())));
        nextCombat.enabled=p.enabled;nextCombat.scope=p.scope;nextCombat.skill=p.skill;nextCombat.fallbackSkills=p.fallbackSkills;
        nextCombat.minimumSkillLevels=p.minimumSkillLevels;
        nextCombat.aura=p.aura;nextCombat.staticField=p.staticField;nextCombat.avoidHazards=p.avoidHazards;
        auto nextMobility=s.parseMobility(policyNumbers({{"distance",s.mobilityPolicy.distance}},j.value("mobility",Impl::Json::object())));
        nextMobility.mode=s.mobilityPolicy.mode;nextMobility.vigor=s.mobilityPolicy.vigor;
        const auto &stock=s.supplyPolicy;
        auto nextSupply=s.parseSupplies(policyNumbers({{"lifePercent",stock.lifePercent},{"manaPercent",stock.manaPercent},
            {"criticalLifePercent",stock.criticalLifePercent},{"healthCount",stock.healthCount},{"manaCount",stock.manaCount},
            {"intervalMs",stock.intervalMs}},j.value("supplies",Impl::Json::object())));
        nextSupply.enabled=stock.enabled;nextSupply.buy=stock.buy;nextSupply.returnWhenEmpty=stock.returnWhenEmpty;nextSupply.fillPortalBook=stock.fillPortalBook;
        const auto nextEvents=s.parseEvents(policyNumbers({{"waitSeconds",s.eventPolicy.waitSeconds},
            {"initialWaitMs",s.eventPolicy.initialWaitMs},{"doorInitialWaitMs",s.eventPolicy.doorInitialWaitMs},{"scrollInitialWaitMs",s.eventPolicy.scrollInitialWaitMs},
            {"questLogDurationMs",s.eventPolicy.questLogDurationMs},
            {"waypointCloseDelayMs",s.eventPolicy.waypointCloseDelayMs},
            {"walkDecisionMs",s.eventPolicy.walkDecisionMs},{"walkReactionMs",s.eventPolicy.walkReactionMs},
            {"walkRequestIntervalMs",s.eventPolicy.walkRequestIntervalMs},{"walkRetryMs",s.eventPolicy.walkRetryMs},
            {"townTurnBaseMs",s.eventPolicy.townTurnBaseMs},{"townTurnMinMs",s.eventPolicy.townTurnMinMs},{"townTurnMaxMs",s.eventPolicy.townTurnMaxMs},
            {"encounterRadius",s.eventPolicy.encounterRadius},{"standoffMin",s.eventPolicy.standoffMin},{"standoffMax",s.eventPolicy.standoffMax}},
            j.value("events",Impl::Json::object())));
        s.policy=std::move(nextCombat);s.mobilityPolicy=std::move(nextMobility);s.supplyPolicy=std::move(nextSupply);
        s.eventPolicy=nextEvents;
        walk.setTiming(nextEvents);s.cadence.wake();
        s.configurationFile=j.value("configFile",std::string{});
        s.note("config-reloaded",j.value("configFile",std::string("inline policy")));
    } else if(name=="debug-ai-start" || name=="debug-ai-append") {
        const auto &view=session.read();
        if(view.protocolProfile!=net::protocol::clientCompatibility(net::protocol::ClientProfile::Lod113c).id)
            throw std::invalid_argument("Debug task recipes currently support the retail 1.13c profile only");
        if(view.stage!=OnlineStage::ProtocolReady || onlinePlayerDead(view.world) || !town.read().nativeMapReady)
            throw std::invalid_argument("AI requires a live player in a ready native map");
        if(view.world.playerTrade.active()) throw std::invalid_argument("Finish the player trade before starting AI");
        if(!s.definitions) s.definitions=std::make_unique<Impl::Definitions>(s.archives);
        auto plan=s.parse(j);auto policy=s.parseCombat(j.value("combat",Impl::Json::object()));
        std::set<uint16_t> requiredWaypointAreas;
        if(j.value("activateAllWaypoints",false)) {
            if(name!="debug-ai-start") throw std::invalid_argument("activateAllWaypoints belongs to the initial plan");
            std::map<int,std::pair<int,uint16_t>> waypoints;
            for(size_t row=0;row<s.definitions->levels.rows().size();++row) {
                const auto id=s.definitions->levels.number(row,"Id"),number=s.definitions->levels.number(row,"Waypoint");
                const auto act=s.definitions->levels.number(row,"Act");
                if(id && *id>0 && *id<=136 && number && *number>=0 && *number<112 && act && *act>=0 && *act<5)
                    waypoints.try_emplace(*number,*act,uint16_t(*id));
            }
            // The departure guard covers visited levels. Schedule side areas
            // in their own act while its prerequisites are available.
            constexpr std::array finales{QuestId::SistersToTheSlaughter,QuestId::SevenTombs,QuestId::Guardian,
                QuestId::TerrorsEnd,QuestId::EveOfDestruction};
            std::array<std::vector<Impl::Goal>,5> byAct;
            for(const auto &[number,entry]:waypoints) {
                const auto [act,level]=entry;
                requiredWaypointAreas.insert(level);
                Impl::Goal goal;goal.kind=Impl::Kind::Travel;goal.area=level;goal.requiredWaypoint=true;
                goal.timeout=integer(j,"goalTimeoutSeconds",600,10,3600);
                byAct[act].push_back(std::move(goal));
            }
            for(size_t act=0;act<byAct.size();++act) {
                const auto before=std::find_if(plan.begin(),plan.end(),[&](const auto &g) {
                    return g.kind==Impl::Kind::Quest && (g.questId==finales[act] || questDefinition(g.questId).act>int(act));
                });
                plan.insert(before,byAct[act].begin(),byAct[act].end());
            }
        }
        if(onlineHasCursorItem(view.world.items,view.load.playerUnitId) &&
            std::any_of(plan.begin(),plan.end(),[](const auto &g){return g.kind!=Impl::Kind::Quest;}))
            throw std::invalid_argument("Cursor recovery requires a quest-only plan; store it before other goals");
        for(auto &goal:plan) if(goal.kind==Impl::Kind::Quest && goal.questId==QuestId::EveOfDestruction && view.world.quests.playerFlags)
            goal.initiallyRewarded=((*view.world.quests.playerFlags)[questDefinition(goal.questId).nativeSlot]&1)!=0;
        const auto mobility=s.parseMobility(j.value("mobility",Impl::Json::object()));
        const auto supplies=s.parseSupplies(j.value("supplies",Impl::Json::object()));
        const auto events=s.parseEvents(j.value("events",Impl::Json::object()));
        const auto &supplyPolicy=name=="debug-ai-append"?s.supplyPolicy:supplies;
        if((!supplyPolicy.enabled || !supplyPolicy.buy) && std::any_of(plan.begin(),plan.end(),[](const auto &g){return g.kind==Impl::Kind::Restock;}))
            throw std::invalid_argument("restock requires supplies.enabled and supplies.buy");
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
        if(!fighting && std::any_of(plan.begin(),plan.end(),[](const auto &g){return g.kind==Impl::Kind::Hunt || g.kind==Impl::Kind::Quest;}))
            throw std::invalid_argument("Hunt/quest goals require combat to be enabled");
        if(name=="debug-ai-start" && policy.enabled && policy.skill) {
            combat.update();
            const bool available=std::any_of(combat.skills().begin(),combat.skills().end(),[&](const auto &v) {
                const bool configured=v.id==policy.skill || std::find(policy.fallbackSkills.begin(),policy.fallbackSkills.end(),v.id)!=policy.fallbackSkills.end();
                const auto minimum=policy.minimumSkillLevels.find(v.id);
                return configured && !v.passive && std::max(unsigned(v.level),v.innate?1u:0u)>=
                    (minimum==policy.minimumSkillLevels.end()?1u:minimum->second);
            });
            if(!available) throw std::invalid_argument("No configured combat attack meets its minimum effective skill level; learn a candidate or configure a usable fallback");
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
            if(aura && std::none_of(combat.skills().begin(),combat.skills().end(),[&](const auto &v) {
                const bool configured=v.id==policy.skill || std::find(policy.fallbackSkills.begin(),policy.fallbackSkills.end(),v.id)!=policy.fallbackSkills.end();
                const auto minimum=policy.minimumSkillLevels.find(v.id);
                return configured && v.left && !v.passive && std::max(unsigned(v.level),v.innate?1u:0u)>=
                    (minimum==policy.minimumSkillLevels.end()?1u:minimum->second);
            })) throw std::invalid_argument("A battle aura requires at least one configured left-hand attack meeting its minimum skill level");
        }
        const bool malus=std::any_of(plan.begin(),plan.end(),[&](const auto &g){
            if(g.kind==Impl::Kind::Acquire && g.item=="hdm") return true;
            if(g.kind!=Impl::Kind::Quest || g.questId!=QuestId::ToolsOfTheTrade) return false;
            return !view.world.quests.playerFlags || !((*view.world.quests.playerFlags)[questDefinition(QuestId::ToolsOfTheTrade).nativeSlot]&3);
        });
        if(malus) {
            const auto level=view.world.playerAttributes.find(12);
            if(level!=view.world.playerAttributes.end() && level->second<8)
                throw std::invalid_argument("Original Malus pickup requires character level 8");
        }
        if(name=="debug-ai-append") {
            if(j.contains("combat") || j.contains("mobility") || j.contains("supplies") || j.contains("events")) throw std::invalid_argument("Append inherits the current plan's combat, mobility, supplies and event policies");
            if(!s.live() || s.game!=view.gameGeneration) throw std::invalid_argument("No live AI plan to append to");
            if(s.goals.size()+plan.size()>128) throw std::invalid_argument("AI queue exceeds 128 goals including required waypoints");
            s.goals.insert(s.goals.end(),plan.begin(),plan.end());s.note("goals-appended",std::to_string(plan.size()));
        } else {
            if(s.live()) throw std::invalid_argument("Cancel the current AI plan before replacing it");
            if(view.world.townPortalPending || (view.world.itemRequest && view.world.itemRequest->state==OnlineItemRequest::State::Pending))
                throw std::invalid_argument("Wait for the current item/portal result before starting another plan");
            if(view.world.storage.kind!=OnlineStorageKind::None || view.world.storage.requested!=OnlineStorageKind::None)
                throw std::invalid_argument("Close storage before starting an AI plan");
            s.stop(walk,control,combat);
            if(view.world.npcRequested) control.closeNpc();
            if(view.world.waypointSource) control.useWaypoint(0);
            s.requiredWaypointAreas=std::move(requiredWaypointAreas);s.waypointDepartureArea.reset();s.nearbyWaypointArea.reset();s.missingWaypointAt.reset();
            s.policy=policy;s.mobilityPolicy=mobility;s.supplyPolicy=supplies;s.goals=std::move(plan);s.cursor=0;s.resetStep();s.history.clear();s.sequence=s.decisions=0;
            s.excursionSkill.reset();s.excursionSkillPrepared=false;
            s.configurationClass=characterClass && *characterClass<classNames.size()?classNames[*characterClass]:"unknown";
            s.eventPolicy=events;
            walk.setTiming(events);
            s.rememberedDrops.clear();
            s.summitTrialKills.reset();
            s.deferredWaypoint.reset();
            s.handledWaypointTimeout.reset();
            s.padTransfersWithoutDiscovery=0;
            if(s.game!=view.gameGeneration) {
                s.journalReadGame.reset();s.padObservations.clear();s.padArrival.reset();s.questWeaponRestore.reset();
                s.padProgressCells=s.padProgressTargets=s.padTransfersWithoutDiscovery=0;
            }
            s.castRequests=s.moveGoals=s.interactions=0;s.game=view.gameGeneration;s.areaGeneration=view.world.areaGeneration;s.area=town.read().area;
            s.combatTarget.reset();s.retreatPoint.reset();s.combatOwned=false;
            s.combatLifeSamples.clear();s.combatMaxLife.reset();
            s.lastAttack={};s.retreatUntil={};s.retreatReady={};
            s.mobilityTarget.reset();s.mobilityHold=false;s.nextMobility={};s.lastSelection={};s.nextArmorAttempt={};
            s.townExitArea.reset();s.townExitObserveUntil={};
            s.supplyStage=Impl::SupplyStage::None;s.supplyOrigin.reset();s.supplyPortalUsed=false;
            s.questRun={};s.questPlanDiagnostics=nullptr;s.tombSymbol.reset();s.bypassedThreats.clear();
            s.questRun.initialInventory=s.goals.front().kind==Impl::Kind::Quest &&
                (onlineHasCursorItem(view.world.items,view.load.playerUnitId) || view.world.storage.kind!=OnlineStorageKind::None ||
                 view.world.storage.requested!=OnlineStorageKind::None);
            s.potionRequest.reset();s.nextPotion={};s.shelfReopens=0;s.shopIssued=false;s.shelfObserved=false;s.healingIssued=false;s.healingObserved=false;s.supplyNpc.reset();s.shopDeadline={};s.drinks=s.purchases=0;s.lastPotion=nullptr;s.supplyVitals=nullptr;
            s.executionClock.reset(now);s.inputBlocked=false;s.actionDiagnostics=nullptr;
            s.navigationEfficiency={};s.goalEfficiencyStart={};s.efficiencySampleAt=now;
            s.efficiencyExecuting=false;s.efficiencyWaiting=false;s.efficiencyPosition.reset();s.efficiencyArea.reset();
            s.efficiencyObserved=town.navigationMemory().observedCells;
            s.cadence.reset();s.decisionLife.reset();
            s.behaviorLog.open(s.logDirectory,s.game);
            s.observedWalkFailures=walk.failureCount();
            s.behaviorLog.observe(s.area,{},view.world.playerPosition);
            s.goalDeadline=now+std::chrono::seconds(s.goals.front().timeout);s.state="running";s.note("plan-started");s.goalStarted();
            s.note("combat-profile",s.configurationClass);
            s.configurationFile=j.value("configFile",std::string{});
            if(!s.configurationFile.empty()) s.note("config-loaded",s.configurationFile);
        }
    } else throw std::invalid_argument("Unknown debug AI command");
    return Impl::Json{{"ok",true},{"ai",s.snapshot()}}.dump();
}
void DebugAi::Impl::tickQuestLog(const OnlineView &view,const SuppliesView &supplies,bool blocked) {
    const auto now=Clock::now();
    const bool invalid=view.stage!=OnlineStage::ProtocolReady || view.gameGeneration!=questLogGame ||
        view.connectionGeneration!=questLogConnection || (!live() && state!="completed");
    const bool disabled=eventPolicy.questLogDurationMs==0;
    if(invalid || disabled) questLogPending=false;
    if(questLogCloseAt!=Clock::time_point{} && (invalid || disabled || now>=questLogCloseAt)) {
        if(supplies.closeQuestLog) supplies.closeQuestLog();
        questLogCloseAt={};
    }
    if(!invalid && questLogPending && !blocked && (active() || state=="completed") &&
        supplies.openQuestLog && supplies.openQuestLog()) {
        questLogPending=false;questLogCloseAt=now+std::chrono::milliseconds(eventPolicy.questLogDurationMs);
    }
}
void DebugAi::tick(net::RealmSession &session,RemoteTown &town,DebugAutoWalk &walk,RemoteControl &control,
        RemoteCombat &combat,RemoteInventory &inventory,const SuppliesView &supplies,bool blocked,std::optional<Vec> position,bool moving) {
    auto &s=*impl_;
    s.tickQuestLog(session.read(),supplies,blocked);
    if(!s.live()) return;
    const auto sampledAt=Impl::Clock::now();
    if(s.efficiencySampleAt!=Impl::Clock::time_point{} && s.efficiencyExecuting) {
        const auto elapsed=std::chrono::duration<double,std::milli>(sampledAt-s.efficiencySampleAt).count();
        s.navigationEfficiency.activeMs+=elapsed;s.navigationEfficiency.modeMs.at(s.efficiencyMode)+=elapsed;
        if(s.efficiencyWaiting) s.navigationEfficiency.navigationWaitMs+=elapsed;
    }
    s.efficiencySampleAt=sampledAt;
    s.efficiencyExecuting=s.active() && !blocked && position && town.read().nativeMapReady &&
        session.read().stage==OnlineStage::ProtocolReady;
    const auto observed=town.navigationMemory().observedCells;
    if(observed>s.efficiencyObserved) s.navigationEfficiency.observedTiles+=observed-s.efficiencyObserved;
    s.efficiencyObserved=observed;
    const auto self=session.read().world.units.find({0,session.read().load.playerUnitId.value_or(0)});
    const auto discontinuity=self==session.read().world.units.end()?0:self->second.positionDiscontinuity;
    if(position && s.efficiencyPosition && s.efficiencyArea==town.read().area) {
        if(discontinuity!=s.efficiencyDiscontinuity) ++s.navigationEfficiency.relocations;
        else if(moving) s.navigationEfficiency.walkingDistance+=(*position-*s.efficiencyPosition).length();
    }
    s.efficiencyPosition=position;s.efficiencyArea=town.read().area;s.efficiencyDiscontinuity=discontinuity;
    const auto timingKey=std::tuple{session.read().gameGeneration,
        supplies.navigationFasterCast.value_or(std::numeric_limits<int>::min()),supplies.navigationWeaponSet,
        self==session.read().world.units.end()?uint64_t{}:self->second.stateSequence,
        self==session.read().world.units.end()?uint64_t{}:self->second.appearanceRevision,
        self==session.read().world.units.end()?uint16_t{}:self->second.classId.value_or(0)};
    const bool timingChanged=s.navigationTimingKey!=timingKey;
    if(timingChanged) {
        s.navigationTimingKey=timingKey;s.navigationTeleportSeconds.reset();
    }
    if(s.definitions && supplies.observedCastSeconds)
        if(const auto sample=supplies.observedCastSeconds(s.definitions->skill("Teleport"));
            sample && std::isfinite(sample->second) && sample->second>0) {
            const auto identity=std::pair{session.read().gameGeneration,sample->first};
            if(s.navigationCastSample!=identity) {
                s.navigationCastSample=identity;
                if(!timingChanged) s.navigationTeleportSeconds=sample->second;
            }
        }
    std::optional<OnlinePoint> local;
    if(position && position->x>=0 && position->y>=0 && position->x<=65535 && position->y<=65535)
        local=OnlinePoint{uint16_t(std::lround(position->x)),uint16_t(std::lround(position->y))};
    s.behaviorLog.observe(town.read().area,local,session.read().world.playerPosition);
    if(s.observedWalkFailures!=walk.failureCount()) {
        s.observedWalkFailures=walk.failureCount();
        if(s.walkIntent.is_object()) {
            const auto child=walk.snapshot();
            auto detail=child.at("reason").get<std::string>()+" moves="+std::to_string(child.at("moveRequests").get<unsigned>())+
                " yields="+std::to_string(child.at("scheduler").at("searchContinuations").get<uint64_t>());
            const auto &at=child.at("target");
            if(at.is_object()) detail+=" target="+std::to_string(at.at("x").get<int>())+","+std::to_string(at.at("y").get<int>());
            s.behaviorLog.event(s.cursor,"walk-failed",detail);
        }
    }
    const auto decisions=s.decisions;
    s.phaseReported=false;
    const auto decisionStarted=Impl::Clock::now();
    tickPlan(session,town,walk,control,combat,inventory,supplies,blocked,position,moving);
    s.efficiencyWaiting=s.phase=="navigation-worker";
    s.efficiencyMode=town.read().town?1:s.mobilityTeleport && s.mobilityTarget?2:0;
    if(!town.read().town && s.searchDiagnostics.is_object() &&
        s.searchDiagnostics.value("area",uint16_t{})==town.read().area)
        s.efficiencyMode=s.searchDiagnostics.value("mobilityClass",0)==2?2:0;
    const auto decisionMs=std::chrono::duration<double,std::milli>(Impl::Clock::now()-decisionStarted).count();
    if(s.decisions!=decisions || decisionMs>=10) walk.navigationSearch().traceDecision({
        {"decision",s.decisions},{"goal",s.cursor},{"phase",s.phase},{"reason",s.reason},
        {"elapsedMs",decisionMs},{"blocked",s.inputBlocked},{"area",town.read().area?nlohmann::json(*town.read().area):nlohmann::json(nullptr)},
        {"actionRecoveryBusy",s.actionRecoveryBusy},{"moving",moving},{"walkActive",walk.active()},
        {"searchDecision",s.searchDiagnostics.is_object()?s.searchDiagnostics.value("decision",uint64_t{}):uint64_t{}},
        {"position",position?nlohmann::json{position->x,position->y}:nlohmann::json(nullptr)}});
    s.tickQuestLog(session.read(),supplies,blocked);
    if(!s.active()) return;
    if(s.inputBlocked) {
        const auto why=session.read().stage!=OnlineStage::ProtocolReady?"Protocol not ready":
            blocked?"Frontend input suspended":!position?"Local position unavailable":"Area or reconstructed map unavailable";
        s.behaviorLog.phase(s.cursor,"input-blocked",why);
    }
    else if(s.decisions!=decisions) {
        if(s.phaseReported) s.behaviorLog.phase(s.cursor,s.phase,s.reason);
        else s.behaviorLog.endPhase();
    }
}
void DebugAi::tickPlan(net::RealmSession &session,RemoteTown &town,DebugAutoWalk &walk,RemoteControl &control,
        RemoteCombat &combat,RemoteInventory &inventory,const SuppliesView &supplies,bool blocked,std::optional<Vec> position,bool moving) {
    auto &s=*impl_;
    const auto now=Impl::Clock::now();const auto &view=session.read();
    const bool recovering=supplies.actionInputBusy.value_or(supplies.castInputBusy.value_or(false));
    // Resume on the first frontend frame that observes animation recovery,
    // without paying another 40/80/160-ms decision interval. Potion safety
    // also wakes immediately on a real life drop during recovery.
    if((s.actionRecoveryBusy && !recovering) ||
        (recovering && s.decisionLife && view.world.life && *view.world.life<*s.decisionLife)) s.cadence.wake();
    s.actionRecoveryBusy=recovering;
    s.inputBlocked=blocked || !s.active() || !position || !town.read().nativeMapReady || !town.read().area || view.stage!=OnlineStage::ProtocolReady;
    const auto suspended=s.executionClock.sample(now,s.inputBlocked);
    for(auto &[owner,pending]:s.submissionWaits) {
        (void)owner;pending.deadline+=suspended;pending.retryAt+=suspended;
    }
    if(s.supplyStage==Impl::SupplyStage::None) s.goalDeadline+=suspended;
    else s.supplyDeadline+=suspended;
    const Vec observed=position.value_or(view.world.playerPosition?Vec{float(view.world.playerPosition->x),float(view.world.playerPosition->y)}:Vec{});
    Impl::Context c{session,town,walk,control,combat,inventory,observed,moving,&supplies};
    if(s.questRun.pending && s.questWaitRequiresInput(c)) s.questRun.pending->deadline+=suspended;
    if(s.questRun.portalDeadline!=Impl::Clock::time_point{}) s.questRun.portalDeadline+=suspended;
    if(s.questRun.pending && s.questRun.pending->action==Impl::QuestPending::Action::Portal) {
        s.questRun.pending->deadline+=suspended;
    }
    if(!s.actionWaitSubmitted && s.actionDeadline!=Impl::Clock::time_point{}) s.actionDeadline+=suspended;
    for(auto &[kind,pending]:s.passageWaits) {(void)kind;pending.retryAt+=suspended;pending.deadline+=suspended;}
    if(const auto found=s.passageWaits.find(Impl::Passage::Door);found!=s.passageWaits.end()) {
        if(s.pendingDoor) s.doorDeadline=found->second.deadline;
        if(s.questRun.pending && s.cursor<s.goals.size() && s.questRun.step<s.goals[s.cursor].steps.size() &&
            s.goals[s.cursor].steps[s.questRun.step].action==Impl::QuestAction::Portal &&
            s.questRun.pending->action==Impl::QuestPending::Action::Object) s.questRun.pending->deadline=found->second.deadline;
    }
    if(const auto found=s.passageWaits.find(Impl::Passage::Transfer);found!=s.passageWaits.end()) {
        if(s.travelPhase==Impl::TravelPhase::CrossWait || s.portalUsed) s.actionDeadline=found->second.deadline;
        if(s.supplyPortalUsed) s.supplyPortalDeadline=found->second.deadline;
    }
    if(s.portalIssued && !s.portalUsed) if(const auto found=s.passageWaits.find(Impl::Passage::CreatePortal);found!=s.passageWaits.end())
        s.actionDeadline=found->second.deadline;
    if(s.questRun.worldWait) s.questRun.worldWait->deadline+=suspended;
    if(s.questRun.witnessUntil!=Impl::Clock::time_point{}) s.questRun.witnessUntil+=suspended;
    if(s.questRun.missingTopicSince!=Impl::Clock::time_point{}) s.questRun.missingTopicSince+=suspended;
    if(s.localPad && !s.localPad->waiting) s.localPad->deadline+=suspended;
    if(s.combatTarget) {
        s.combatRun.progressAt+=suspended;s.combatRun.strategyAt+=suspended;
        if(s.combatRun.delayObservedAt!=Impl::Clock::time_point{}) s.combatRun.delayObservedAt+=suspended;
        if(s.combatRun.suspendedAt!=Impl::Clock::time_point{}) s.combatRun.suspendedAt+=suspended;
        if(s.combatRun.approachUntil!=Impl::Clock::time_point{}) s.combatRun.approachUntil+=suspended;
        if(s.combatRun.kiteUntil!=Impl::Clock::time_point{}) s.combatRun.kiteUntil+=suspended;
        if(s.combatRun.nextKite!=Impl::Clock::time_point{}) s.combatRun.nextKite+=suspended;
    }
    if(s.shelfObserved) s.shopDeadline+=suspended;
    if(!s.decisionDue(c,now)) return;
    ++s.decisions;
    try {
        if(view.gameGeneration!=s.game) {
            s.finish("cancelled","Game changed",c);return;
        }
        if(view.stage!=OnlineStage::ProtocolReady) {
            if(view.error && view.error->kind!=OnlineErrorKind::Input) s.finish("failed",view.error->message,c);
            else s.wait("loading","Waiting for the normal protocol/map transition");
            return;
        }
        if(onlinePlayerDead(view.world)) {s.finish("failed","Player died; AI never fabricates resurrection",c);return;}
        if(view.world.playerTrade.active()) {s.finish("cancelled","Player trade took control",c);return;}
        if(s.area!=town.read().area || s.areaGeneration!=view.world.areaGeneration) {
            const bool leavingTown=s.area && s.area==view.load.townArea && town.read().area && town.read().area!=view.load.townArea;
            if(town.read().town) s.excursionSkillPrepared=false;
            s.townExitArea.reset();
            s.deferredWaypoint.reset();
            s.summitTrialKills.reset();s.waypointDepartureArea.reset();s.nearbyWaypointArea.reset();s.missingWaypointAt.reset();
            s.hazardDodge.reset();s.hazardDiagnostics=nullptr;s.passageWaits.clear();
            s.defendedTravel.reset();
            s.backtrackAttempts.clear();
            if(!s.viewpointAttempts.empty()) s.note("observation-viewpoints-cleared",Impl::Json{
                {"reason","area-changed"},{"count",s.viewpointAttempts.size()}}.dump());
            s.viewpointAttempts.clear();s.viewpointRoute.reset();
            s.cancelWalk(c);s.area=town.read().area;s.areaGeneration=view.world.areaGeneration;s.visited.clear();s.frontierAttempts.clear();s.exploring.reset();
            if(leavingTown && s.teleportAvailable(c,false)) {
                s.townExitArea=s.area;s.townExitObserveUntil=now+std::chrono::milliseconds(1500);
            }
            s.checkedHints.clear();s.unavailableRooms.clear();s.approachHint.reset();s.approachFrontier.reset();s.approachDetour.reset();
            s.pickupApproaches.clear();
            s.continuation=Impl::Continuation::None;
            // Ordinary movement may cross back over a border. Its old exit
            // belongs to the previous area; recompute from the observed one.
            // Accepted warp/waypoint operations retain their result wait.
            if(s.travelPhase!=Impl::TravelPhase::WaypointWait &&
                !(s.travelPhase==Impl::TravelPhase::CrossWait && s.legUnit)) {
                s.travelPhase=Impl::TravelPhase::Route;s.legDestination.reset();s.legUnit.reset();s.waypointDestination.reset();
            }
            s.combatTarget.reset();s.combatOwned=false;s.retreatPoint.reset();s.note("area-observed",s.area?std::to_string(*s.area):"loading");
            s.combatRun={};
            s.mobilityTarget.reset();s.mobilityHold=false;
            s.mobilityGoal.reset();s.mobilityRoute.clear();
            s.mobilityRouteProgress=s.mobilityPendingProgress=s.mobilityRouteLength=0;
        }
        if(s.cursor>=s.goals.size()) {s.finish("completed","All goal postconditions observed",c);return;}
        inventory.update(view);combat.update();
        s.prepareCombatSkill(c);
        s.supplyVitals={{"life",view.world.life?Impl::Json(*view.world.life):Impl::Json(nullptr)},
            {"mana",view.world.mana?Impl::Json(*view.world.mana):Impl::Json(nullptr)},
            {"displayMaxLife",supplies.maxLife?Impl::Json(*supplies.maxLife):Impl::Json(nullptr)},
            {"displayMaxMana",supplies.maxMana?Impl::Json(*supplies.maxMana):Impl::Json(nullptr)}};
        if(s.supplyPolicy.enabled) {
            const auto stock=s.portalStock(c);
            s.supplyVitals["portalBooks"]=stock.books;s.supplyVitals["portalPages"]=stock.pages;
            s.supplyVitals["portalCapacity"]=stock.capacity;s.supplyVitals["portalStockKnown"]=stock.known;
            s.supplyVitals["healthPotions"]=s.potionCount(c,Impl::PotionKind::Life);
            s.supplyVitals["manaPotions"]=s.potionCount(c,Impl::PotionKind::Mana);
        }
        s.mobilityHold=false;
        const auto &goal=s.goals[s.cursor];
        s.observeDrops(c,goal);
        s.observeActions(c);s.describeActions(c);
        if(s.actionRecoveryBusy && s.canInput(c,DebugActionOwner::Potion,DebugItem) && s.drink(c)) {
            s.describeActions(c);return;
        }
        if(!view.error || view.error->kind!=OnlineErrorKind::Timeout) s.npcOpeningRecovery=false;
        const auto &item=view.world.itemRequest;
        const bool shelfTimeout=s.shopIssued && !s.potionRequest && !s.shelfObserved && s.supplyNpc &&
            view.error && view.error->kind==OnlineErrorKind::Timeout &&
            view.error->message=="No related server item update arrived; inspect current inventory before retrying" &&
            item && item->state==OnlineItemRequest::State::TimedOut && item->command.action==OnlineItemAction::TradeOpen &&
            item->command.npc==s.supplyNpc->id && s.shopGeneration==view.world.interactionGeneration;
        if(view.error && view.error->kind!=OnlineErrorKind::Input && !shelfTimeout && s.handledWaypointTimeout!=view.error->sequence) {
            const auto purpose=s.walkIntent.is_object()?s.walkIntent.value("purpose",std::string{}):std::string{};
            const bool optionalWaypoint=purpose=="initial-waypoint-memory" || purpose=="activate-waypoint";
            const bool waypointOpening=view.error->kind==OnlineErrorKind::Timeout &&
                view.error->message=="No original waypoint menu confirmation arrived" &&
                ((s.travelPhase==Impl::TravelPhase::WaypointOpen && s.travelRoute.size()>1) || optionalWaypoint ||
                 (!s.walkIntent.is_object() && s.deferredWaypoint && now<s.deferredWaypoint->retryAt));
            if(waypointOpening) {
                s.handledWaypointTimeout=view.error->sequence;
                if(s.travelPhase==Impl::TravelPhase::WaypointOpen || optionalWaypoint) {
                    const auto child=walk.snapshot();const auto &source=child.at("unit");
                    if(!source.is_object()) {s.finish("failed",view.error->message,c);return;}
                    const OnlineUnitKey key{source.at("type").get<uint8_t>(),source.at("id").get<uint32_t>()};
                    const auto unit=view.world.units.find(key);
                    s.deferredWaypoint=Impl::WaypointDeferral{key,unit==view.world.units.end()?0:unit->second.assignmentRevision,
                        now+std::chrono::seconds(30),c.position};
                    s.note("waypoint-query-deferred","Opening had no menu result; continuing the retained ordinary route");
                    if(view.world.waypointRequested || view.world.waypointSource) control.useWaypoint(0);
                    s.cancelWalk(c);
                    if(!optionalWaypoint) {s.travelPhase=Impl::TravelPhase::Route;s.waypointDestination.reset();s.legDestination.reset();}
                }
                // The first subsequent ordinary input clears the session's
                // timeout. Until then retain the diagnostic, not a fake menu.
            } else {
                const bool npcOpening=view.error->kind==OnlineErrorKind::Timeout &&
                    view.error->message=="NPC did not return a conversation before timeout" &&
                    s.walkIntent.is_object() && s.walkIntent.value("goal",std::string{})=="unit" &&
                    s.walkIntent.value("unitType",0)==1 && s.walkIntent.value("interact",false) &&
                    !s.questRun.pending && !s.potionRequest && !s.shopIssued;
                if(npcOpening && (s.npcOpenRetries<2 || s.npcOpeningRecovery)) {
                    if(s.inputBlocked) {s.wait("npc-recovery-input","The opening timed out; retry input waits until observation closes");return;}
                    if(s.npcOpeningRecovery && walk.active()) return;
                    if(s.npcOpenRetries>=2) {s.finish("failed",view.error->message,c);return;}
                    ++s.npcOpenRetries;s.note("npc-open-retry","Opening only; no quest topic or item operation is replayed");
                    auto intent=s.walkIntent;s.cancelWalk(c);s.walkIntent=intent;
                    intent["forceApproach"]=true;intent["resyncApproach"]=s.npcOpenRetries==2;walk.command(intent,session,town,control,combat);
                    s.npcOpeningRecovery=true;return;
                }
                const bool returning=s.cursor<s.goals.size() && (s.questLeaf(s.goals[s.cursor]).kind==Impl::Kind::ReturnTown ||
                    s.goals[s.cursor].kind==Impl::Kind::Restock || s.supplyStage==Impl::SupplyStage::ToTown ||
                    (s.explorationReturn && !s.explorationReturn->returning));
                const bool portalTimeout=returning &&
                    s.portalIssued && !s.portalUsed && view.error->kind==OnlineErrorKind::Timeout &&
                    view.error->message=="No new server-owned town portal was assigned";
                if(!portalTimeout) {s.finish("failed",view.error->message,c);return;}
                // The native request's result is unknown: never cast again or
                // assume a scroll was consumed. Use a separate ordinary walk home.
                s.note("town-portal-unconfirmed",view.error->message);
                s.stop(walk,control,combat);s.walkIntent=nullptr;s.portalIssued=false;s.passageWaits.erase(Impl::Passage::CreatePortal);
            }
        }
        if(s.inputBlocked) return;
        const bool travelSatisfied=goal.kind==Impl::Kind::Travel && goal.area &&
            (goal.requiredWaypoint?s.knownWaypointAreas.contains(*goal.area):goal.area==s.area);
        if(s.actionRecoveryBusy) {
            if(!travelSatisfied && s.supplyStage==Impl::SupplyStage::None && now>=s.goalDeadline) {
                s.finish("failed","Current high-level goal exceeded its execution budget during action recovery",c);return;
            }
            s.prefetchExploration(c);
            s.wait("action-recovery","Observing the normal action animation; potion use and result observation remain active");
            s.describeActions(c);return;
        }
        if(s.pickup && !s.pickup->observed) {
            s.wait("pickup-wait","Waiting for the submitted pickup to confirm personal ownership");return;
        }
        if(walk.recoveringMovement()) {
            if(!travelSatisfied && s.supplyStage==Impl::SupplyStage::None && now>=s.goalDeadline) {
                s.finish("failed","Current high-level goal exceeded its execution budget during walking recovery",c);return;
            }
            if(s.canInput(c,DebugActionOwner::Potion,DebugItem)) s.drink(c);
            const auto child=walk.snapshot();const auto &anchor=child.at("recoveryAnchor");
            s.wait("walking-recovery","n="+std::to_string(child.at("movementRecoveries").get<unsigned>())+
                " via="+std::to_string(anchor.at("x").get<unsigned>())+","+std::to_string(anchor.at("y").get<unsigned>()));
            s.describeActions(c);return;
        }
        if(s.defendNavigation(c,goal)) {s.describeActions(c);return;}
        if(s.explorationReturn) {
            if(s.canInput(c,DebugActionOwner::Potion,DebugItem) && s.drink(c)) {s.describeActions(c);return;}
            if(s.canInput(c,DebugActionOwner::Goal,DebugAllInputs)) s.resumeExplorationReturn(c);
            if(s.canInput(c,DebugActionOwner::Mobility,DebugMotion|DebugSkill)) s.mobility(c);
            s.describeActions(c);return;
        }
        const auto preparedGoal=s.cursor;
        const bool prepared=s.prepareQuest(c,goal);
        if(!s.active() || s.cursor!=preparedGoal) return;
        if(s.supplyStage==Impl::SupplyStage::None && now>=s.goalDeadline && !travelSatisfied) {
            s.finish("failed","Current high-level goal exceeded its execution budget",c);return;
        }
        using Owner=DebugActionOwner;
        if(s.closeWaypoint && s.canInput(c,Owner::Waypoint,DebugMenu)) {
            if(view.world.waypointSource==s.closeWaypoint && s.observedWaypointMenu==s.closeWaypoint &&
                now<s.waypointMenuOpenedAt+std::chrono::milliseconds(s.eventPolicy.waypointCloseDelayMs)) {
                if(s.canInput(c,Owner::Potion,DebugItem) && s.drink(c)) {s.describeActions(c);return;}
                s.wait("waypoint-close-delay","Waiting for the configured waypoint menu display time");
                s.describeActions(c);return;
            }
            if(view.world.waypointSource==s.closeWaypoint || view.world.waypointRequested==s.closeWaypoint) control.useWaypoint(0);
            s.closeWaypoint.reset();s.describeActions(c);return;
        }
        if(s.canInput(c,Owner::Potion,DebugItem) && s.drink(c)) {s.describeActions(c);return;}
        if(s.servicePassage(c,goal,prepared)) {s.describeActions(c);return;}
        if(s.canInput(c,Owner::Combat,DebugMotion|DebugSkill) && s.avoidHazards(c)) {s.describeActions(c);return;}
        if(s.canInput(c,Owner::Combat,DebugMotion|DebugSkill) && s.maintainArmor(c)) {s.describeActions(c);return;}
        if(s.pendingDoor) {if(s.canInput(c,Owner::Combat,DebugMotion|DebugSkill)) s.combat(c,goal,true);s.describeActions(c);return;}
        if(!prepared) {
            Impl::Goal defense;defense.kind=Impl::Kind::Travel;
            if(s.phase!="quest-drop-notice" && s.canInput(c,Owner::Combat,DebugMotion|DebugSkill)) s.combat(c,defense);
            s.describeActions(c);return;
        }
        if(goal.kind==Impl::Kind::Quest && s.questRun.pending && s.questWaitRequiresInput(c) &&
            s.canInput(c,Owner::Goal,DebugAllInputs)) {
            // Finish an arrived reading / query continuation before starting
            // another optional attack. Waiting for a reply grants no priority.
            s.execute(c,goal);s.describeActions(c);return;
        }
        const auto leaf=s.questLeaf(goal);
        if(s.resumeAction(c,goal,leaf)) {s.describeActions(c);return;}
        if(s.canInput(c,Owner::Supply,DebugAllInputs) && s.autoSupply(c,leaf)) {s.describeActions(c);return;}
        if(!s.pendingWaypoint && !s.closeWaypoint && !s.questRun.worldWait && !s.questRun.encounterAnchored &&
            !s.portalIssued && !s.supplyPortalUsed &&
            (!town.read().town || (leaf.kind!=Impl::Kind::ReturnTown && leaf.kind!=Impl::Kind::Restock)) &&
            s.canInput(c,Owner::Waypoint,DebugMotion|DebugInteraction|DebugMenu) && s.activateWaypoint(c)) {s.describeActions(c);return;}
        if(s.canInput(c,Owner::Combat,DebugMotion|DebugSkill) && ((leaf.kind==Impl::Kind::ReturnTown || leaf.kind==Impl::Kind::Restock)?
            s.supplyCombat(c,leaf):s.combat(c,leaf))) {s.describeActions(c);return;}
        unsigned inputs=DebugAllInputs;
        if(leaf.kind==Impl::Kind::Travel || leaf.kind==Impl::Kind::Find) inputs=DebugMotion|DebugInteraction|DebugMenu;
        else if(leaf.kind==Impl::Kind::Hunt) inputs=DebugMotion|DebugSkill;
        const auto owner=goal.kind==Impl::Kind::Restock?Owner::Supply:Owner::Goal;
        if(s.canInput(c,owner,inputs)) {s.resumeDefendedTravel(c);s.execute(c,goal);}
        if(s.canInput(c,Owner::Mobility,DebugMotion|DebugSkill)) s.mobility(c);
        s.describeActions(c);
    } catch(const DebugSearchPending &) {
        s.cadence.continueNextFrame();s.wait("navigation-worker","Waiting for background navigation");
        s.describeActions(c);
    } catch(const std::exception &e) {s.finish("failed",e.what(),c);}
}
}
