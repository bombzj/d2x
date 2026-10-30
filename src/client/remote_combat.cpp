#include "content/monsters/monster_identity.hpp"
#include "remote_combat.hpp"
#include "remote_inventory.hpp"
#include "item_skill_sources.hpp"
#include "content/skills/skill_eligibility.hpp"
#include "network/protocol/bits.hpp"
#include "network/protocol/client_profile.hpp"
#include "gameplay/units/distance.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <stdexcept>

namespace d2x {
RemoteCombat::RemoteCombat(Archives &archives, RemoteTown &scene, net::RealmSession &session, RemoteInventory &inventory)
    : scene_(scene), inventory_(inventory), session_(session) {
    for (const char *name : {"skills", "skilldesc", "missiles", "playerclass", "charstats", "monstats", "monstats2", "pettype", "states", "itemstatcost", "setitems", "weapons", "armor"})
        tables_.emplace(name, DataTable(archives.read("data/global/excel/" + std::string(name) + ".txt")));
    for (const auto &[name, column] : {std::pair{"skills", "Id"}, {"itemstatcost", "ID"}}) {
        const auto &table = tables_.at(name);
        auto &rows = std::string_view(name) == "skills" ? skills_ : stats_;
        for (size_t row = 0; row < table.rows().size(); ++row)
            if (auto id = table.number(row, column); id && *id >= 0 && *id <= UINT16_MAX) rows.emplace(uint16_t(*id), row);
    }
    const auto &states = tables_.at("states");

    for (size_t row = 0; row < states.rows().size(); ++row)
        if (auto id = states.number(row, "Id"); id && *id >= 0 && *id < 255) states_.emplace(uint8_t(*id), row);
    const auto &monsters = tables_.at("monstats");
    for (size_t row = 0; row < monsters.rows().size(); ++row)
        if (!monsters.value(row,"hcIdx").empty()) {
            const int id=compiledMonsterIndex(monsters,row);
            if(id<=UINT16_MAX) monsters_.emplace(uint16_t(id),row);
        }
    const auto &extras = tables_.at("monstats2");
    constexpr std::array componentCodes{"HD","TR","LG","RA","LA","RH","LH","SH","S1","S2","S3","S4","S5","S6","S7","S8"};
    for (const auto &[id, row] : monsters_)
        for (size_t extra = 0; extra < extras.rows().size(); ++extra) {
            if (extras.value(extra, "Id") != monsters.value(row, "MonStatsEx")) continue;
            auto &widths = componentBits_[id];
            for (size_t part = 0; part < widths.size(); ++part) {
                auto variants = std::string(extras.value(extra, std::string(componentCodes[part]) + "v"));
                std::erase(variants, '"');
                unsigned count = 0;
                for (size_t start = 0; start < variants.size();) {
                    ++count;
                    const auto end = variants.find(',', start);
                    if (end == std::string::npos) break;
                    start = end + 1;
                }
                widths[part] = count >= 3 ? std::bit_width(count - 1) : 1;
            }
            break;
        }
    const auto &classes=tables_.at("playerclass"), &characters=tables_.at("charstats");
    for (size_t row=0; row<classes.rows().size(); ++row) {
        const auto code=classes.value(row,"Code");
        if (code.empty()) continue;
        auto &innate=innateSkills_[std::string(code)];
        for (size_t character=0; character<characters.rows().size(); ++character) {
            if (characters.value(character,"class")!=classes.value(row,"Player Class")) continue;
            for (const int id : loadInnateSkillIds(tables_.at("skills"), characters, character))
                if (id <= UINT16_MAX) innate.insert(uint16_t(id));
        }
    }
    const bool remoteOnly = session_.read().protocolProfile != "lod113c";
    for (const auto &[id, row] : skills_) {
        const bool innate = std::any_of(innateSkills_.begin(), innateSkills_.end(),
            [&](const auto &entry) { return entry.second.contains(id); });
        const auto &skills = tables_.at("skills");
        const bool itemSkill = skills.number(row, "srvdofunc") == 113 && skills.number(row, "scroll") == 1 &&
            skills.number(row, "InGame") == 1;
        const bool itemEffect=skills.number(row,"InGame")==1 && skills.number(row,"ItemEffect").value_or(0)>0;
        if (remoteOnly && skills.value(row, "charclass").empty() && !innate && !itemSkill && !itemEffect) continue;
        skillMetadata_.emplace(id, loadSkillEligibilityMetadata(tables_.at("skills"), row, &tables_.at("skilldesc")));
    }
    std::optional<uint16_t> defaultSkill;
    for (const auto &[id, metadata] : skillMetadata_)
        if (metadata.basicAction == BasicSkillAction::Attack) {
            if (defaultSkill) throw std::runtime_error("Current MPQ has duplicate basic attack identities");
            defaultSkill = id;
        }
    if (!defaultSkill) throw std::runtime_error("Current MPQ has no basic attack identity");
    session_.configure_player_default_skill(*defaultSkill);
}
bool RemoteCombat::reject(std::string reason) { reason_ = std::move(reason); return false; }
std::string_view RemoteCombat::classCode() const {
    const auto &view = session_.read();
    for (const auto &character : view.characters)
        if (character.name == view.selectedCharacter && character.characterClass)
            {
                const auto &classes = tables_.at("playerclass");
                size_t ordinal = 0;
                for (size_t row = 0; row < classes.rows().size(); ++row) {
                    if (classes.value(row,"Code").empty()) continue; // Expansion separator is not a class.
                    if (ordinal++ == *character.characterClass) return classes.value(row,"Code");
                }
            }
    return {};
}
bool RemoteCombat::innateSkill(uint16_t id) const {
    const auto found=innateSkills_.find(classCode());
    return found!=innateSkills_.end() && found->second.contains(id);
}
std::optional<uint16_t> RemoteCombat::superUnique(const OnlineUnit &unit) const {
    const auto found=unitOwners_.find(unit.key);
    if(found==unitOwners_.end() || !found->second.decoded || found->second.revision!=unit.appearanceRevision) return {};
    return found->second.superUnique;
}
bool RemoteCombat::hostile(const OnlineUnit &unit) const {
    if (!hostileSource(unit)) return false;
    if(unit.key.type==0) return true;
    const auto monster = monsters_.find(*monsterClass(unit));
    return tables_.at("monstats").number(monster->second, "killable").value_or(0) != 0;
}
bool RemoteCombat::hostileSource(const OnlineUnit &unit) const {
    if(unit.key.type==0) {
        const auto self=session_.read().load.playerUnitId;
        return self && *self!=unit.key.id && hostilePlayers(*self,unit.key.id);
    }
    return monsterDisposition(unit) == MonsterDisposition::Hostile;
}
std::optional<UnitAlignment> RemoteCombat::alignment(const OnlineUnit &unit) const {
    if (unit.key.type > 1) return UnitAlignment::Good;
    const auto snapshot = unitStates_.find(unit.key);
    if (snapshot == unitStates_.end()) return UnitAlignment::Evil;
    if (!snapshot->second.decoded) return {};
    for (const auto &[id, state] : snapshot->second.states) {
        (void)id;
        if (state.name != "alignment") continue;
        for (const auto &stat : state.stats)
            if (const auto row = stats_.find(stat.id); row != stats_.end() &&
                tables_.at("itemstatcost").value(row->second, "Stat") == "alignment") {
                if (stat.value < 0 || stat.value > 2) return {};
                return UnitAlignment(stat.value);
            }
    }
    // Original STATLIST_GetUnitAlignment: absent state or zero stat => EVIL.
    return UnitAlignment::Evil;
}
std::optional<uint32_t> RemoteCombat::playerOwner(const OnlineUnit &unit) const {
    if (unit.key.type == 0) return unit.key.id;
    const auto &world = session_.read().world;
    if (const auto pet = world.pets.find(unit.key.id); pet != world.pets.end()) return pet->second.owner;
    if (world.hireling && world.hireling->id == unit.key.id) return world.hireling->owner;
    if (const auto owner = unitOwners_.find(unit.key); owner != unitOwners_.end() && owner->second.decoded)
        return owner->second.player;
    return {};
}
bool RemoteCombat::sameParty(uint32_t first, uint32_t second) const {
    const auto &players = session_.read().world.social.players;
    const auto a = players.find(first), b = players.find(second);
    return a != players.end() && b != players.end() && a->second.partyId &&
        *a->second.partyId != UINT16_MAX && a->second.partyId == b->second.partyId;
}
bool RemoteCombat::hostilePlayers(uint32_t first, uint32_t second) const {
    const auto &relations = session_.read().world.social.relationships;
    const auto relation = relations.find({first, second});
    // Native 1.13c D2Client RVA 4FD90 returns zero for a missing relation.
    return relation!=relations.end() && (relation->second & 8) != 0;
}
std::optional<OnlineUnitKey> RemoteCombat::storedOwner(const OnlineUnit &unit) const {
    const auto snapshot=unitStates_.find(unit.key);if(snapshot==unitStates_.end() || !snapshot->second.decoded) return {};
    for(const auto &[id,state]:snapshot->second.states) {
        (void)id;if(state.name!="sourceunit") continue;
        std::optional<uint8_t> type;std::optional<uint32_t> owner;
        for(const auto &stat:state.stats) {
            const auto row=stats_.find(stat.id);if(row==stats_.end()) continue;
            const auto name=tables_.at("itemstatcost").value(row->second,"Stat");
            if(name=="source_unit_type" && stat.value>=0 && stat.value<=6) type=uint8_t(stat.value);
            if(name=="source_unit_id" && stat.value>=0 && stat.value<=UINT32_MAX) owner=uint32_t(stat.value);
        }
        // A zero type is omitted by the native base stat list.
        if(owner && *owner) return OnlineUnitKey{type.value_or(0),*owner};
    }
    return {};
}
const std::vector<OnlineItemStat> *RemoteCombat::monsterPropertyStats(const OnlineUnit &unit) const {
    const auto found=unitOwners_.find(unit.key);
    return found!=unitOwners_.end() && found->second.revision==unit.appearanceRevision && found->second.statsDecoded?
        &found->second.stats:nullptr;
}
std::optional<uint16_t> RemoteCombat::monsterClass(const OnlineUnit &unit) const {
    if(unit.key.type!=1) return unit.classId;
    const auto snapshot=unitStates_.find(unit.key);
    if(snapshot!=unitStates_.end() && snapshot->second.decoded) for(const auto &[id,state]:snapshot->second.states) {
        (void)id;if(state.name!="changeclass") continue;
        for(const auto &stat:state.stats) {
            const auto row=stats_.find(stat.id);if(row==stats_.end() || tables_.at("itemstatcost").value(row->second,"Stat")!="shortparam1") continue;
            if(stat.value>=0 && stat.value<=UINT16_MAX && monsters_.contains(uint16_t(stat.value))) return uint16_t(stat.value);
        }
    }
    // State.setfunc=5 reinitializes the local unit. Removing the stat list
    // has no inverse class change; retain it until another AC assignment.
    const auto changed=changedClasses_.find(unit.key);
    return changed!=changedClasses_.end() && changed->second.first==unit.assignmentRevision?std::optional{changed->second.second}:unit.classId;
}
uint8_t RemoteCombat::ownershipTransform(const OnlineUnit &unit) const {
    // RVA A7ACD -> 21B10: Utrans=255 uses the pet roster and hostility,
    // not party membership. No roster owner returns transform zero.
    const auto &view=session_.read();
    const auto pet=view.world.pets.find(unit.key.id);
    if(!view.load.playerUnitId || pet==view.world.pets.end()) return 0;
    return pet->second.owner==*view.load.playerUnitId || !hostilePlayers(pet->second.owner,*view.load.playerUnitId) ? 1 : 0;
}
bool RemoteCombat::allied(const OnlineUnit &unit) const {
    const auto &view=session_.read();
    if(!view.load.playerUnitId) return false;
    const auto self=view.world.units.find({0,*view.load.playerUnitId});
    if(self==view.world.units.end()) return false;
    if(const auto cached=unitOwners_.find(unit.key);cached!=unitOwners_.end() && !cached->second.decoded) return false;
    if(const auto owner=playerOwner(unit)) return *owner==self->first.id || sameParty(self->first.id,*owner);
    const auto first=alignment(self->second),second=alignment(unit);
    return first && second && sameAlignment(*first,*second);
}
RemoteCombat::MonsterDisposition RemoteCombat::monsterDisposition(const OnlineUnit &unit) const {
    if (unit.key.type != 1 || !monsterClass(unit)) return MonsterDisposition::Unknown;
    const auto monster = monsters_.find(*monsterClass(unit));
    if (monster == monsters_.end()) return MonsterDisposition::Unknown;
    const auto &view = session_.read();
    if (!view.load.playerUnitId) return MonsterDisposition::Unknown;
    const auto self = view.world.units.find({0, *view.load.playerUnitId});
    if (self == view.world.units.end()) return MonsterDisposition::Unknown;
    if (const auto owner = unitOwners_.find(unit.key); owner != unitOwners_.end() && !owner->second.decoded)
        return MonsterDisposition::Unknown;
    if (const auto owner = playerOwner(unit)) {
        if(*owner==self->first.id) return MonsterDisposition::NonHostile;
        return hostilePlayers(self->first.id,*owner) ? MonsterDisposition::Hostile : MonsterDisposition::NonHostile;
    }
    const auto sourceAlignment = alignment(self->second), targetAlignment = alignment(unit);
    if (!sourceAlignment || !targetAlignment) return MonsterDisposition::Unknown;
    return sameAlignment(*sourceAlignment, *targetAlignment) ? MonsterDisposition::NonHostile : MonsterDisposition::Hostile;
}
bool RemoteCombat::monsterTargetEligible(const OnlineUnit &unit, bool targetCorpse) const {
    if(unit.key.type!=1) return false;
    // Corpse selectors do not consult live-unit alignment or killable.
    if(targetCorpse) return corpseSelectable(unit);
    if(const auto snapshot=unitStates_.find(unit.key);snapshot!=unitStates_.end() && snapshot->second.decoded)
        for(const auto &[id,state]:snapshot->second.states) {(void)id;if(state.name=="attached") return false;}
    if((unit.nativeMode || session_.read().protocolProfile=="lod113c") && unit.mode==8 && !unit.actionSkill) {
        const auto found=monsters_.find(monsterClass(unit).value_or(UINT16_MAX));
        if(found!=monsters_.end()) {
            const auto ai=tables_.at("monstats").value(found->second,"AI");
            if(ai=="Vulture" || ai=="SandMaggot" || ai=="FrogDemon") return false;
        }
    }
    return hostile(unit) && !onlineMonsterCorpse(unit);
}
bool RemoteCombat::skillTargetEligible(const OnlineUnit &unit,uint16_t skill) const {
    const auto row=skills_.find(skill);if(row==skills_.end() || !unit.position) return false;
    const auto &table=tables_.at("skills");
    const auto n=[&](std::string_view column) {return table.number(row->second,column).value_or(0);};
    if (!skillMetadata_.contains(skill) || skillMetadata_.at(skill).itemSkill) return false; // SrvDo113 searches inventory, not a hovered unit.
    if(n("TargetCorpse")) return corpseTargetEligible(unit,n("SelectProc"));
    if(n("srvstfunc")==34 && n("srvdofunc")==72) return monsterTargetEligible(unit,true);
    if(n("srvstfunc")==33 && (n("srvdofunc")==69 || n("srvdofunc")==75)) {
        if(!monsterTargetEligible(unit,true)) return false;
        const auto monster=monsters_.find(monsterClass(unit).value_or(UINT16_MAX));
        const auto extended=tables_.at("monstats").value(monster->second,"MonStatsEx");
        const auto &extra=tables_.at("monstats2");
        for(size_t row=0;row<extra.rows().size();++row) if(extra.value(row,"Id")==extended)
            return extra.number(row,"soft").value_or(0)!=0;
        return false;
    }
    if(n("srvstfunc")==3 && n("srvdofunc")==4) {
        if(unit.key.type!=1 || onlineMonsterCorpse(unit)) return false;
        const auto &world=session_.read().world;
        const auto pet=world.pets.find(unit.key.id);
        if(pet==world.pets.end() || pet->second.owner!=session_.read().load.playerUnitId) return false;
        const auto &types=tables_.at("pettype");
        for(size_t row=0;row<types.rows().size();++row)
            if(types.number(row,"idx")==pet->second.type) return types.number(row,"unsummon").value_or(0)!=0;
        return false;
    }
    if(n("TargetAlly") || n("TargetPet")) {
        // SrvDo025 uses SUNIT_AreUnitsAligned, including unowned GOOD units.
        const auto profile=session_.read().protocolProfile;
        const bool pd2HolyLight=n("srvdofunc")==176 && (profile==net::protocol::clientCompatibility(
            net::protocol::ClientProfile::Pd2CnNormal20261020).id || profile==net::protocol::clientCompatibility(net::protocol::ClientProfile::Pd2Local).id);
        if(n("srvdofunc")==25 || pd2HolyLight) return (allied(unit) || hostileSource(unit)) &&
            (unit.key.type==1 ? !onlineMonsterCorpse(unit) : unit.key.type==0 &&
             !session_.read().world.corpseOwners.contains(unit.key.id) &&
             (unit.nativeMode ? unit.mode!=0 && unit.mode!=17 : unit.mode!=8 && unit.mode!=9));
        if(unit.key.type==1 && hostile(unit) && !onlineMonsterCorpse(unit)) {
            // Missiles::SrvHit07 permits enemy targeting as well as ally healing;
            // its undead collision restriction is resolved by the authority.
            const auto &missiles=tables_.at("missiles");
            for(size_t missileRow=0;missileRow<missiles.rows().size();++missileRow)
                if(missiles.value(missileRow,"Missile")==table.value(row->second,"srvmissile") && missiles.number(missileRow,"pSrvHitFunc")==7) return true;
        }
        const auto &view=session_.read();
        if(!view.load.playerUnitId) return false;
        const auto owner=playerOwner(unit);
        const bool ownPet=unit.key.type==1 && owner==view.load.playerUnitId;
        const bool ally=owner && sameParty(*view.load.playerUnitId,*owner);
        if(unit.key.type==0) return n("TargetAlly") && ally && !hostilePlayers(*view.load.playerUnitId,unit.key.id) &&
            !view.world.corpseOwners.contains(unit.key.id) &&
            (unit.nativeMode ? unit.mode!=0 && unit.mode!=17 : unit.mode!=8 && unit.mode!=9);
        return unit.key.type==1 && !onlineMonsterCorpse(unit) &&
            ((n("TargetPet") && ownPet) || (n("TargetAlly") && ally)) &&
            monsterDisposition(unit)==MonsterDisposition::NonHostile;
    }
    if(n("srvstfunc")==12 && n("srvdofunc")==21 && (unit.key.type==2 || unit.key.type==4)) return true;
    if(unit.key.type==0) return !n("TargetCorpse") && hostile(unit) &&
        !session_.read().world.corpseOwners.contains(unit.key.id) &&
        (unit.nativeMode ? unit.mode!=0 && unit.mode!=17 : unit.mode!=8 && unit.mode!=9);
    return monsterTargetEligible(unit,n("TargetCorpse")!=0);
}
bool RemoteCombat::skillTargetEligible(const OnlineItem &item, uint16_t skill) const {
    const auto row = skills_.find(skill);
    if(item.mode==3 && row!=skills_.end() && tables_.at("skills").number(row->second,"srvstfunc")==20 && tables_.at("skills").number(row->second,"srvdofunc")==57) {
        if(!(item.flags & 0x10u)) return false;
        for(const auto name:{"weapons","armor"}) {const auto &table=tables_.at(name);for(size_t i=0;i<table.rows().size();++i) if(table.value(i,"code")==item.code) return (table.number(i,"bitfield1").value_or(0)&2)!=0;}
        return false;
    }
    return item.mode == 3 && row != skills_.end() &&
        tables_.at("skills").number(row->second, "srvstfunc") == 12 &&
        tables_.at("skills").number(row->second, "srvdofunc") == 21;
}
bool RemoteCombat::corpseSelectable(const OnlineUnit &unit) const {
    const auto monster=monsters_.find(monsterClass(unit).value_or(UINT16_MAX));
    if (unit.key.type!=1 || unit.mode!=12 || monster==monsters_.end()) return false;
    const auto &extras=tables_.at("monstats2");
    const auto identity=tables_.at("monstats").value(monster->second,"MonStatsEx");
    bool selectable=false;
    for (size_t row=0; row<extras.rows().size(); ++row)
        if (extras.value(row,"Id")==identity) { selectable=extras.number(row,"corpseSel").value_or(0)!=0; break; }
    if (!selectable) return false;
    if (const auto snapshot=unitStates_.find(unit.key); snapshot!=unitStates_.end()) {
        if (!snapshot->second.decoded) return false;
        for (const auto &[id,state]:snapshot->second.states) {
            (void)state;
            if (const auto row=states_.find(id); row!=states_.end())
                if (tables_.at("states").value(row->second,"state")=="corpse_noselect" || tables_.at("states").number(row->second,"hide").value_or(0) ||
                    tables_.at("states").number(row->second,"udead").value_or(0)) return false;
        }
    }
    return true;
}
bool RemoteCombat::corpseTargetEligible(const OnlineUnit &unit,int selection) const {
    if(!corpseSelectable(unit)) return false;
    const auto monster=monsters_.find(*monsterClass(unit));
    const auto &monstats=tables_.at("monstats");
    const auto &extras=tables_.at("monstats2");
    const auto identity=monstats.value(monster->second,"MonStatsEx");
    const auto extraNumber=[&](std::string_view field) {
        for(size_t row=0;row<extras.rows().size();++row)
            if(extras.value(row,"Id")==identity) return extras.number(row,field).value_or(0);
        return 0;
    };
    // D2Client SelectProc table: 1/5=corpse, 2=consumable, 3=revive,
    // 4/6=soft corpse. Read the current MPQ so PD2's archer uses the
    // same selector as its skeleton and mage, without retail skill IDs.
    switch(selection) {
    case 1: case 5: return true;
    case 2: return monstats.number(monster->second,"Velocity").value_or(0)!=0;
    case 3: {
        if(!monstats.number(monster->second,"Velocity").value_or(0) || !extraNumber("mWL") ||
           !monstats.number(monster->second,"switchai").value_or(0) || monstats.number(monster->second,"boss").value_or(0)) return false;
        const auto appearance=unitOwners_.find(unit.key);
        // Native UNIQUE/SUPERUNIQUE flags are wire rank bits 1/2.
        // Champions also carry UNIQUE; do not reinterpret the wire mask.
        if(appearance==unitOwners_.end() || !appearance->second.decoded || (appearance->second.rank & 6u)) return false;
        // Retail SelectProc3 excludes the SandLeaper base class.
        return monstats.value(monster->second,"BaseId")!="sandleaper1";
    }
    case 4: case 6: return extraNumber("soft")!=0;
    default: return false;
    }
}
bool RemoteCombat::submit(OnlineCombatCommand command,std::optional<Vec> inputOrigin) {
    command.interaction.reset();
    if (!command.context) command.context = onlineIntentContext(session_.read());
    if (!onlineWorldMatches(*command.context, session_.read()))
        return reject("Combat intent belongs to a previous game or area");
    if(!inputOrigin) inputOrigin=scene_.inputOrigin(session_.read());
    if(inputOrigin && (!std::isfinite(inputOrigin->x) || !std::isfinite(inputOrigin->y) ||
        inputOrigin->x<0 || inputOrigin->y<0 || inputOrigin->x>UINT16_MAX || inputOrigin->y>UINT16_MAX))
        return reject("Invalid local combat input origin");
    scene_.update(session_.read(),inputOrigin);
    update();
    const auto &world = session_.read().world;
    using Action = OnlineCombatCommand::Action;
    if (command.action == Action::BindHotkey && command.hotkeySlot >= world.skillHotkeys.size())
        return reject("Hotkey slot is outside the native range");
    const auto attribute = [&](uint8_t id) { const auto it = world.playerAttributes.find(id); return it == world.playerAttributes.end() ? 0u : it->second; };
    if (command.action == Action::SpendAttribute) {
        if (command.attribute > 3 || !command.count || command.count > 100 || attribute(4) < command.count)
            return reject("Insufficient server attribute points or invalid count");
    } else if (command.action != Action::Stop) {
        if (command.action == Action::Cast) {
            const auto selected = command.hand == OnlineSkillHand::Left ? world.leftSkill : world.rightSkill;
            if (!selected) return reject("No confirmed skill is selected for this hand");
            command.skill = selected->skill;command.owner=selected->owner;
        }
        const auto row = skills_.find(command.skill);
        if (row == skills_.end()) return reject("Skill is absent from current MPQ");
        if (!skillMetadata_.contains(command.skill)) return reject("Player skill metadata is not supported by this profile");
        const auto &metadata = skillMetadata_.at(command.skill);
        SkillEligibilityInput facts;
        facts.classCode = classCode(); facts.innate = innateSkill(command.skill);
        facts.dead = onlinePlayerDead(world); facts.town = scene_.read().town;
        if (const auto quantity = world.itemSkillQuantities.find(command.skill); quantity != world.itemSkillQuantities.end())
            facts.itemQuantity = quantity->second;
        const auto knownAttribute = [&](uint8_t id) -> std::optional<int> {
            const auto value = world.playerAttributes.find(id);
            return value == world.playerAttributes.end() ? std::nullopt : std::optional{int(value->second)};
        };
        facts.level = knownAttribute(12); facts.skillPoints = knownAttribute(5);
        constexpr std::array<uint8_t, 4> attributeIds{0, 2, 3, 1};
        for (size_t i = 0; i < attributeIds.size(); ++i) facts.attributes[i] = knownAttribute(attributeIds[i]);
        if (const auto rank = world.playerBaseSkills.find(command.skill); rank != world.playerBaseSkills.end())
            facts.baseRank = rank->second;
        else if (world.playerBaseSkillsAssigned) facts.baseRank = 0;
        if (const auto rank = world.playerSkills.find(command.skill); rank != world.playerSkills.end())
            facts.effectiveRank = rank->second;
        if(command.owner!=UINT32_MAX) {
            inventory_.update(session_.read());const auto charges=nativeChargedSkills(session_.read(),inventory_.read(),tables_.at("setitems"));
            const auto source=std::find_if(charges.begin(),charges.end(),[&](const auto &c){return c.item.id.value==command.owner && c.skill==command.skill && c.charges>0;});
            if(source==charges.end() || command.action==Action::LearnSkill) return reject("Charged skill source is unavailable or exhausted");
            facts.effectiveRank=source->rank;
        }
        for (const int required : metadata.prerequisites) {
            if (required > UINT16_MAX) continue;
            if (const auto rank = world.playerBaseSkills.find(uint16_t(required)); rank != world.playerBaseSkills.end())
                facts.prerequisiteRanks.emplace(required, rank->second);
        }
        const auto eligibility = evaluateSkillEligibility(metadata, facts);
        if (command.action == Action::LearnSkill) {
            if (!eligibility.canAllocate)
                return reject("Known server ranks, points and attributes or MPQ skill requirements do not permit learning");
        } else {
            // SKILLS_InitSkillList adds Attack and current CharStats.Skill 1..10;
            // native 0x94 need not list these innate skills. Equipment and item
            // quantities still belong to the server, not an invented skill rank.
            if (metadata.itemSkill && !eligibility.available)
                return reject("Server has not reported a usable scroll or tome for this item skill");
            if (!eligibility.canSelect(command.hand == OnlineSkillHand::Left, metadata))
                return reject("Server has not reported an available active skill for this hand");
            if (command.action == Action::Cast) {
                const auto &binding = scene_.read();
                if (!binding.nativeMapReady)
                    return reject("Skill is waiting for the active map: " + binding.nativeMapReason);
                if (!binding.movementAvailable || !world.playerPosition)
                    return reject("Skill requires an active player position");
                // Match the normal local DynamicPath input origin. A sparse
                // resource sample is not the current pointer origin. This
                // hint never updates the replica or the server's coordinates.
                const auto player=inputOrigin?OnlinePoint{uint16_t(std::floor(inputOrigin->x+.5f)),uint16_t(std::floor(inputOrigin->y+.5f))}:*world.playerPosition;
                if (!eligibility.usableNow)
                    return reject(facts.town ? "Skill cannot be cast in town" : "Skill is not currently usable");
                if (command.point.has_value() == command.target.has_value()) return reject("Specify exactly one point or unit target");
                if(command.point && tables_.at("skills").number(row->second,"TargetCorpse").value_or(0) &&
                   tables_.at("skills").number(row->second,"SearchEnemyNear").value_or(0)) {
                    // Retail D2Client 51650: circle radius 9, then strictly
                    // nearest UNITS_GetDistanceToCoordinates below 10.
                    // PD2 CN patches the initial best distance at 517EF to 7.
                    const auto center=*command.point;
                    int nearest=session_.read().protocolProfile==net::protocol::clientCompatibility(
                        net::protocol::ClientProfile::Pd2CnNormal20261020).id?7:10;
                    for(const auto key:scene_.nearbyUnits(session_.read(),center,9)) {
                        const auto &candidate=world.units.at(key);
                        if(!skillTargetEligible(candidate,command.skill)) continue;
                        const auto position=*candidate.position;
                        const int distance=unitCoordinateDistance(center.x,center.y,position.x,position.y);
                        if(distance<nearest) {nearest=distance;command.target=key;}
                    }
                    if(command.target) command.point.reset();
                }
                if (tables_.at("skills").number(row->second,"TargetCorpse").value_or(0) && !command.target)
                    return reject("This skill requires a selectable corpse");
                if (tables_.at("skills").number(row->second,"srvstfunc")==20 &&
                    tables_.at("skills").number(row->second,"srvdofunc")==57 &&
                    (!command.target || command.target->type!=4))
                    return reject("Iron Golem requires an identified metal item on the ground");
                if (metadata.itemSkill && tables_.at("skills").value(row->second, "range") == "none") {
                    // SrvDo113 invokes pSpell at the player's position. A pointer
                    // over the map edge must not prevent a self item action.
                    command.target.reset();
                    command.point = player;
                }
                auto point = command.point;
                if (command.target && command.target->type == 4) {
                    const auto item = world.items.find(command.target->id);
                    if (item == world.items.end() || !skillTargetEligible(item->second, command.skill))
                        return reject("Skill cannot target this assigned ground item");
                    point = OnlinePoint{item->second.groundX, item->second.groundY};
                } else if (command.target) {
                    const auto target = world.units.find(*command.target);
                    if (target == world.units.end() || !target->second.position)
                        return reject("Target has no assigned native position");
                    if(!skillTargetEligible(target->second,command.skill))
                        return reject("MPQ target categories, native alignment or corpse state do not permit this target");
                    point = target->second.position;
                    if (target->first.type == 2 && tables_.at("skills").number(row->second, "srvstfunc") == 12 &&
                        tables_.at("skills").number(row->second, "srvdofunc") == 21) {
                        const auto object = std::find_if(binding.mapTargets.begin(), binding.mapTargets.end(),
                            [&](const auto &entry) { return entry.unit == target->first; });
                        if (object != binding.mapTargets.end()) {
                            if (object->interaction == OnlineMapInteraction::Stash) command.interaction = OnlineObjectIntent::Stash;
                            else if (object->interaction == OnlineMapInteraction::Waypoint) command.interaction = OnlineObjectIntent::Waypoint;
                        }
                    }
                }
                if (std::abs(int(player.x) - point->x) > 50 || std::abs(int(player.y) - point->y) > 50)
                    return reject("Cast exceeds the native 50-subtile request range");
                const bool teleport=tables_.at("skills").number(row->second,"srvdofunc")==27;
                if (!binding.origin || (!teleport && (point->x < binding.origin->x || point->y < binding.origin->y ||
                    int(point->x - binding.origin->x) >= binding.width || int(point->y - binding.origin->y) >= binding.height)))
                    return reject("Cast target is outside the current map binding");
            }
        }
    }
    if (!session_.submit_combat(command)) return reject(session_.read().error ? session_.read().error->message : "Combat request is unavailable");
    reason_.clear(); return true;
}
void RemoteCombat::update() {
    const auto &view = session_.read();
    const auto &world = view.world;
    if (game_ != view.gameGeneration || area_ != world.areaGeneration) {
        game_ = view.gameGeneration; area_ = world.areaGeneration; unitStates_.clear(); unitOwners_.clear();changedClasses_.clear();
    }
    catalog_.clear();
    const auto &skills = tables_.at("skills");
    for (const auto &[id, row] : skills_) {
        if (!skillMetadata_.contains(id)) continue;
        const auto &metadata = skillMetadata_.at(id);
        const bool classSkill = !classCode().empty() && metadata.classCode == classCode();
        const bool innate = innateSkill(id);
        const auto level = world.playerSkills.find(id);
        const auto quantity = world.itemSkillQuantities.find(id);
        const bool itemAvailable = metadata.itemSkill && quantity != world.itemSkillQuantities.end() && quantity->second;
        if (!classSkill && !innate && !itemAvailable && (level == world.playerSkills.end() || !level->second)) continue;
        OnlineCombatSkillView entry; entry.id = id; entry.name = std::string(skills.value(row, "skill"));
        if (const auto it = world.playerBaseSkills.find(id); it != world.playerBaseSkills.end()) entry.base = it->second;
        if (const auto it = world.playerBonusSkills.find(id); it != world.playerBonusSkills.end()) entry.bonus = it->second;
        if (level != world.playerSkills.end()) entry.level = level->second;
        entry.innate = innate;
        entry.left = metadata.leftAllowed;
        entry.passive = metadata.passive;
        entry.inTown = metadata.allowedInTown; entry.classSkill = classSkill;
        catalog_.push_back(std::move(entry));
    }
    std::erase_if(unitStates_, [&](const auto &entry) { return !world.units.contains(entry.first); });
    std::erase_if(changedClasses_,[&](const auto &entry){const auto unit=world.units.find(entry.first);return unit==world.units.end() || unit->second.assignmentRevision!=entry.second.first;});
    std::erase_if(unitOwners_, [&](const auto &entry) { return !world.units.contains(entry.first); });
    for (const auto &[key, unit] : world.units) {
        if (key.type == 1 && unit.appearanceRevision) {
            auto &owner = unitOwners_[key];
            if (owner.revision != unit.appearanceRevision) {
                owner = {unit.appearanceRevision, false, {}};
                try {
                    net::protocol::BitReader bits(unit.appearanceBits);
                    bits.read(4);
                    if (bits.read(1)) for (const auto width : componentBits_.at(unit.classId.value())) bits.read(width);
                    if (bits.read(1)) {
                        const auto rank = bits.read(5);
                        owner.rank = rank;
                        if (rank & 4) owner.superUnique=uint16_t(bits.read(16));
                        unsigned modifiers = 0;
                        while (bits.read(8)) if (++modifiers > 9) throw std::runtime_error("Too many NPC modifiers");
                        bits.read(16);
                        if (bits.read(1)) owner.player = bits.read(32);
                    }
                    if (bits.read(1)) owner.player = bits.read(31);
                    owner.decoded=true;
                    if(bits.read(1)) {
                        const auto &table=tables_.at("itemstatcost");
                        for(;;) {
                            const auto id=uint16_t(bits.read(9));if(id==511) break;
                            const auto row=stats_.find(id);
                            if(row==stats_.end() || owner.stats.size()>=16) throw std::runtime_error("Invalid native monster property list");
                            const int width=table.number(row->second,"Send Bits").value_or(0),param=table.number(row->second,"Send Param Bits").value_or(0);
                            if(width<1 || width>32 || param<0 || param>32) throw std::runtime_error("Invalid native monster stat width");
                            OnlineItemStat stat;stat.id=id;stat.parameter=bits.read(unsigned(param));
                            const auto raw=bits.read(unsigned(width));stat.value=raw;
                            if(table.number(row->second,"Signed").value_or(0) && (raw&(uint32_t{1}<<(width-1)))) stat.value-=int64_t{1}<<width;
                            owner.stats.push_back(stat);
                        }
                    }
                    owner.statsDecoded=true;
                } catch (const std::exception &) { if(!owner.decoded) owner.player.reset();owner.stats.clear();owner.statsDecoded=false; }
            }
        }
        if (!unit.stateSequence) continue;
        auto &output = unitStates_[key];
        if (output.sequence == unit.stateSequence) continue;
        const auto previous = output.sequence;
        output.sequence = unit.stateSequence;
        try {
            if (!previous) output.decoded = true;
            auto stats = [&](net::protocol::BitReader &bits) {
                std::vector<OnlineItemStat> values;
                for (;;) {
                    const auto id = uint16_t(bits.read(9)); if (id == 511) return values;
                    const auto found = stats_.find(id);
                    if (found == stats_.end()) throw std::runtime_error("Unknown state stat in current MPQ");
                    const auto &table = tables_.at("itemstatcost"); const auto row = found->second;
                    const int width = table.number(row, "Send Bits").value_or(0), param = table.number(row, "Send Param Bits").value_or(0);
                    if (width <= 0 || width > 32 || param < 0 || param > 32) throw std::runtime_error("Invalid MPQ state stat width");
                    OnlineItemStat stat; stat.id = id; stat.parameter = bits.read(unsigned(param));
                    const auto raw = bits.read(unsigned(width)); stat.value = raw;
                    if (table.number(row, "Signed").value_or(0) && (raw & (uint32_t{1} << (width - 1)))) stat.value -= int64_t{1} << width;
                    values.push_back(stat);
                }
            };
            uint64_t expected = previous + 1;
            for (const auto &message : unit.stateMessages) {
                if (message.sequence <= previous) continue;
                if (message.kind == OnlineStateMessage::Kind::Snapshot) {
                    output.states.clear(); output.decoded = true; output.reason.clear();
                } else if (message.sequence != expected) {
                    output.states.clear(); output.decoded = false; output.reason = "Native state history prefix was discarded";
                }
                expected = message.sequence + 1;
                if (!output.decoded) continue;
                auto state = [&](uint8_t id, std::vector<OnlineItemStat> values) {
                    const auto row = states_.find(id);
                    if (row == states_.end()) throw std::runtime_error("Unknown state in current MPQ");
                    output.states[id] = {id, std::string(tables_.at("states").value(row->second, "state")), std::move(values)};
                };
                if (message.kind == OnlineStateMessage::Kind::Disable) output.states.erase(message.state);
                else if (message.kind == OnlineStateMessage::Kind::Snapshot) {
                    net::protocol::BitReader bits(message.packed);
                    for (;;) { const auto id = uint8_t(bits.read(8)); if (id == 255) break;
                        state(id, bits.read(1) ? stats(bits) : std::vector<OnlineItemStat>{}); }
                } else {
                    if (message.packed.empty()) {
                        if (!output.states.contains(message.state)) state(message.state, {});
                    } else { net::protocol::BitReader bits(message.packed); state(message.state, stats(bits)); }
                }
            }
        } catch (const std::exception &error) { output.states.clear(); output.decoded = false; output.reason = error.what(); }
        if(key.type==1) if(const auto changed=monsterClass(unit);changed && changed!=unit.classId)
            changedClasses_.insert_or_assign(key,std::pair{unit.assignmentRevision,*changed});
    }
}
}
