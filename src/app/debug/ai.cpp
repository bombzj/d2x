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
    copy("combat",{"range","crowdRadius","crowdCount","retreatDistance","engageRadius","waitDefenseRadius","noProgressSeconds",
        "stallTimeoutSeconds","retreatLifePercent","lifeDropPercent","rangedKite","kiteIntervalMs","kiteDistance",
        "kiteMinDistance","hazardLookaheadMs","hazardPadding"});
    copy("mobility",{"distance"});
    copy("supplies",{"lifePercent","manaPercent","criticalLifePercent","healthCount","manaCount","quiverCount","intervalMs"});
    copy("events",{"waitSeconds","initialWaitMs","doorInitialWaitMs","scrollInitialWaitMs","questLogDurationMs","waypointCloseDelayMs","npcDialogueCloseDelayMs","encounterRadius","standoffMin","standoffMax",
        "walkDecisionMs","walkReactionMs","walkRequestIntervalMs","walkRetryMs"});
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
            if(key!="skill" && key!="fallbackSkills" && key!="groupSkill" && key!="aura")
                throw std::invalid_argument("Unsupported class combat profile option: "+key);
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
    // appends. Only an explicit new start can replace skills or execution mode
    if(reloading) config=reloadPolicyNumbers(config);
    else if(appending) {
        auto plan=nlohmann::json::object();
        for(const auto key:{"goals","scenario","questNumber","goalTimeoutSeconds","useServiceRewards"})
            if(config.contains(key)) plan[key]=config.at(key);
        config=std::move(plan);
    }
    if(explicitRequest.contains("goals")) {config.erase("scenario");config.erase("questNumber");}
    else if(explicitRequest.contains("scenario") && !explicitRequest.contains("questNumber")) config.erase("questNumber");
    config.merge_patch(explicitRequest); // Explicit command arguments override even class default
    // Reload the file that was actually opened, even if the working directory
    // changes or another relative candidate is created while the plan run
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
        // Compiled MonStats identities count records with hcIdx, not source row
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
    return {1,id,v.dump(),{},false,j.contains("npc")};
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
        if(entry.contains("search")) {
            if(goal.kind!=Kind::Find && goal.kind!=Kind::Hunt && goal.kind!=Kind::Acquire && goal.kind!=Kind::Deliver)
                throw std::invalid_argument("search applies to find/hunt/acquire/deliver; use searchKnowledge for quest requirements");
            goal.search=parseSearchKnowledge(entry.at("search"),{});
            if(goal.search->radius && !goal.search->anchor) throw std::invalid_argument("Bounded goal search requires an anchor");
        }
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
            if(key!="skill" && key!="minLevel" && key!="range") throw std::invalid_argument("Unknown combat skill option: "+key);
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
        if(value.is_object() && value.contains("range")) {
            const auto range=integer(value,"range",16,2,30);
            const auto [entry,added]=p.skillRanges.emplace(id,range);
            if(!added && entry->second!=range)
                throw std::invalid_argument("Duplicate combat skill has different range: "+std::to_string(id));
        }
        return id;
    };
    if(j.contains("skill")) p.skill=candidate(j.at("skill"));
    if(j.contains("groupSkill")) p.groupSkill=candidate(j.at("groupSkill"));
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
    p.waitDefenseRadius=integer(j,"waitDefenseRadius",10,1,30);
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
    // Legacy townTurn keys remain accepted for saved diagnostic configs.
    for(const auto &[key,value]:j.items()) {
        (void)value;
        if(key!="waitSeconds" && key!="initialWaitMs" && key!="doorInitialWaitMs" && key!="scrollInitialWaitMs" && key!="questLogDurationMs" && key!="waypointCloseDelayMs" && key!="npcDialogueCloseDelayMs" && key!="encounterRadius" && key!="standoffMin" && key!="standoffMax" &&
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
    p.npcDialogueCloseDelayMs=integer(j,"npcDialogueCloseDelayMs",500,0,30000);
    p.walkDecisionMs=integer(j,"walkDecisionMs",160,1,5000);
    p.walkReactionMs=integer(j,"walkReactionMs",40,1,5000);
    p.walkRequestIntervalMs=integer(j,"walkRequestIntervalMs",200,0,5000);
    p.walkRetryMs=integer(j,"walkRetryMs",1000,0,30000);
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
void DebugAi::Impl::observeDiagnostics(const Context &c) {
    const auto &view=c.observation.read();
    const auto &alerts=view.world.questAlerts;
    if(alerts!=loggedQuestAlerts) {
        const auto describe=[&](const OnlineUnitKey &key) {
            const auto unit=view.world.units.find(key);
            return Json{{"id",key.id},{"class",unit!=view.world.units.end() && unit->second.classId?Json(*unit->second.classId):Json(nullptr)}};
        };
        Json added=Json::array(),removed=Json::array();
        for(const auto &key:alerts) if(!loggedQuestAlerts.contains(key)) added.push_back(describe(key));
        for(const auto &key:loggedQuestAlerts) if(!alerts.contains(key)) removed.push_back(describe(key));
        behaviorLog.event(cursor,"quest-alert",Json{{"added",std::move(added)},{"removed",std::move(removed)},
            {"active",alerts.size()},{"phase",phase}}.dump());
        loggedQuestAlerts=alerts;
    }
    const auto host=view.world.playerPosition;const auto here=c.town.read().area;
    const auto now=Clock::now();
    // Expected stationary waits own their own deadlines; only unexplained idling is reported.
    static constexpr std::array<std::string_view,13> expected{"quest-dialogue","quest-dialogue-result","quest-world-result",
        "supply-heal","supply-buy","npc-service-open","npc-service-speech","waypoint","quest-forge-result","quest-drop-spawn",
        "quest-portal","item-input-settle","town-portal-wait"};
    const bool waiting=std::any_of(expected.begin(),expected.end(),[&](auto name){return phase.rfind(name,0)==0;}) ||
        questState.questRun.worldWait || questState.questRun.pending || inputBlocked;
    if(!host || waiting || stallWatch.area!=here || !stallWatch.host ||
        std::abs(int(stallWatch.host->x)-int(host->x))+std::abs(int(stallWatch.host->y)-int(host->y))>2) {
        stallWatch={host,here,now,0};return;
    }
    if(now-stallWatch.since<std::chrono::seconds(8*(stallWatch.reports+1)) || stallWatch.reports>=4) return;
    ++stallWatch.reports;
    const auto full=snapshot();
    Json detail{{"stationaryMs",std::chrono::duration_cast<std::chrono::milliseconds>(now-stallWatch.since).count()},
        {"phase",phase},{"reason",reason},{"walk",debugWalkName(c.walk.observe().state)},{"walkActive",c.walk.active()},{"walkIntent",navigation.walkIntent},
        {"child",c.walk.snapshot()},{"actions",full.value("actions",Json(nullptr))},
        {"routeExecution",routeExecutionSnapshot()},{"combat",battle.combatDiagnostics},
        {"questProgress",full.value("questProgress",Json(nullptr))},{"travel",full.value("travel",Json(nullptr))},
        {"exploration",full.value("exploration",Json(nullptr))},{"populationMemory",full.value("populationMemory",Json(nullptr))},
        {"search",navigation.searchDiagnostics},{"targetSearch",navigation.targetSearchDiagnostics},{"approach",navigation.approachDiagnostics},{"mobility",navigation.mobilityDiagnostics},{"travelDiagnostics",navigation.travelDiagnostics}};
    behaviorLog.event(cursor,"stall-snapshot",detail.dump());
}
DebugAi::Impl::Json DebugAi::Impl::snapshot() const {
    Json passages=Json::array();
    for(const auto &[kind,pending]:navigation.passageWaits) passages.push_back({
        {"phase",passageName(kind)},{"attempts",pending.attempts},
        {"retryRemainingMs",std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(pending.retryAt-Clock::now()).count())},
        {"remainingMs",std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(pending.deadline-Clock::now()).count())}});
    Json drops=Json::array();
    for(const auto &[code,drop]:rememberedDrops)
        drops.push_back({{"code",code},{"id",drop.id},{"area",drop.area},{"x",drop.position.x},{"y",drop.position.y}});
    const auto population=area?navigation.populationMemory.find(*area):navigation.populationMemory.end();
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
    for(const auto &[point,attempt]:navigation.frontierAttempts) attempts.push_back({
        {"point",{point.first,point.second}},{"position",{attempt.position.x,attempt.position.y}},
        {"geometryRevision",attempt.geometryRevision},{"outcome",attempt.outcome},{"reason",attempt.reason},
        {"relayGoal",attempt.relayGoal?Json{attempt.relayGoal->x,attempt.relayGoal->y}:Json(nullptr)},
        {"reached",attempt.reached},{"retryInMs",std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(attempt.retryAt-Clock::now()).count())}});
    Json localLinks=Json::array();
    for(const auto &[key,record]:navigation.padObservations) localLinks.push_back({{"area",std::get<0>(key)},{"identity",std::get<1>(key)},
        {"source",{std::get<2>(key),std::get<3>(key)}},{"destination",{record.destination.x,record.destination.y}},{"visits",record.visits}});
    Json failedConnections=Json::array();
    for(const auto &[edge,failures]:navigation.failedWalkEdges)
        failedConnections.push_back({{"from",edge.first},{"to",edge.second},{"failures",failures}});
    return {{"state",state},{"active",active()},{"phase",phase},{"reason",reason},{"goalIndex",cursor},{"goals",std::move(plan)},
        {"configuration",{{"file",configurationFile},{"characterClass",configurationClass},{"reload","explicit"},
            {"navigationExperienceAreas",navigation.navigationExperience.size()}}},
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
            {"npcDialogueCloseDelayMs",eventPolicy.npcDialogueCloseDelayMs},
            {"walkDecisionMs",eventPolicy.walkDecisionMs},{"walkReactionMs",eventPolicy.walkReactionMs},
            {"walkRequestIntervalMs",eventPolicy.walkRequestIntervalMs},{"walkRetryMs",eventPolicy.walkRetryMs},
            {"standoffMin",eventPolicy.standoffMin},{"standoffMax",eventPolicy.standoffMax}}},
        {"worldWait",questState.questRun.worldWait?Json{{"area",questState.questRun.worldWait->area},
            {"anchor",{questState.questRun.worldWait->anchor.x,questState.questRun.worldWait->anchor.y}},{"expected",questState.questRun.worldWait->expected},
            {"progress",questState.questRun.worldWait->progress},{"lastProgress",questState.questRun.worldWait->lastProgress},
            {"residualRange",questState.questRun.worldWait->residualRange},{"movementRadius",encounterMovementRadius()},
            {"clearanceRevision",questState.questRun.worldWait->clearanceRevision},
            {"trackedEnemies",questState.questRun.worldWait->enemies.size()},{"clock","execution"},
            {"waitRemainingMs",std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(questState.questRun.worldWait->deadline-Clock::now()).count())}}:Json(nullptr)},
        {"gameGeneration",game},{"area",area?Json(*area):Json(nullptr)},{"decisionIntervalMs",cadence.intervalMs()},{"decisions",decisions},
        {"scheduler",{{"mode","adaptive"},{"wakeReason",cadence.reason()},{"eventWakeups",cadence.eventWakeups()},
            {"timerWakeups",cadence.timerWakeups()},{"searchContinuations",cadence.continuations()}}},
        {"combatTarget",battle.combatTarget?Json{{"type",battle.combatTarget->type},{"id",battle.combatTarget->id}}:Json(nullptr)},
        {"target",target?Json{{"type",target->type},{"id",target->id},{"assignment",targetAssignment}}:Json(nullptr)},
        {"walkIntent",navigation.walkIntent},{"travel",{{"phase",unsigned(navigation.travelPhase)},
            {"destination",navigation.travelDestination?Json(*navigation.travelDestination):Json(nullptr)},{"route",navigation.travelRoute},{"visitedAreas",navigation.travelVisitedAreas},
            {"leg",navigation.legDestination?Json(*navigation.legDestination):Json(nullptr)},
            {"unit",navigation.legUnit?Json{{"type",navigation.legUnit->type},{"id",navigation.legUnit->id}}:Json(nullptr)}}},
        {"waypointDeferral",navigation.deferredWaypoint?Json{{"unit",{{"type",navigation.deferredWaypoint->unit.type},{"id",navigation.deferredWaypoint->unit.id}}},
            {"assignment",navigation.deferredWaypoint->assignment},{"from",{navigation.deferredWaypoint->position.x,navigation.deferredWaypoint->position.y}},
            {"retryRemainingMs",std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(navigation.deferredWaypoint->retryAt-Clock::now()).count())},
            {"requiresNewApproach",true}}:Json(nullptr)},
        {"combatPolicy",{{"enabled",policy.enabled},{"skill",policy.skill?Json(*policy.skill):Json("Attack")},{"range",policy.range},
            {"departureSkill",battle.excursionSkill?Json(*battle.excursionSkill):Json(nullptr)},
            {"selectionMode","departure"},
            {"groupSkill",policy.groupSkill?Json(*policy.groupSkill):Json(nullptr)},
            {"fallbackSkills",policy.fallbackSkills},{"minimumSkillLevels",policy.minimumSkillLevels},{"skillRanges",policy.skillRanges},{"noProgressSeconds",policy.noProgressSeconds},
            {"stallTimeoutSeconds",policy.stallTimeoutSeconds},{"retreatLifePercent",policy.retreatLifePercent},{"lifeDropPercent",policy.lifeDropPercent},
            {"waitDefenseRadius",policy.waitDefenseRadius},
            {"scope",policy.scope},{"staticField",policy.staticField},{"avoidHazards",policy.avoidHazards},
            {"rangedKite",policy.rangedKite},{"kiteIntervalMs",policy.kiteIntervalMs},{"kiteDistance",policy.kiteDistance},
            {"kiteMinDistance",policy.kiteMinDistance},{"hazardLookaheadMs",policy.hazardLookaheadMs},{"hazardPadding",policy.hazardPadding},
            {"aura",policy.aura?Json(*policy.aura):Json("auto")},
            {"engageRadius",policy.engage},{"crowdRadius",policy.radius},{"crowdCount",policy.crowd},{"retreatDistance",policy.retreat}}},
        {"pending",{{"sourceInteraction",objectIssued},{"pickup",bool(pickup)},{"portal",portalIssued},{"portalUsed",portalUsed}}},
        {"waypointMemory",{{"game",navigation.waypointMemoryGame},{"historyRevision",navigation.waypointMemoryRevision},{"areas",navigation.knownWaypointAreas},{"requireBeforeDeparture",!navigation.requiredWaypointAreas.empty()}}},
        {"populationMemory",{{"game",navigation.populationMemoryGame},{"areas",navigation.populationMemory.size()},
            {"currentRooms",population==navigation.populationMemory.end()?0:population->second.rooms.size()},
            {"searchedRooms",population==navigation.populationMemory.end()?0:population->second.searchedRooms.size()},
            {"settlingRooms",population==navigation.populationMemory.end()?0:population->second.settlingRooms.size()},
            {"currentHostileHints",population==navigation.populationMemory.end()?0:population->second.enemies.size()}}},
        {"mobility",{{"mode",mobilityPolicy.mode},{"vigor",mobilityPolicy.vigor},{"distance",mobilityPolicy.distance},
            {"waitingForPosition",navigation.mobilityTarget.has_value()},
            {"departureArea",navigation.departureArea?Json(*navigation.departureArea):Json(nullptr)},
            {"departureRetryFrom",navigation.departureRetryFrom?Json{navigation.departureRetryFrom->x,navigation.departureRetryFrom->y}:Json(nullptr)}}},
        {"supplies",{{"enabled",supplyPolicy.enabled},{"buy",supplyPolicy.buy},{"returnWhenEmpty",supplyPolicy.returnWhenEmpty},
            {"lifePercent",supplyPolicy.lifePercent},{"manaPercent",supplyPolicy.manaPercent},{"criticalLifePercent",supplyPolicy.criticalLifePercent},
            {"quiverCount",supplyPolicy.quiverCount},
            {"healthCount",supplyPolicy.healthCount},{"manaCount",supplyPolicy.manaCount},{"fillPortalBook",supplyPolicy.fillPortalBook},{"intervalMs",supplyPolicy.intervalMs},
            {"stage",std::array{"none","to-town","shop","return"}[size_t(supply.supplyStage)]},
            {"returnArea",supply.supplyOrigin?Json(*supply.supplyOrigin):Json(nullptr)},
            {"pending",supply.potionRequest?Json{{"item",supply.potionRequest->item},{"code",supply.potionRequest->code},{"buying",supply.potionRequest->buying},
                {"sequence",supply.potionRequest->sequence},{"expectedGain",supply.potionRequest->expectedGain}}:Json(nullptr)},
            {"drinksConfirmed",supply.drinks},{"purchasesConfirmed",supply.purchases},{"vitals",supply.supplyVitals},{"lastPotion",supply.lastPotion}}},
        {"questProgress",cursor<goals.size() && goals[cursor].kind==Kind::Quest?Json{
            {"quest",goals[cursor].quest},{"step",questState.questRun.step},{"steps",goals[cursor].steps.size()},
            {"encounterAnchor",questState.questRun.encounterPosition?Json{questState.questRun.encounterPosition->x,questState.questRun.encounterPosition->y}:Json(nullptr)},
            {"encounterAnchored",questState.questRun.encounterAnchored},
            {"encounterStandoff",questState.questRun.encounterStandoff?Json{questState.questRun.encounterStandoff->x,questState.questRun.encounterStandoff->y}:Json(nullptr)},
            {"encounterStance",questState.questRun.encounterStanceDiagnostics},
            {"inventoryWork",{{"state",std::array{"idle","running","done","failed"}[size_t(questState.questRun.inventoryWork.state())]},
                {"kind",questState.questRun.inventoryWork.kind()==InventoryWorkflow::Kind::Combine?"combine":"store"},{"reason",questState.questRun.inventoryWork.reason()},
                {"completedOperations",questState.questRun.inventoryWork.completedOperations()},
                {"remainingPlacements",questState.questRun.inventoryWork.remainingPlacements()}}},
            {"mode",goals[cursor].requirements.empty()?"steps":"conditions"},
            {"condition",questState.questRun.step<goals[cursor].requirements.size()?Json(goals[cursor].requirements[questState.questRun.step].name):Json(nullptr)},
            {"action",questState.questRun.step<goals[cursor].steps.size()?Json(std::array{"talk","clear","hunt","acquire","read-scroll","stones","operate","portal","return-town","imbue","act-travel","travel","use-item","combine","submit-staff","search-drop","equip-weapon","smash-orb","smash-forge","service","rescue","encounter"}[size_t(goals[cursor].steps[questState.questRun.step].action)]):Json(nullptr)},
            {"pending",questState.questRun.pending?Json{{"action",unsigned(questState.questRun.pending->action)},{"item",questState.questRun.pending->item},
                {"operationPending",questState.questRun.pending->operationPending},
                {"operationMode",questState.questRun.pending->operationMode?Json(*questState.questRun.pending->operationMode):Json(nullptr)},
                {"sequence",questState.questRun.pending->sequence},{"unit",questState.questRun.pending->unit?Json(questState.questRun.pending->unit->id):Json(nullptr)}}:Json(nullptr)},
            {"clearStatusQueries",questState.questRun.sweeps},{"materialPhase",questState.questRun.materialPhase}}:Json(nullptr)},
        {"exploration",{{"model","observed-passages"},{"frontierAttempts",std::move(attempts)},
            {"mapCacheBuilds",navigation.mapCacheBuilds},{"pathSearches",navigation.pathSearches},
            {"frontier",navigation.frontierRoute?Json{{"area",navigation.frontierRoute->area},{"target",{navigation.frontierRoute->target.x,navigation.frontierRoute->target.y}},
                {"direction",navigation.frontierRoute->passage},{"evidence",navigation.frontierRoute->evidence},
                {"pathPoints",navigation.frontierRoute->path.size()},{"stalls",navigation.frontierRoute->stalls}}:Json(nullptr)}}},
        {"pickupApproachesTried",pickupApproaches.size()},
        {"goalExecutionRemainingMs",std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(goalDeadline-Clock::now()).count())},
        {"combatFeedback",battle.combatDiagnostics},{"hazardFeedback",battle.hazardDiagnostics},{"passageWaits",std::move(passages)},
        {"localTravel",{{"links",std::move(localLinks)},{"transfersWithoutDiscovery",navigation.padTransfersWithoutDiscovery},
            {"pending",navigation.localPad?Json{{"unit",navigation.localPad->unit.id},{"area",navigation.localPad->area},{"waiting",navigation.localPad->waiting},
                {"source",{navigation.localPad->source.x,navigation.localPad->source.y}},{"discontinuityBefore",navigation.localPad->discontinuity},
                {"requestRevision",navigation.localPad->requestRevision}}:Json(nullptr)}}},
        {"questPlan",questState.questPlanDiagnostics},{"rememberedDrops",drops},
        {"routeExecution",routeExecutionSnapshot()},{"targetSearch",navigation.targetSearchDiagnostics},
        {"failedConnections",std::move(failedConnections)},
        {"actions",actionDiagnostics},{"navigationDiagnostics",{{"search",navigation.searchDiagnostics},{"approach",navigation.approachDiagnostics},{"mobility",navigation.mobilityDiagnostics},{"travel",navigation.travelDiagnostics},{"terrainClue",navigation.terrainClueDiagnostics}}},
        {"castRequests",castRequests},{"navigationGoals",moveGoals},{"interactions",interactions},{"history",history}};
}
void DebugAi::Impl::stop(DebugAutoWalk &walk,IOnlineControl &control,IOnlineCombat &combat) {
    walk.manualInput(control);control.cancelMovement();
    OnlineCombatCommand command;command.action=OnlineCombatCommand::Action::Stop;
    try {if(!combat.submit(command)) note("stop-unavailable",combat.reason());}
    catch(const std::exception &e) {note("stop-unavailable",e.what());}
    battle.combatOwned=false;battle.combatRun.repeating=false;navigation.mobilityHold=false;navigation.mobilityTarget.reset();
}
void DebugAi::Impl::cancelWalk(Context &c) {
    submissionWaits.erase(DebugActionOwner::Navigation);
    c.walk.manualInput(c.control);c.control.cancelMovement();navigation.walkIntent={};
}
void DebugAi::Impl::finish(std::string value,std::string detail,Context &c) {
    // Failure stops movement, so it cannot rely on the normal move-to-close
    // path to dismiss the conversation opened by this AI's current action.
    if(value=="failed" && c.observation.read().stage==OnlineStage::ProtocolReady && c.observation.read().gameGeneration==game &&
        c.observation.read().world.npcRequested && bool(navigation.walkIntent) && (navigation.walkIntent.unit?navigation.walkIntent.unit->type:-1)==1 &&
        (navigation.walkIntent.unit?navigation.walkIntent.unit->id:uint32_t{})==*c.observation.read().world.npcRequested) {
        if(c.control.closeNpc()) note("failed-dialogue-closed","Released the AI's NPC conversation before stopping");
        else note("failed-dialogue-close-rejected","Client did not accept closing the current conversation");
    }
    if(c.observation.read().stage==OnlineStage::ProtocolReady && c.observation.read().gameGeneration==game)
        stop(c.walk,c.control,c.combat);
    else {
        // The old game's stop must neither affect a new game nor replace a
        // transport/protocol failure with an unrelated input-stage error.
        cancelWalk(c);battle.combatOwned=false;battle.combatRun.repeating=false;navigation.mobilityHold=false;navigation.mobilityTarget.reset();
    }
    behaviorLog.endPhase();
    state=std::move(value);reason=std::move(detail);phase.clear();note(state,reason);behaviorLog.close();
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
    questState.questPlanDiagnostics=nullptr;
    if(c.observation.read().world.npcRequested) c.control.closeNpc();
    if(c.observation.read().world.waypointSource) c.control.useWaypoint(0);
    if(skipped) goals.at(cursor).skipReason=evidence;
    if(!skipped && goals.at(cursor).kind==Kind::Quest && eventPolicy.questLogDurationMs>0) {
        questState.questLogPending=true;questState.questLogGame=c.observation.read().gameGeneration;
        questState.questLogConnection=c.observation.read().connectionGeneration;
    }
    cancelWalk(c);behaviorLog.endPhase();note(skipped?"goal-skipped":"goal-completed",std::move(evidence));++cursor;resetStep();questState.questRun={};navigation.padTransfersWithoutDiscovery=0;
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
bool DebugAi::controlsMovement() const {return impl_->active() && (Impl::Clock::now()<impl_->npcDialogueReadyAt() ||
    impl_->navigation.mobilityHold || impl_->actionRecoveryBusy || impl_->supply.quiverTransfer ||
    (impl_->navigation.routeExecution && impl_->navigation.routeExecution->phase!=Impl::RouteExecution::Phase::Resume &&
     (impl_->navigation.walkIntent==impl_->navigation.routeExecution->intent ||
      (!impl_->battle.hazardDodge && !impl_->battle.retreatPoint && !impl_->battle.combatRun.reposition && !impl_->battle.combatRun.kitePoint))));}
bool DebugAi::controlsInventory() const {
    const auto &s=*impl_;
    if(s.active() && (s.supply.supplyStage==Impl::SupplyStage::Shop ||
        (s.cursor<s.goals.size() && s.goals[s.cursor].kind==Impl::Kind::Restock))) return true;
    if(!s.active() || s.cursor>=s.goals.size() || s.goals[s.cursor].kind!=Impl::Kind::Quest ||
        s.questState.questRun.step>=s.goals[s.cursor].steps.size()) return false;
    const auto action=s.goals[s.cursor].steps[s.questState.questRun.step].action;
    return s.questState.questRun.initialInventory || s.questState.questRun.recoveringItem || action==Impl::QuestAction::Combine || action==Impl::QuestAction::SubmitStaff || action==Impl::QuestAction::Imbue || action==Impl::QuestAction::EquipWeapon;
}
void DebugAi::Impl::cancel(DebugAutoWalk &walk,IOnlineControl &control,IOnlineCombat &combat,std::string why) {
    if(!live()) return;
    stop(walk,control,combat);behaviorLog.endPhase();state="cancelled";
    reason=std::move(why);note(state,reason);behaviorLog.close();
}
void DebugAi::manualInput(DebugAutoWalk &walk,IOnlineControl &control,IOnlineCombat &combat) {
    impl_->cancel(walk,control,combat,"Escape released AI control");
}
std::optional<std::string> DebugAi::command(const nlohmann::json &raw,const OnlineObservation &observation,const RemoteTown &town,
        DebugAutoWalk &walk,IOnlineControl &control,IOnlineCombat &combat) {
    const auto name=raw.at("command").get<std::string>();if(!name.starts_with("debug-ai-")) return {};
    auto &s=*impl_;const auto now=Impl::Clock::now();
    auto request=raw;
    if(name=="debug-ai-reload-config" && !request.contains("configFile") && !s.configurationFile.empty()) request["configFile"]=s.configurationFile;
    std::optional<uint16_t> characterClass;
    if(observation.read().load.playerUnitId) {
        const auto player=observation.read().world.units.find({0,*observation.read().load.playerUnitId});
        if(player!=observation.read().world.units.end()) characterClass=player->second.classId;
    }
    const auto j=name=="debug-ai-start" || name=="debug-ai-append" || name=="debug-ai-reload-config"?configuredAiRequest(request,characterClass):request;
    if(name=="debug-ai-status") return Impl::Json{{"ok",true},{"ai",s.snapshot()}}.dump();
    if(name=="debug-ai-terrain-export") {
        // Offline replay input: what the explorer remembers of the current level, not the generated map.
        const auto *memory=s.area?walk.navigationSearch().explorationMemory.area(*s.area):nullptr;
        if(!memory) throw std::invalid_argument("No observed terrain for the current area");
        if(s.behaviorLog.path.empty()) throw std::invalid_argument("AI behavior log directory is unavailable");
        const auto &m=*memory;
        Impl::Json route=nullptr;
        if(s.navigation.frontierRoute) {
            route={{"target",{s.navigation.frontierRoute->target.x,s.navigation.frontierRoute->target.y}},
                {"direction",s.navigation.frontierRoute->passage},{"evidence",s.navigation.frontierRoute->evidence},
                {"stalls",s.navigation.frontierRoute->stalls},{"path",Impl::Json::array()}};
            for(const auto point:s.navigation.frontierRoute->path) route["path"].push_back({point.x,point.y});
        }
        auto passages=Impl::Json::array(),trails=Impl::Json::array();
        for(const auto &p:m.passages) passages.push_back({{"id",p.id},{"direction",p.direction},
            {"kind",p.units?"unit-observation":"terrain-observation"},
            {"boundary",p.boundary},{"approaches",p.approaches},{"evidence",p.evidence},{"deferred",p.deferred==p.evidence}});
        for(const auto &[at,edges]:m.walked) trails.push_back({{"from",at},{"to",edges}});
        const auto player=observation.read().world.playerPosition;
        auto sketchInk=Impl::Json::array(),services=Impl::Json::array(),ownership=Impl::Json::array();
        if(m.sketch) for(const auto pixel:m.sketch->ink) sketchInk.push_back({pixel.x,pixel.y});
        if(m.sketch) for(const auto &entry:m.sketch->townServices)
            services.push_back({{"identity",entry.identity},{"position",{entry.position.x,entry.position.y}}});
        for(const auto &[cell,level]:m.terrainLevels) ownership.push_back({cell.first,cell.second,level});
        const Impl::Json dump{{"format","d2x-ai-terrain-v3"},{"game",s.game},{"area",*s.area},{"decision",s.decisions},
            {"observedStorageBounds",{m.x,m.y,m.width,m.height}},{"revision",m.revision},{"collisionPublication",m.geometry},{"sourceTerrainRevision",m.sample},{"blockingMask",playerMovement.mask},
            {"player",player?Impl::Json{player->x,player->y}:Impl::Json(nullptr)},
            { "knowledgePolicy","client-collision-unit-observation-and-automap-independent"},{"terrainLevels",std::move(ownership)},
            {"unitObservationTiles",m.unitObserved},
            {"experienceBounds",m.expectedBounds?Impl::Json(*m.expectedBounds):Impl::Json(nullptr)},
            { "masks",m.flatMasks()},{"mapSketch",m.sketch?Impl::Json{{"revision",m.sketch->revision},{"primitives",m.sketch->primitives},{"ink",std::move(sketchInk)},{"townServices",std::move(services)}}:Impl::Json(nullptr)},{"frontier",std::move(route)},{"passages",std::move(passages)},{"walkedRoutes",std::move(trails)}};
        const auto file=std::filesystem::path(s.behaviorLog.path).parent_path()/
            ("terrain-"+std::to_string(*s.area)+"-"+std::to_string(s.decisions)+".json");
        std::ofstream output(file,std::ios::binary);
        output<<dump.dump();
        if(!output) throw std::runtime_error("Terrain export could not be written");
        return Impl::Json{{"ok",true},{"file",file.string()}}.dump();
    }
    if(name=="debug-ai-cancel") {
        s.cancel(walk,control,combat,"Cancelled by debug command");
    }
    else if(name=="debug-ai-pause") {
        if(s.active()) {
            const auto submittedMovement=s.navigation.mobilityTarget;
            const bool incomplete=walk.active();s.stop(walk,control,combat);
            // Stop future input, retaining the real-time observation of an
            // already accepted movement skill. Pause cannot undo that cast.
            s.navigation.mobilityTarget=submittedMovement;
            if(incomplete) s.navigation.walkIntent={};
            s.behaviorLog.endPhase();s.state="paused";s.pausedAt=now;s.note("paused");
        }
    } else if(name=="debug-ai-resume") {
        if(s.state!="paused") throw std::invalid_argument("AI is not paused");
        if(s.game!=observation.read().gameGeneration) throw std::invalid_argument("AI plan belongs to a previous game");
        if(s.areaGeneration!=observation.read().world.areaGeneration || s.area!=town.read().area)
            throw std::invalid_argument("Paused AI area changed; cancel and start a new high-level plan");
        // Tick accounts only suspended execution budgets. Native requests
        // and their real-time deadlines keep running during an explicit pause.
        s.cadence.wake();s.state="running";s.note("resumed",std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now-s.pausedAt).count())+"ms paused");
    } else if(name=="debug-ai-reload-config") {
        if(!s.live() || s.game!=observation.read().gameGeneration || !s.definitions) throw std::invalid_argument("No live AI plan in this game to configure");
        for(const auto &[key,value]:j.items()) {
            (void)value;
            if(key!="command" && key!="configFile" && key!="combat" && key!="mobility" && key!="supplies" && key!="events")
                throw std::invalid_argument("Live AI configuration only changes combat, mobility, supply and event policy numbers");
        }
        const auto &p=s.policy;
        auto nextCombat=s.parseCombat(policyNumbers({{"range",p.range},{"crowdRadius",p.radius},{"crowdCount",p.crowd},
            {"retreatDistance",p.retreat},{"engageRadius",p.engage},{"waitDefenseRadius",p.waitDefenseRadius},{"noProgressSeconds",p.noProgressSeconds},
            {"stallTimeoutSeconds",p.stallTimeoutSeconds},{"retreatLifePercent",p.retreatLifePercent},{"lifeDropPercent",p.lifeDropPercent},
            {"rangedKite",p.rangedKite},{"kiteIntervalMs",p.kiteIntervalMs},{"kiteDistance",p.kiteDistance},
            {"kiteMinDistance",p.kiteMinDistance},{"hazardLookaheadMs",p.hazardLookaheadMs},{"hazardPadding",p.hazardPadding}},
            j.value("combat",Impl::Json::object())));
        nextCombat.enabled=p.enabled;nextCombat.scope=p.scope;nextCombat.skill=p.skill;nextCombat.fallbackSkills=p.fallbackSkills;
        nextCombat.groupSkill=p.groupSkill;
        nextCombat.minimumSkillLevels=p.minimumSkillLevels;nextCombat.skillRanges=p.skillRanges;
        nextCombat.aura=p.aura;nextCombat.staticField=p.staticField;nextCombat.avoidHazards=p.avoidHazards;
        auto nextMobilityPolicy=s.parseMobility(policyNumbers({{"distance",s.mobilityPolicy.distance}},j.value("mobility",Impl::Json::object())));
        nextMobilityPolicy.mode=s.mobilityPolicy.mode;nextMobilityPolicy.vigor=s.mobilityPolicy.vigor;
        const auto &stock=s.supplyPolicy;
        auto nextSupply=s.parseSupplies(policyNumbers({{"lifePercent",stock.lifePercent},{"manaPercent",stock.manaPercent},
            {"criticalLifePercent",stock.criticalLifePercent},{"healthCount",stock.healthCount},{"manaCount",stock.manaCount},
            {"quiverCount",stock.quiverCount},{"intervalMs",stock.intervalMs}},j.value("supplies",Impl::Json::object())));
        nextSupply.enabled=stock.enabled;nextSupply.buy=stock.buy;nextSupply.returnWhenEmpty=stock.returnWhenEmpty;nextSupply.fillPortalBook=stock.fillPortalBook;
        const auto nextEvents=s.parseEvents(policyNumbers({{"waitSeconds",s.eventPolicy.waitSeconds},
            {"initialWaitMs",s.eventPolicy.initialWaitMs},{"doorInitialWaitMs",s.eventPolicy.doorInitialWaitMs},{"scrollInitialWaitMs",s.eventPolicy.scrollInitialWaitMs},
            {"questLogDurationMs",s.eventPolicy.questLogDurationMs},
            {"waypointCloseDelayMs",s.eventPolicy.waypointCloseDelayMs},
            {"npcDialogueCloseDelayMs",s.eventPolicy.npcDialogueCloseDelayMs},
            {"walkDecisionMs",s.eventPolicy.walkDecisionMs},{"walkReactionMs",s.eventPolicy.walkReactionMs},
            {"walkRequestIntervalMs",s.eventPolicy.walkRequestIntervalMs},{"walkRetryMs",s.eventPolicy.walkRetryMs},
            {"encounterRadius",s.eventPolicy.encounterRadius},{"standoffMin",s.eventPolicy.standoffMin},{"standoffMax",s.eventPolicy.standoffMax}},
            j.value("events",Impl::Json::object())));
        s.policy=std::move(nextCombat);s.mobilityPolicy=std::move(nextMobilityPolicy);s.supplyPolicy=std::move(nextSupply);
        s.eventPolicy=nextEvents;
        walk.setTiming(nextEvents);s.cadence.wake();
        s.configurationFile=j.value("configFile",std::string{});
        s.note("config-reloaded",j.value("configFile",std::string("inline policy")));
    } else if(name=="debug-ai-start" || name=="debug-ai-append") {
        const auto &view=observation.read();
        if(view.protocolProfile!=net::protocol::clientCompatibility(net::protocol::ClientProfile::Lod113c).id)
            throw std::invalid_argument("Debug task recipes currently support the retail 1.13c profile only");
        if(view.stage!=OnlineStage::ProtocolReady || onlinePlayerDead(view.world) || !town.read().nativeMapReady)
            throw std::invalid_argument("AI requires a live player in a ready native map");
        if(view.world.playerTrade.active()) throw std::invalid_argument("Finish the player trade before starting AI");
        if(!s.definitions) s.definitions=std::make_unique<Impl::Definitions>(s.archives);
        std::set<uint16_t> doorClasses;
        for(size_t row=0;row<s.definitions->objects.rows().size();++row) {
            const auto operation=s.definitions->objects.number(row,"OperateFn");
            const auto identity=s.definitions->objects.number(row,"Id");
            if(identity && *identity>=0 && *identity<=UINT16_MAX && (operation==8 || operation==61) &&
                s.definitions->objects.number(row,"Selectable0").value_or(0)) doorClasses.insert(uint16_t(*identity));
        }
        walk.navigationSearch().setDoorClasses(std::move(doorClasses));
        auto plan=s.parse(j);auto policy=s.parseCombat(j.value("combat",Impl::Json::object()));
        auto experience=s.parseExperience(j.value("navigationExperience",Impl::Json::object()));
        s.configureSearchKnowledge(plan,j.value("searchKnowledge",Impl::Json::object()));
        std::set<uint16_t> requiredWaypoints;
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
                requiredWaypoints.insert(level);
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
            const bool available=std::any_of(combat.skills().begin(),combat.skills().end(),[&](const auto &v) {
                const bool configured=v.id==policy.skill || std::find(policy.fallbackSkills.begin(),policy.fallbackSkills.end(),v.id)!=policy.fallbackSkills.end();
                const auto minimum=policy.minimumSkillLevels.find(v.id);
                return configured && !v.passive && std::max(unsigned(v.level),v.innate?1u:0u)>=
                    (minimum==policy.minimumSkillLevels.end()?1u:minimum->second);
            });
            if(!available) throw std::invalid_argument("No configured combat attack meets its minimum effective skill level; learn a candidate or configure a usable fallback");
        }
        if(name=="debug-ai-start" && policy.aura) {
            if(std::none_of(combat.skills().begin(),combat.skills().end(),[&](const auto &v){return v.id==policy.aura && v.level && !v.passive;}))
                throw std::invalid_argument("Configured battle aura is not currently learned");
        }
        if(name=="debug-ai-start" && policy.enabled) {
            bool aura=policy.aura.has_value();
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
            if(j.contains("combat") || j.contains("mobility") || j.contains("supplies") || j.contains("events") || j.contains("navigationExperience"))
                throw std::invalid_argument("Append inherits the current plan's combat, mobility, supplies, events and navigation experience");
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
            s.navigation.requiredWaypointAreas=std::move(requiredWaypoints);s.navigation.waypointDepartureArea.reset();s.navigation.nearbyWaypointArea.reset();s.navigation.missingWaypointAt.reset();
            s.policy=policy;s.mobilityPolicy=mobility;s.supplyPolicy=supplies;s.goals=std::move(plan);s.cursor=0;s.resetStep();s.history.clear();s.sequence=s.decisions=0;
            s.navigation.navigationExperience=std::move(experience);
            s.navigation.targetSearches.clear();s.navigation.activeTargetSearch.reset();s.navigation.targetSearchDiagnostics=nullptr;
            s.battle.excursionSkill.reset();s.battle.excursionSkillPrepared=false;
            s.configurationClass=characterClass && *characterClass<classNames.size()?classNames[*characterClass]:"unknown";
            s.eventPolicy=events;
            s.npcDialogueDisplay.reset();
            walk.setTiming(events);
            s.rememberedDrops.clear();
            s.questState.summitTrialKills.reset();
            s.navigation.deferredWaypoint.reset();
            s.navigation.handledWaypointTimeout.reset();
            s.navigation.padTransfersWithoutDiscovery=0;
            if(s.game!=view.gameGeneration) {
                s.navigation.failedWalkEdges.clear();
                s.questState.journalReadGame.reset();s.navigation.padObservations.clear();s.navigation.failedPads.clear();s.navigation.padArrival.reset();s.questState.questHandsRestore={};
                s.navigation.padProgressCells=s.navigation.padProgressTargets=s.navigation.padTransfersWithoutDiscovery=0;
            }
            s.castRequests=s.moveGoals=s.interactions=0;s.game=view.gameGeneration;s.areaGeneration=view.world.areaGeneration;s.area=town.read().area;
            s.battle.combatTarget.reset();s.battle.retreatPoint.reset();s.battle.combatOwned=false;
            s.battle.combatLifeSamples.clear();s.battle.combatMaxLife.reset();
            s.battle.lastAttack={};s.battle.retreatUntil={};s.battle.retreatReady={};
            s.navigation.mobilityTarget.reset();s.navigation.mobilityHold=false;s.navigation.nextMobility={};s.navigation.lastSelection={};s.battle.nextArmorAttempt={};
            s.supply.resetPlan();
            s.questState.resetPlan();s.battle.bypassedThreats.clear();
            s.questState.questRun.initialInventory=s.goals.front().kind==Impl::Kind::Quest &&
                (onlineHasCursorItem(view.world.items,view.load.playerUnitId) || view.world.storage.kind!=OnlineStorageKind::None ||
                 view.world.storage.requested!=OnlineStorageKind::None);
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
    const bool invalid=view.stage!=OnlineStage::ProtocolReady || view.gameGeneration!=questState.questLogGame ||
        view.connectionGeneration!=questState.questLogConnection || (!live() && state!="completed");
    const bool disabled=eventPolicy.questLogDurationMs==0;
    if(invalid || disabled) questState.questLogPending=false;
    if(questState.questLogCloseAt!=Clock::time_point{} && (invalid || disabled || now>=questState.questLogCloseAt)) {
        if(supplies.closeQuestLog) supplies.closeQuestLog();
        questState.questLogCloseAt={};
    }
    if(!invalid && questState.questLogPending && !blocked && (active() || state=="completed") &&
        supplies.openQuestLog && supplies.openQuestLog()) {
        questState.questLogPending=false;questState.questLogCloseAt=now+std::chrono::milliseconds(eventPolicy.questLogDurationMs);
    }
}
void DebugAi::tick(const OnlineObservation &observation,const RemoteTown &town,DebugAutoWalk &walk,IOnlineControl &control,
        IOnlineCombat &combat,IOnlineInventory &inventory,const SuppliesView &supplies,bool blocked,std::optional<Vec> position,bool moving) {
    auto &s=*impl_;
    s.tickQuestLog(observation.read(),supplies,blocked);
    if(!s.live()) return;
    const auto sampledAt=Impl::Clock::now();
    if(s.efficiencySampleAt!=Impl::Clock::time_point{} && s.efficiencyExecuting) {
        const auto elapsed=std::chrono::duration<double,std::milli>(sampledAt-s.efficiencySampleAt).count();
        s.navigationEfficiency.activeMs+=elapsed;s.navigationEfficiency.modeMs.at(s.efficiencyMode)+=elapsed;
        if(s.efficiencyWaiting) s.navigationEfficiency.navigationWaitMs+=elapsed;
    }
    s.efficiencySampleAt=sampledAt;
    s.efficiencyExecuting=s.active() && !blocked && position && town.read().nativeMapReady &&
        observation.read().stage==OnlineStage::ProtocolReady;
    const auto observed=town.navigationMemory().observedCells;
    if(observed>s.efficiencyObserved) s.navigationEfficiency.observedTiles+=observed-s.efficiencyObserved;
    s.efficiencyObserved=observed;
    const auto self=observation.read().world.units.find({0,observation.read().load.playerUnitId.value_or(0)});
    const auto discontinuity=self==observation.read().world.units.end()?0:self->second.positionDiscontinuity;
    if(position && s.efficiencyPosition && s.efficiencyArea==town.read().area) {
        if(discontinuity!=s.efficiencyDiscontinuity) ++s.navigationEfficiency.relocations;
        else if(moving) s.navigationEfficiency.walkingDistance+=(*position-*s.efficiencyPosition).length();
    }
    s.efficiencyPosition=position;s.efficiencyArea=town.read().area;s.efficiencyDiscontinuity=discontinuity;
    std::optional<OnlinePoint> local;
    if(position && position->x>=0 && position->y>=0 && position->x<=65535 && position->y<=65535) {
        const auto feet=DebugExplorationMemory::playerCell(*position);
        local=OnlinePoint{uint16_t(feet.first),uint16_t(feet.second)};
    }
    s.behaviorLog.observe(town.read().area,local,observation.read().world.playerPosition);
    if(s.observedWalkFailures!=walk.failureCount()) {
        s.observedWalkFailures=walk.failureCount();
        if(bool(s.navigation.walkIntent)) {
            const auto child=walk.observe();
            auto detail=child.reason+" moves="+std::to_string(child.moveRequests)+
                " yields="+std::to_string(child.searchContinuations);
            const auto &at=child.target;
            if(bool(at)) detail+=" target="+std::to_string(at->x)+","+std::to_string(at->y);
            s.behaviorLog.event(s.cursor,"walk-failed",detail);
        }
    }
    const auto decisions=s.decisions;
    s.phaseReported=false;
    const auto decisionStarted=Impl::Clock::now();
    tickPlan(observation,town,walk,control,combat,inventory,supplies,blocked,position,moving);
    const auto decisionMs=std::chrono::duration<double,std::milli>(Impl::Clock::now()-decisionStarted).count();
    s.efficiencyWaiting=s.phase=="navigation-worker";
    s.efficiencyMode=town.read().town?1:s.navigation.mobilityTeleport && s.navigation.mobilityTarget?2:0;
    if(!town.read().town && s.navigation.searchDiagnostics.is_object() &&
        s.navigation.searchDiagnostics.value("area",uint16_t{})==town.read().area)
        s.efficiencyMode=s.navigation.searchDiagnostics.value("mobilityClass",0)==2?2:0;
    if(s.decisions!=decisions || decisionMs>=10) {
        nlohmann::json detail{
        {"decision",s.decisions},{"goal",s.cursor},{"phase",s.phase},{"reason",s.reason},
        {"elapsedMs",decisionMs},{"blocked",s.inputBlocked},{"area",town.read().area?nlohmann::json(*town.read().area):nlohmann::json(nullptr)},
        {"actionRecoveryBusy",s.actionRecoveryBusy},{"moving",moving},{"walkActive",walk.active()},
        {"searchDecision",s.navigation.searchDiagnostics.is_object()?s.navigation.searchDiagnostics.value("decision",uint64_t{}):uint64_t{}},
        {"position",position?nlohmann::json{position->x,position->y}:nlohmann::json(nullptr)},
        {"function","DebugAi::tickPlan"},{"slowThresholdMs",20},{"runtimeSlow",decisionMs>=20}};
        if(decisionMs>=20) detail["snapshot"]={{"walkIntent",s.navigation.walkIntent},{"child",walk.snapshot()},
            {"search",s.navigation.searchDiagnostics},{"approach",s.navigation.approachDiagnostics},{"mobility",s.navigation.mobilityDiagnostics},
            {"travel",s.navigation.travelDiagnostics},{"routeExecution",s.routeExecutionSnapshot()},{"combat",s.battle.combatDiagnostics}};
        walk.navigationSearch().traceDecision(std::move(detail));
    }
    s.tickQuestLog(observation.read(),supplies,blocked);
    if(!s.active()) return;
    {
        const Vec at=position.value_or(Vec{});
        Impl::Context c{observation,town,walk,control,combat,inventory,at,moving,&supplies};
        s.observeDiagnostics(c);
        if(position && !s.inputBlocked) s.observeTerrain(c);
    }
    if(s.inputBlocked) {
        const auto why=observation.read().stage!=OnlineStage::ProtocolReady?"Protocol not ready":
            blocked?"Frontend input suspended":!position?"Local position unavailable":"Area or reconstructed map unavailable";
        s.behaviorLog.phase(s.cursor,"input-blocked",why);
    }
    else if(s.decisions!=decisions) {
        if(s.phaseReported) s.behaviorLog.phase(s.cursor,s.phase,s.reason);
        else s.behaviorLog.endPhase();
    }
}
} // namespace d2x
