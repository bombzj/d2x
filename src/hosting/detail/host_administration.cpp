#include "native_realm_service.hpp"
#include "hosting/combat_content.hpp"
#include "hosting/item_content.hpp"
#include "hosting/character_content.hpp"
#include "content/items/item_magic_loot.hpp"
#include "content/items/item_quality.hpp"
#include "content/items/item_grades.hpp"
#include "content/items/item_properties.hpp"
#include "persistence/d2s_inventory.hpp"
#include "gameplay/loot/special.hpp"
#include "core/random.hpp"
#include "gameplay/character/respec.hpp"
#include "content/npc/hireling_data.hpp"
#include <charconv>
#include <algorithm>

namespace d2x::hosting {
HostDiagnostics NativeRealmService::diagnostics() const {
    auto result = counters;
    result.player = binding;
    if (binding) if (const auto view = host.read(*binding)) {
        result.tick = view->tick; result.paused = view->paused;
        result.command = view->command;
        result.systemSteps = host.systemSteps(binding->game);
        result.fixedSteps = host.stepProgress(binding->game);
    }
    switch (peer.phase) {
    case GamePhase::Connected: result.phase = "connected"; break;
    case GamePhase::LoggedOn: result.phase = "logged-on"; break;
    case GamePhase::Entered: result.phase = "entered"; break;
    case GamePhase::Closed: result.phase = "closed"; break;
    }
    return result;
}
AdminResult NativeRealmService::administer(const AdminRequest &request) {
    if (!binding || binding->game != request.target.game || binding->player != request.target.player)
        return {AdminStatus::InvalidTarget, "The host game/player generation has changed"};
    const auto commands = adminCommands();
    const auto command = std::find_if(commands.begin(), commands.end(), [&](const auto &c) { return c.operation == request.operation; });
    if (command == commands.end()) return {AdminStatus::InvalidArguments, "Unknown host operation"};
    const bool valid = [&] {
        switch (command->arguments) {
        case AdminArgumentKind::None: return std::holds_alternative<std::monostate>(request.arguments);
        case AdminArgumentKind::Amount: return std::holds_alternative<AdminAmount>(request.arguments);
        case AdminArgumentKind::Step: return std::holds_alternative<AdminStep>(request.arguments);
        case AdminArgumentKind::Spawn: return std::holds_alternative<AdminSpawn>(request.arguments);
        case AdminArgumentKind::Unit: return std::holds_alternative<AdminUnit>(request.arguments);
        case AdminArgumentKind::Travel: return std::holds_alternative<AdminTravel>(request.arguments);
        case AdminArgumentKind::Missile: return std::holds_alternative<AdminMissile>(request.arguments);
        case AdminArgumentKind::Waypoints: return std::holds_alternative<AdminWaypoints>(request.arguments);
        }
        return false;
    }();
    if (!valid) return {AdminStatus::InvalidArguments, "Wrong argument type for host operation"};
    if (reload && request.operation != AdminOperation::CancelReload)
        return {AdminStatus::Unavailable, "A prepared reload must finish or be cancelled"};
    if(const auto steps=host.stepProgress(binding->game);steps && steps->pending() &&
       request.operation!=AdminOperation::CancelStep && request.operation!=AdminOperation::Pause &&
       request.operation!=AdminOperation::Resume && request.operation!=AdminOperation::AutoPause)
        return {AdminStatus::Unavailable,"A fixed-step job is still pending; observe its progress or cancel its sequence before another mutation"};
    try {
        switch (request.operation) {
        case AdminOperation::Save:
            checkpoint(); return {AdminStatus::Applied, "Character saved"};
        case AdminOperation::Reload:
            prepareReload(); return {AdminStatus::Applied, "Reload prepared; native leave and rejoin required"};
        case AdminOperation::CancelReload:
            discardReload(); return {AdminStatus::Applied, "Prepared reload cancelled"};
        case AdminOperation::Step: {
            const auto frames = std::get<AdminStep>(request.arguments).frames;
            if (!frames || frames > 250) return {AdminStatus::InvalidArguments, "Step requires 1-250 frames"};
            if (peer.phase != GamePhase::Entered || !host.step(*binding, frames))
                return {AdminStatus::Unavailable, "Pause the entered host game before stepping"};
            return {AdminStatus::Applied, "Queued " + std::to_string(frames) + " fixed frames; completion is reported by server.fixedSteps"};
        }
        case AdminOperation::CancelStep: {
            const auto sequence=std::get<AdminAmount>(request.arguments).value;
            if(sequence<=0) return {AdminStatus::InvalidArguments,"Supply the fixed-step sequence as amount"};
            if(!host.cancelStep(binding->game,uint64_t(sequence))) return {AdminStatus::InvalidTarget,"Fixed-step sequence has expired"};
            return {AdminStatus::Applied,"Fixed-step job cancelled; completed frames are retained and the game stays paused"};
        }
        // Each privileged mutation has an explicit slot. Implement inside the
        // corresponding server domain, then project native packets; never edit
        // a client snapshot or encode an invented success acknowledgement.
        case AdminOperation::Pause:
        case AdminOperation::Resume:
        case AdminOperation::AutoPause: {
            if (peer.phase != GamePhase::Entered) return {AdminStatus::Unavailable, "Enter the host game before controlling its clock"};
            const auto pause = request.operation == AdminOperation::AutoPause ? std::optional<bool>{} :
                std::optional<bool>{request.operation == AdminOperation::Pause};
            if (!host.debugPause(binding->game, pause)) return {AdminStatus::InvalidTarget, "Game instance expired"};
            return {AdminStatus::Applied, "Instance debug clock policy updated; all participants share this clock"};
        }
        case AdminOperation::RestoreResources: {
            const auto result = host.restoreResources(*binding);
            if (!result) return {AdminStatus::Unavailable, "Resource restoration requires a living entered player and available transaction capacity"};
            publishEvents();
            return {AdminStatus::Applied, "Life, mana and stamina restored through the character transaction"};
        }
        case AdminOperation::MissileHit: {
            const auto &hit=std::get<AdminMissile>(request.arguments);const auto &table=content->tables.at("missiles");
            std::optional<size_t> row;for(size_t i=0;i<table.rows().size();++i) if(table.number(i,"Id")==hit.missile) {row=i;break;}
            if(!row || !hit.source || !hit.amount) return {AdminStatus::InvalidArguments,"Supply a current monster source, positive amount and MPQ missile Id"};
            const auto element=table.value(*row,"EType");DamageType type;
            if(element.empty()) type=DamageType::Physical;else if(element=="fire") type=DamageType::Fire;
            else if(element=="ltng") type=DamageType::Lightning;else if(element=="cold") type=DamageType::Cold;
            else if(element=="mag") type=DamageType::Magic;else return {AdminStatus::InvalidArguments,"This missile's damage channel is not supported by the diagnostic impact"};
            const auto result=host.missileHit(*binding,EntityId{hit.source},hit.amount,type,table.number(*row,"ReturnFire").value_or(0)!=0);
            if(!result) return {AdminStatus::Unavailable,"Authority rejected this missile impact"};
            publishEvents();
            return {AdminStatus::Applied,"MPQ missile impact applied through shield, mitigation and reaction hooks"};
        }
        case AdminOperation::GrantGold:
        case AdminOperation::DamagePlayer: {
            const auto amount=std::get<AdminAmount>(request.arguments).value;
            if(amount<=0 || amount>UINT32_MAX) return {AdminStatus::InvalidArguments,"Amount must be 1..UINT32_MAX"};
            const auto result=request.operation==AdminOperation::GrantGold ? host.grantGold(*binding,uint32_t(amount)) : host.damagePlayer(*binding,uint32_t(amount));
            if(!result) return {AdminStatus::Unavailable,"Authority rejected the resource mutation"};
            publishEvents(); return {AdminStatus::Applied,"Authority resource transaction committed"};
        }
        case AdminOperation::SetPlayers: {
            const auto count=std::get<AdminAmount>(request.arguments).value;
            if(count<1 || count>8) return {AdminStatus::InvalidArguments,"Players must be 1-8"};
            if(peer.phase!=GamePhase::Entered) return {AdminStatus::Unavailable,"Enter the host game first"};
            const auto result=host.setVirtualPlayers(*binding,unsigned(count));
            if(!result) return {AdminStatus::Unavailable,"Only the entered primary host can set virtual players; retry if its event queue is full"};
            publishEvents();return {AdminStatus::Applied,"Virtual player count changed"};
        }
        case AdminOperation::GrantExperience: {
            if (peer.phase != GamePhase::Entered) return {AdminStatus::Unavailable, "Enter the host game before granting experience"};
            const auto amount = std::get<AdminAmount>(request.arguments).value;
            if (amount <= 0) return {AdminStatus::InvalidArguments, "Experience amount must be positive"};
            const auto result = host.grantExperience(*binding, uint64_t(amount));
            if (!result) return {AdminStatus::Unavailable, "Progression rejected the experience grant (dead player, level cap or unavailable capacity/rules)"};
            publishEvents(); publishPlayers(); publishMonsters(); publishMissiles(); publishGroundItems(); publishCorpses(); publishObjects(); publishNpcs(); publishShop(); publishPortals(); publishItemSkills(); publishMotion();
            return {AdminStatus::Applied, "Experience granted by the authority"};
        }
        case AdminOperation::SpawnItem: {
            const auto &spawn = std::get<AdminSpawn>(request.arguments);
            if (peer.phase != GamePhase::Entered || spawn.level < 1 || spawn.level > 99 || !content->items.find(spawn.code)) return {AdminStatus::InvalidArguments, "Enter a game and supply an MPQ item code and level 1..99"};
            if(spawn.propertyQuery) {
                if(spawn.propertyQuery->size()>64 || !spawn.properties.empty() || !spawn.affixes.empty() || spawn.sockets || spawn.ethereal || spawn.durability || spawn.position || (spawn.quality!="magic" && spawn.quality!="rare"))
                    return {AdminStatus::InvalidArguments,"propertyQuery requires magic/rare quality and no generation overrides"};
                const auto &base=*content->items.find(spawn.code);
                const auto rules=loadItemQualityRules(*content,content->tables.at("itemratio"),spawn.code);
                if(rules.normalOnly || rules.uniqueOnly || base.questTag || (spawn.quality=="rare" && !rules.rareAllowed))
                    return {AdminStatus::InvalidArguments,"This base cannot have the requested affix quality"};
                const int magic=base.base.magicLevel.value_or(0),level=itemAffixLevel(spawn.level,base.base.level.value_or(0),magic);
                AdminResult result{AdminStatus::Applied,"Eligible original affixes; query only, no item created"};
                result.propertyCandidates.emplace();
                for(bool prefix:{true,false}) for(const auto &record:prefix?content->magicPrefixes:content->magicSuffixes) {
                    if(!spawn.propertyQuery->empty() && std::none_of(record.properties.begin(),record.properties.end(),[&](const auto &property){return property.code.find(*spawn.propertyQuery)!=std::string::npos;})) continue;
                    const auto eligible=rollMagicAffix(std::span(&record,1),base.equipment.types,base.equipment.requiredClass,level,magic,
                        spawn.quality=="rare",base.base.sockets.value_or(0)>0,{},true,initialRandom(0),int(record.row)+1);
                    if(!eligible.row) continue;
                    AdminPropertyCandidate candidate{prefix,int(record.row),record.group,record.requiredLevel,record.name,{}};
                    for(const auto &property:record.properties) candidate.properties.push_back({property.code,property.parameter,property.minimum.value_or(0),property.maximum.value_or(0),property.directRoll});
                    result.propertyCandidates->push_back(std::move(candidate));
                }
                return result;
            }
            const auto saved=host.exportCharacter(*binding); if(!saved) return {AdminStatus::Unavailable,"Character settlement is pending"};
            auto character = *saved; character.inventory.items.clear();
            auto random = initialRandom(spawn.seed.value_or(uint32_t(host.nextEntity(binding->game))));
            const auto commandSeed=uint32_t(random);
            if(!spawn.properties.empty() && (!spawn.affixes.empty() || (spawn.quality!="magic" && spawn.quality!="rare")))
                return {AdminStatus::InvalidArguments,"properties requires magic or rare quality and cannot be combined with affixes"};
            if(!spawn.affixes.empty() && spawn.quality!="magic" && spawn.quality!="rare")
                return {AdminStatus::InvalidArguments,"Explicit affixes require magic or rare quality"};
            ItemGeneration generation;
            if(!spawn.properties.empty()) {
                auto rolled=generatePropertyItem(*content,*content->items.find(spawn.code),spawn.quality=="magic"?ItemQuality::Magic:ItemQuality::Rare,spawn.level,random,spawn.properties);
                generation=std::move(rolled.generation);random=rolled.randomState;
            } else if (spawn.quality == "magic" || spawn.quality=="rare") {
                const auto quality=spawn.quality=="magic"?ItemQuality::Magic:ItemQuality::Rare;
                auto rolled = rollAffixItem(*content, *content->items.find(spawn.code), quality, spawn.level, random, {});
                if (!rolled.deferred.empty() || rolled.generation.quality != quality)
                    return {AdminStatus::Unavailable, "MPQ cannot prepare this affix item: " + rolled.deferred};
                generation = std::move(rolled.generation); random = rolled.randomState;
            } else if(spawn.quality=="superior" || spawn.quality=="inferior") {
                auto roll=rollItemGrade(*content,*content->items.find(spawn.code),spawn.quality=="superior"?ItemQuality::Superior:ItemQuality::Inferior,random);
                if(!roll.deferred.empty()) return {AdminStatus::Unavailable,roll.deferred};
                generation=std::move(roll.generation);random=roll.randomState;
            } else if(spawn.quality=="unique" || spawn.quality=="set") {
                const bool unique=spawn.quality=="unique";
                const auto plan=planSelectedItem(*content,spawn.code,spawn.level,random,host.usedUniques(binding->game),{},unique?DropQuality::Unique:DropQuality::Set);
                if(!plan.deferred.empty() || plan.drops.size()!=1 || plan.drops.front().generation.quality!=(unique?ItemQuality::Unique:ItemQuality::Set)) return {AdminStatus::Unavailable,"No eligible original special item: "+plan.deferred};
                generation=plan.drops.front().generation;random=plan.randomState;
            } else if (spawn.quality != "normal") return {AdminStatus::InvalidArguments, "Unknown generated quality"};
            if(!spawn.affixes.empty()) {
                if(spawn.affixes.size()>6) return {AdminStatus::InvalidArguments,"At most six affixes are supported"};
                const auto &base=*content->items.find(spawn.code);
                const int magicLevel=base.base.magicLevel.value_or(0);
                const int affixLevel=itemAffixLevel(spawn.level,base.base.level.value_or(0),magicLevel);
                std::vector<int> groups;
                unsigned prefixes=0,suffixes=0;
                generation.affixes.clear();generation.requiredLevel=0;
                for(const auto &requested:spawn.affixes) {
                    auto &count=requested.prefix?prefixes:suffixes;
                    if(++count>(spawn.quality=="magic"?1u:3u) || requested.row<0 || requested.row>=INT32_MAX)
                        return {AdminStatus::InvalidArguments,"Invalid affix row or too many prefixes/suffixes"};
                    const auto &records=requested.prefix?content->magicPrefixes:content->magicSuffixes;
                    auto choice=rollMagicAffix(records,base.equipment.types,base.equipment.requiredClass,
                        affixLevel,magicLevel,spawn.quality=="rare",base.base.sockets.value_or(0)>0,
                        groups,true,random,requested.row+1);
                    if(!choice.row || *choice.row!=size_t(requested.row))
                        return {AdminStatus::InvalidArguments,"Affix is unavailable for this base, level, quality or group"};
                    random=choice.randomState;
                    const auto record=std::find_if(records.begin(),records.end(),[&](const auto &entry){return entry.row==*choice.row;});
                    auto rolled=rollItemProperties(*content,record->properties,random,spawn.level);random=rolled.randomState;
                    if(requested.rolls) {
                        if(requested.rolls->size()!=record->properties.size())
                            return {AdminStatus::InvalidArguments,"rolls must match the original affix property count"};
                        for(size_t index=0;index<record->properties.size();++index) {
                            const auto &property=record->properties[index];const auto value=requested.rolls->at(index);
                            if(property.directRoll && property.minimum && property.maximum) {
                                if(value<std::min(*property.minimum,*property.maximum) || value>std::max(*property.minimum,*property.maximum))
                                    return {AdminStatus::InvalidArguments,"Affix value is outside its original MPQ range"};
                                std::fill(rolled.operations[index].begin(),rolled.operations[index].end(),value);
                            } else if(value!=property.minimum.value_or(0))
                                return {AdminStatus::InvalidArguments,"This property uses fixed original parameters, not a direct value roll"};
                        }
                        rolled.values=*requested.rolls;
                    }
                    generation.affixes.push_back({requested.prefix,requested.row,std::move(rolled.values),std::move(rolled.operations)});
                    generation.requiredLevel=std::max(generation.requiredLevel,record->requiredLevel);
                    groups.push_back(record->group);
                }
                generation.propertyRandom=random;
            }
            AdminItemDescription description;
            description.code=spawn.code;description.quality=spawn.quality;description.seed=commandSeed;description.preview=spawn.preview;
            for(const auto &affix:generation.affixes) {
                const auto &records=affix.prefix?content->magicPrefixes:content->magicSuffixes;
                const auto record=std::find_if(records.begin(),records.end(),[&](const auto &entry){return int32_t(entry.row)==affix.row;});
                if(record==records.end()) return {AdminStatus::InvalidArguments,"Generated affix has no original record"};
                description.affixes.push_back({affix.prefix,affix.row,record->name,affix.propertyRolls});
            }
            generation.forceEthereal=spawn.ethereal;
            ItemInstance item;
            try {
                item=prepareItem(*content, {spawn.code, 1, {}, unsigned(spawn.level), std::move(generation)}, random, character.difficulty);
            } catch(const std::exception &error) {
                return {AdminStatus::InvalidArguments,std::string("Generated item was rejected: ")+error.what()};
            }
            if (spawn.durability) {
                auto identified=item;identified.identified=true;
                const auto maximum=itemMaximumDurability(*content,identified,resolveItemStats(*content,identified,int(item.level)));
                if (!maximum || *spawn.durability > maximum) return {AdminStatus::InvalidArguments, "Durability requires a durable item and cannot exceed its prepared MPQ maximum"};
                item.durability = *spawn.durability;
            }
            if(spawn.sockets) {
                const auto &base=*content->items.find(spawn.code);
                unsigned cap=unsigned(std::max(0,std::min({base.base.sockets.value_or(0),base.base.socketsByLevel[item.level<=25?0:item.level<=40?1:2],base.width*base.height,6})));
                if(item.quality==ItemQuality::Magic) cap=std::min(cap,3u);
                else if(item.quality==ItemQuality::Rare || item.quality==ItemQuality::Unique || item.quality==ItemQuality::Set || item.quality==ItemQuality::Crafted) cap=std::min(cap,1u);
                if(*spawn.sockets>cap || item.sockets) return {AdminStatus::InvalidArguments,"Sockets exceed the original base/level/quality limit or item already has sockets"};
                item.sockets=*spawn.sockets;if(item.sockets) item.nativeFlags|=0x800u;
            }
            item.id = EntityId{1}; item.location = ContainerLocation{character.containers.backpack, {}};
            character.inventory.items.emplace(item.id, item);
            try {
                const auto encoded=writeD2sItem(exportD2sItem(character,item,*content),*content);
                (void)readD2sItem(encoded,*content);
            } catch(const std::exception &error) {
                return {AdminStatus::InvalidArguments,std::string("Generated item failed native save validation: ")+error.what()};
            }
            description.itemSeed=item.nativeSeed;description.level=int(item.level);description.requiredLevel=item.requiredLevel;
            description.defense=item.defense;description.durability=item.durability;description.sockets=item.sockets;description.ethereal=(item.nativeFlags&0x400000u)!=0;
            auto identified=item;identified.identified=true;
            description.maximumDurability=itemMaximumDurability(*content,identified,resolveItemStats(*content,identified,int(item.level)));
            for(const auto &stat:item.savedStats) {
                const auto definition=std::find_if(content->itemStats.begin(),content->itemStats.end(),[&](const auto &entry){return entry.id==stat.id;});
                if(definition==content->itemStats.end()) return {AdminStatus::InvalidArguments,"Unknown generated native stat"};
                description.stats.push_back({definition->name,stat.id,stat.parameter,stat.value});
            }
            server::items::PreparedBatch batch{{item}, prepareEquipmentRules(*content, character), {}};
            if(spawn.preview) return {AdminStatus::Applied,"Item validated; preview only, no item spawned or unique registered",{},std::move(description)};
            if(item.quality==ItemQuality::Unique && item.specialRow>=0 && !content->tables.at("uniqueitems").number(size_t(item.specialRow),"nolimit").value_or(0)) batch.limitedUniques.insert(size_t(item.specialRow));
            const auto view = host.read(*binding);
            const auto origin = shared.terrain.at(binding->game).at(view->actor.region).origin;
            const auto result = host.spawnItems(*binding, std::move(batch), spawn.position ? std::optional<Vec>{*spawn.position - origin} : std::nullopt);
            if (!result) return {AdminStatus::Unavailable, "Item placement rejected"};
            publishGroundItems(); return {AdminStatus::Applied, "MPQ item materialized through ground item authority",{},std::move(description)};
        }
        case AdminOperation::SpawnMonster: {
            if (peer.phase != GamePhase::Entered) return {AdminStatus::Unavailable, "Enter the host game before spawning a monster"};
            const auto &spawn = std::get<AdminSpawn>(request.arguments);
            if (!spawn.position) return {AdminStatus::InvalidArguments, "Monster spawn requires global x and y; level is selected from the current MPQ area"};
            const auto view = host.read(*binding);
            const auto &source = shared.terrain.at(binding->game).at(view->actor.region);
            MonsterIdentity identity;
            identity.monster = spawn.code; identity.origin = SpawnOrigin::Debug;identity.rank=spawn.rank;identity.superUnique=spawn.superUnique;
            identity.spawnKey = "debug/" + std::to_string(host.nextEntity(binding->game));
            auto monster = prepareCombatMonster(archives, *content, source.request, std::move(identity), *spawn.position - source.origin);
            if (!monster) return {AdminStatus::Unavailable, "Monster code has no supported MPQ combat profile"};
            const auto result = host.spawnMonster(*binding, *monster);
            if (!result) return {AdminStatus::Unavailable, "Monster spawn rejected by area collision, lifecycle or capacity"};
            publishMonsters();
            return {AdminStatus::Applied, "Monster admitted through shared actor lifecycle", *result.value};
        }
        case AdminOperation::DamageMonster:
        case AdminOperation::KillMonster: {
            const auto &unit = std::get<AdminUnit>(request.arguments);
            const bool kill = request.operation == AdminOperation::KillMonster;
            if (!unit.id || (!kill && (unit.amount <= 0 || unit.amount > UINT32_MAX)))
                return {AdminStatus::InvalidArguments, "Supply a monster id and damage amount 1..UINT32_MAX"};
            const auto result = host.damageMonster(*binding, EntityId{unit.id}, kill ? std::nullopt : std::optional<uint32_t>{uint32_t(unit.amount)});
            if (!result) return {AdminStatus::Unavailable, "Monster must be alive in the entered player's area"};
            publishEvents(); publishMonsters();
            return {AdminStatus::Applied, "Monster damage committed; normal death, rewards and loot run on the next fixed step"};
        }
        case AdminOperation::Travel: {
            const auto &destination=std::get<AdminTravel>(request.arguments);
            if(destination.level<1 || destination.level>136) return {AdminStatus::InvalidArguments,"Original area must be 1..136"};
            const auto result=host.relocate(*binding,RegionId(destination.level),destination.position);
            if(!result) return {AdminStatus::Unavailable,"Area content is preparing, position is unavailable, or output capacity is blocked; retry after inspecting the host snapshot"};
            publishEvents();return {AdminStatus::Applied,"Host relocated the player through prepared native collision and original travel facts; no task or waypoint was granted"};
        }
        case AdminOperation::UnlockWaypoints: {
            const auto &unlock=std::get<AdminWaypoints>(request.arguments);
            if(unlock.level.has_value()==unlock.all || (unlock.level && (*unlock.level<1 || *unlock.level>136)))
                return {AdminStatus::InvalidArguments,"Supply exactly one of level (1..136) or all=true"};
            if(peer.phase!=GamePhase::Entered) return {AdminStatus::Unavailable,"Enter the host game before unlocking waypoints"};
            const auto result=host.unlockWaypoints(*binding,unlock.level?std::optional<RegionId>{RegionId(*unlock.level)}:std::nullopt);
            if(result.status==server::DomainStatus::InvalidRequest)
                return {AdminStatus::InvalidArguments,"The supplied MPQ area is not a valid waypoint destination"};
            if(!result) return {AdminStatus::Unavailable,"Waypoint unlock rejected (dead player, unavailable rules or transaction capacity)"};
            publishEvents();
            return {AdminStatus::Applied,unlock.all?"All MPQ waypoints unlocked for the current difficulty":"Requested MPQ waypoint unlocked for the current difficulty"};
        }
        case AdminOperation::GrantShrine:
        case AdminOperation::SpawnObject: {
            const auto &spawn=std::get<AdminSpawn>(request.arguments);int code=-1;
            const auto parsed=std::from_chars(spawn.code.data(),spawn.code.data()+spawn.code.size(),code);
            if(peer.phase!=GamePhase::Entered || parsed.ec!=std::errc{} || parsed.ptr!=spawn.code.data()+spawn.code.size() || code<0) return {AdminStatus::InvalidArguments,"Supply an original Objects Id or shrine code"};
            const auto view=host.read(*binding);const auto origin=shared.terrain.at(binding->game).at(view->actor.region).origin;
            std::optional<int> shrine=spawn.shrine;
            if(request.operation==AdminOperation::GrantShrine) {if(code<1 || code>22) return {AdminStatus::InvalidArguments,"Shrine code must be 1..22"};shrine=code;code=136;}
            const auto position=spawn.position?*spawn.position-origin:view->actor.position+Vec{2,0};
            const auto result=host.spawnObject(*binding,{code,view->actor.region,position,shrine,spawn.trap});
            if(!result) return {AdminStatus::Unavailable,"Original object admission rejected"};
            return {AdminStatus::Applied,"Original object admitted; use the normal native interaction command to activate it",*result.value};
        }
        case AdminOperation::GrantHireling: {
            const auto &grant=std::get<AdminSpawn>(request.arguments);int type=-1;
            const auto parsed=std::from_chars(grant.code.data(),grant.code.data()+grant.code.size(),type);
            const auto character=host.exportCharacter(*binding);
            if(peer.phase!=GamePhase::Entered || !character || parsed.ec!=std::errc{} || parsed.ptr!=grant.code.data()+grant.code.size() ||
                grant.level<1 || grant.level>character->player.level) return {AdminStatus::InvalidArguments,"Supply the current MPQ Hireling Id and a level not exceeding the owner"};
            auto row=content->hirelings.end();
            for(auto candidate=content->hirelings.begin();candidate!=content->hirelings.end();++candidate)
                if(candidate->id==type && (row==content->hirelings.end() || (candidate->level<=grant.level && candidate->level>row->level))) row=candidate;
            if(row==content->hirelings.end()) return {AdminStatus::InvalidArguments,"Hireling Id is unavailable in the original table"};
            const auto stats=deriveHirelingStats(*row,grant.level);auto random=initialRandom(uint32_t(host.nextEntity(binding->game)));
            HirelingRecord record{row->sourceRow,row->classId,row->nameFirst,grant.level,float(stats.life),stats.experience,rollRandom(random)};
            const auto result=host.grantHireling(*binding,std::move(record));if(!result) return {AdminStatus::Unavailable,"Hireling replacement transaction rejected"};
            return {AdminStatus::Applied,"Hireling replacement queued; the previous hireling remains until the new birth commits"};
        }
        case AdminOperation::ResetAttributes:
        case AdminOperation::ResetSkills: {
            if(peer.phase!=GamePhase::Entered) return {AdminStatus::Unavailable,"Enter the host game before resetting points"};
            const bool attributes=request.operation==AdminOperation::ResetAttributes;
            const auto result=host.resetPoints(*binding,attributes?PointRefund::Attributes:PointRefund::Skills);
            if(!result) return {AdminStatus::Unavailable,"Point reset rejected (dead player, active skill, refund overflow or unavailable transaction capacity/rules)"};
            publishEvents();publishPlayers();
            return {AdminStatus::Applied,attributes?"Allocated base attribute points refunded; quest rewards were not consumed":"Allocated base skill points refunded and skill bindings cleared; quest rewards were not consumed"};
        }
        }
    } catch (const std::invalid_argument &e) { return {AdminStatus::InvalidArguments,e.what()}; }
    catch (const std::exception &e) { return {AdminStatus::Failed, e.what()}; }
    return {AdminStatus::InvalidArguments, "Unknown host operation"};
}
}
