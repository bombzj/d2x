#include "npc_content.hpp"
#include "content/monsters/monster_catalog.hpp"
#include "content/monsters/monster_ai_data.hpp"
#include "content/npc/vendor_stock.hpp"
#include "content/monsters/monster_difficulty_combat.hpp"
#include "content/monsters/monster_animation.hpp"
#include "resources/anim_data.hpp"
#include "content/world/world_catalog.hpp"
#include "monster_actions_content.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "content/string_table.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>
namespace d2x {
namespace {
std::string speechKey(std::string_view value) {
    std::string key="key:";
    for(const unsigned char c:value) if(std::isalnum(c)) key+=char(std::tolower(c));
    return key;
}
using SpeechIndexes=std::map<std::string,uint16_t,std::less<>>;
server::NpcRule prepareNpcRule(const ClassicData &data,const SpeechIndexes &indexes,const std::set<uint16_t> &callbacks,const MonsterRecord &record,std::string_view name,int act) {
    const auto message = [&](const NpcSpeech *speech) -> std::optional<uint16_t> {
        if (!speech) return {};
        // The language-independent TBL key resolved at load time is the same in
        // every language, so it localizes introductions and acknowledgements.
        if(!speech->stringKey.empty()) {
            if(const auto keyed=indexes.find(speechKey(speech->stringKey));keyed!=indexes.end()) return keyed->second;
        }
        // Authored names such as MeshifAct3 differ from Sounds' speaker Meshif.
        // Accept only a verified TBL key or the same MPQ text after wrapping.
        if(!speech->quest.empty()) {
            if(const auto keyed=indexes.find(speechKey(speech->quest+speech->state+speech->speaker));keyed!=indexes.end()) return keyed->second;
            // A5 Tyrael's authored wave has no matching Sounds row. Its
            // original TBL key uses the MPQ NPC name instead of a voice ID.
            if(const auto keyed=indexes.find(speechKey(speech->quest+speech->state+std::string(name)));keyed!=indexes.end()) return keyed->second;
        }
        const auto found=indexes.find(npcSpeechText(speech->text));
        return found==indexes.end()?std::nullopt:std::optional<uint16_t>{found->second};
    };
    server::NpcRule rule; rule.code = record.id; rule.nativeClass = record.index; rule.size = record.collisionSize;
    rule.interactable=record.interact;rule.walkVelocity=record.walkVelocity.value_or(0);rule.movement=record.movementRule();
    
    rule.vendor = data.vendors.contains(rule.code); rule.repair = npcCanRepair(rule.code);
    rule.identify = npcCanIdentify(rule.code); rule.heal = npcCanHeal(rule.code);
    rule.gamble = npcCanGamble(rule.code);
    rule.introduction = npcIntroductionKey(name, act);
    // Callback identities are original rules; every offered text is validated
    // against the current MPQ rather than supplied by the authority.
    for(const auto &definition:questDefinitions) {
        const auto &key=data.questContent.at(questIndex(definition.id)).speechKey;
        for(const auto &[group,speeches]:data.npcDialogues) {
            (void)group;
            for(const auto &speech:speeches) if(speech.act==act && speech.quest==key && !rule.questSpeeches.contains(std::pair{definition.id,speech.state}))
                if(const auto text=message(questSpeech(data.npcDialogues,key,speech.state,name))) {
                    rule.questMessages.insert(*text);
                    rule.questSpeeches.emplace(std::pair{definition.id,speech.state},*text);
                }
        }
    }
    // A1/A2 acknowledgements also include messages outside journal groups.
    rule.questMessages.insert(callbacks.begin(),callbacks.end());
    for (const auto &character : data.characters) if (const auto text = message(introSpeech(data.npcDialogues, name, character.name, act))) rule.introductions.emplace(character.name, *text);
    return rule;
}
}
std::shared_ptr<const server::MonsterRule> prepareNeutralMonsterRule(Archives &archives,const ClassicData &data,
    AreaGenerationRequest request,const MonsterRecord &record) {
    if(record.hostile()) throw std::invalid_argument("Hostile class passed to neutral rule preparation");
    const auto world=sharedWorldCatalog(archives,request.difficulty);
    const auto animations=archives.prepared<AnimDataTable>([&]{return AnimDataTable(archives.read("data/global/animdata.d2"));});
    const auto profile=loadMonsterCombatProfile(data.tables.at("monstats"),record.sourceRow,data.tables.at("monlvl"),
        request.difficulty,world->level(request.level).population.level.at(size_t(request.difficulty)),{},request.ladderMonsterStats,MonsterStatPurpose::NativeNpc);
    if(!profile || !profile->defense) throw std::runtime_error("Missing original NPC stat profile: "+record.id);
    auto rule=std::make_shared<server::MonsterRule>();
    if(record.ai=="BaalToStairs" || record.ai=="BaalThrone" || record.ai=="BaalTaunt" || record.ai=="HellMeteor" || record.ai=="CatapultSpotter") {
        const auto ai=loadMonsterAiProfile(data.tables.at("monstats"),record.sourceRow,record.ai,request.difficulty);
        if(!ai) throw std::runtime_error("Missing original scripted NPC AI: "+record.id);
        rule->ai=*ai;
    }
    rule->stats=data.nativeStats;rule->formulas=data.nativeFormulas;rule->auraPrograms=data.nativeAuraPrograms;
    rule->nativeClass=record.index;rule->level=profile->level;rule->difficulty=request.difficulty;rule->statGameType=request.ladderMonsterStats;
    rule->minimumLife=profile->damage.minLife;rule->maximumLife=profile->damage.maxLife;
    rule->defense=*profile->defense;rule->attackRating=profile->attack1Rating.value_or(0);rule->resistances=profile->resistances;
    if(profile->damage.attack1Damage) {rule->minimumDamage=profile->damage.attack1Damage->first;rule->maximumDamage=profile->damage.attack1Damage->second;}
    rule->damageRegen=profile->damageRegen;rule->criticalChance=profile->criticalChance;
    rule->size=record.collisionSize;rule->meleeRange=record.meleeRange;
    rule->nativeVelocity=record.walkVelocity.value_or(0);rule->collision=record.movementRule();rule->spawnCollision=record.spawnRule();
    rule->demon=record.demon;rule->undead=record.undead;rule->boss=record.boss;rule->switchAi=record.switchAi;
    rule->noTreasure=true;rule->noExperience=true;
    rule->corpseSelectable=record.corpseSelectable;rule->corpseSize=record.corpseSize;rule->softCorpse=record.soft;
    rule->coldEffect=record.coldEffect.at(size_t(request.difficulty));
    rule->hitStates={data.states.at("cold").definition,data.states.at("poison").definition,{}};
    rule->stunState=data.states.at("stunned").definition.id;rule->openWoundsState=data.states.at("openwounds").definition.id;
    rule->preventHealState=data.states.at("preventheal").definition.id;rule->corpseNoSelectState=data.states.at("corpse_noselect").definition.id;
    rule->noManaRegenState=data.states.at("nomanaregen").definition.id;
    const auto suffix=request.difficulty==0?"":request.difficulty==1?"(N)":"(H)";
    rule->blockChance=data.tables.at("monstats").number(record.sourceRow,std::string("ToBlock")+suffix).value_or(0);
    rule->drain=data.tables.at("monstats").number(record.sourceRow,std::string("Drain")+suffix).value_or(0);
    rule->blockWithoutShield=data.tables.at("monstats").number(record.sourceRow,"NoShldBlock").value_or(0)!=0;
    rule->threat=data.tables.at("monstats").number(record.sourceRow,"threat").value_or(0);
    if(!prepareMonsterLifecycle(archives,data,*animations,record,*rule)) throw std::runtime_error("Missing original NPC lifecycle: "+record.id);
    if(record.ai=="BaalToStairs" || record.ai=="BaalThrone" || record.ai=="BaalTaunt" || record.ai=="HellMeteor" || record.ai=="CatapultSpotter") rule->decisionTicks=rule->neutralDelayTicks;
    if((record.ai=="BaalThrone" || record.ai=="BaalTaunt" || record.ai=="HellMeteor" || record.ai=="CatapultSpotter") && !prepareAct2MonsterSkills(archives,data,*animations,record,*rule))
        throw std::runtime_error("Missing original scripted monster skills");
    const auto &extra=data.tables.at("monstats2");
    for(size_t row=0;row<extra.rows().size();++row) if(extra.value(row,"Id")==data.tables.at("monstats").value(record.sourceRow,"MonStatsEx") && extra.number(row,"mDT").value_or(0)) {
        const auto death=loadMonsterMotionTiming(*animations,record.token,"dt",monsterModeWeapon(archives,record.token,"dt",record.baseWeapon));
        if(!death) throw std::runtime_error("Missing original NPC death animation: "+record.id);
        rule->deathTicks=monsterAnimationTicks({death->frames,death->speed});
    }
    return rule;
}
void prepareNpcs(Archives &archives, const ClassicData &data, PreparedWorldArea &prepared) {
    const auto catalogSource = archives.prepared<MonsterCatalog>([&] { return MonsterCatalog(archives, data.tables.at("monstats")); });
    const auto &catalog = *catalogSource; ClassicStrings strings(archives);
    SpeechIndexes speechIndexes;
    for(const auto &[key,value]:strings.entries()) {
        (void)value;const auto index=strings.index(key);
        if(index>=0 && index<=UINT16_MAX) if(const auto text=strings.speech(index);!text.empty()) {
            speechIndexes.try_emplace(speechKey(key),uint16_t(index));
            speechIndexes.try_emplace(npcSpeechText(text),uint16_t(index));
        }
    }
    std::set<uint16_t> callbacks;
    for(int index=64;index<=452;++index) if(!strings.speech(index).empty()) callbacks.insert(uint16_t(index));
    auto &area = prepared.authority;
    if(int(area.id)==136) return;
    std::map<int,std::shared_ptr<const server::MonsterRule>> combatRules;
    const auto npcRule=[&](const MonsterRecord &record) {
        const auto name=strings.find(record.name);
        auto result=prepareNpcRule(data,speechIndexes,callbacks,record,name.empty()?std::string_view(record.name):name,area.act);
        const auto hireling=std::find_if(data.hirelings.begin(),data.hirelings.end(),[&](const auto &entry) {
            return entry.seller==record.index && entry.difficulty==1;
        });
        if(hireling!=data.hirelings.end()) {
            const int first=strings.index(hireling->nameFirst),last=strings.index(hireling->nameLast);
            if(first<0 || last<first || last>UINT16_MAX || last-first>=69)
                throw std::runtime_error("Invalid original hireling name range");
            for(int index=first;index<=last;++index) result.hirelingNames.push_back(uint16_t(index));
        }
        auto [entry,fresh]=combatRules.try_emplace(record.index);
        if(fresh) entry->second=prepareNeutralMonsterRule(archives,data,prepared.terrain.request,record);
        result.combat=entry->second;return result;
    };
    if(area.act==4) {
        const auto *stairs=catalog.find("baalcrabstairs");
        if(!stairs || stairs->hostile()) throw std::runtime_error("Missing original Baal stairs class");
        area.questNpcs.emplace(stairs->id,npcRule(*stairs));
    }
    std::vector<Vec> prisonerMarkers;
    for(const auto *code:{"izualghost","drehya","tyrael3"}) {
        const bool wanted=(area.act==3 && std::string_view(code)=="izualghost") || (area.act==4 && std::string_view(code)!="izualghost");
        const auto *record=catalog.find(code);
        if(wanted && record && record->interact && !record->hostile()) area.questNpcs.emplace(code,npcRule(*record));
    }
    for (size_t sourceIndex=0;sourceIndex<prepared.terrain.map->terrain.data.objects.size();++sourceIndex) {
        const auto &source=prepared.terrain.map->terrain.data.objects[sourceIndex];
        if(source.flags&1u) continue;
        if(int(area.id)==111 && source.type==2 && source.nativeIdentity && source.id>=0) {
            const auto &objects=data.tables.at("objects");bool cage=false;
            for(size_t row=0;row<objects.rows().size();++row) if(objects.number(row,"Id")==source.id && objects.number(row,"InitFn")==62) cage=true;
            const auto *prisoner=catalog.find("act5pow");
            const Vec marker{float(source.x),float(source.y)};
            const bool duplicate=std::any_of(prisonerMarkers.begin(),prisonerMarkers.end(),[&](Vec at){return (at-marker).length()<1;});
            if(cage && !duplicate) {
                if(!prisoner || prisoner->hostile() || prisoner->walkVelocity.value_or(0)<=0)
                    throw std::runtime_error("Missing original caged prisoner rule");
                prisonerMarkers.push_back(marker);
                auto rule=npcRule(*prisoner);rule.questInitFunction=62;
                // Prepare identities only. Init62 performs its 25 birth
                // attempts and three-group limit in room activation order.
                for(int slot=0;slot<5;++slot)
                    area.npcs.push_back({{},marker,rule,sourceIndex,marker,source.id});
            }
        }
        const MonsterRecord *record = nullptr;
        int questInit=0;
        if (source.type == 1) {
            const auto preset = catalog.preset(area.act, source.id, prepared.terrain.map->terrain.data.version, source.nativeIdentity);
            record = preset.nativeClass>=0?catalog.find(preset.nativeClass):catalog.find(preset.id);
        } else if (source.type == 2 && source.nativeIdentity) {
            const auto &objects = data.tables.at("objects");
            for (size_t row = 0; row < objects.rows().size(); ++row)
                if (objects.number(row,"Id") == source.id) {
                    const auto init=objects.number(row,"InitFn").value_or(0);
                    if(int(area.id)==1 && init==54) {record=catalog.find("cain5");questInit=init;}
                    if(int(area.id)==75 && (init==49 || init==50)) {record=catalog.find("hratli");questInit=init;}
                    if(int(area.id)==40 && (init==18 || init==19)) {record=catalog.find("jerhyn");questInit=init;}
                    if(int(area.id)==109 && init==68) {record=catalog.find("nihlathak");questInit=init;}
                    if(int(area.id)==109 && init==71) {record=catalog.find("larzuk");questInit=init;}
                    if(init==60) {record=catalog.find("compellingorb");questInit=init;}
                }
        }
        const bool townPreset=area.town && source.type==1 && record && !record->critter;
        if (!record || record->hostile() || (!townPreset && (!record->npc || !record->interact) &&
            record->id!="baalthrone" && record->id!="compellingorb")) continue;
        auto rule=npcRule(*record);
        rule.questInitFunction=questInit;
        const Vec marker{float(source.x)+(source.type==1?.5f:0.f),float(source.y)+(source.type==1?.5f:0.f)};
        // Authored monsters are placed by the server's native retry search;
        // object callbacks retain the actual marker coordinate.
        area.npcs.push_back({{}, marker, std::move(rule),sourceIndex,
            source.type==2?std::optional<Vec>{Vec{float(source.x),float(source.y)}}:std::nullopt,
            source.type==2?source.id:-1});
    }
    // InitFunction60 is an object callback. It creates a distinct native
    // UNIT_MONSTER even when DS1's object identity came through a preset alias.
    // Register that real carrier once; never use the operated object as owner.
    for(const auto &object:area.objects) if(object.rule.initialization==60 &&
        std::none_of(area.npcs.begin(),area.npcs.end(),[](const auto &npc){return npc.rule.code=="compellingorb";})) {
        const auto *record=catalog.find("compellingorb");
        if(!record || record->hostile()) throw std::runtime_error("Missing original Compelling Orb carrier");
        auto rule=npcRule(*record);rule.questInitFunction=60;
        area.npcs.push_back({{},object.position,std::move(rule),{},object.position,object.type});
    }
}
}
