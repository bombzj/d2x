#include "remote_ui_clients_impl.hpp"
#include "gameplay/skills/amazon_passive_spec.hpp"

namespace d2x {
const CharacterView &RemoteUiClients::Impl::Character::read() const { return o.characterView; }
void RemoteUiClients::Impl::Character::submit(CharacterIntent c) {
            std::visit([&](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                OnlineCombatCommand request;
                if constexpr (std::is_same_v<T, BindSkillHotkey>) {
                    if (v.index >= o.hotkeys.size() || v.skill < -1) return;
                    request.action = OnlineCombatCommand::Action::BindHotkey;
                    request.owner=v.owner;request.hotkeySlot = uint8_t(v.index); request.skill = uint16_t(std::max(0,v.skill));
                    request.hand = v.right ? OnlineSkillHand::Right : OnlineSkillHand::Left;
                    request.context = o.context;
                    if (o.combat.submit(request)) {
                        o.hotkeys[v.index] = SkillHotkey{v.skill,v.right,v.owner};
                        o.projectedSources.reset();
                    }
                    else o.notice = o.combat.reason();
                    return;
                } else if constexpr (std::is_same_v<T, AllocateAttribute>) {
                    request.action = OnlineCombatCommand::Action::SpendAttribute;
                    // Attribute intent order STR/DEX/VIT/ENE; native stat order STR/ENE/DEX/VIT.
                    constexpr uint8_t ids[]{0,2,3,1}; request.attribute = ids[size_t(v.attribute)];
                } else if constexpr (std::is_same_v<T, AllocateSkill>) {
                    request.action = OnlineCombatCommand::Action::LearnSkill; request.skill = uint16_t(v.id);
                } else {
                    request.action = OnlineCombatCommand::Action::SelectSkill;
                    request.owner=v.owner;request.skill = uint16_t(std::max(0,v.skill));
                    request.hand = v.right ? OnlineSkillHand::Right : OnlineSkillHand::Left;
                }
                request.context = o.context;
                if (!o.combat.submit(request)) o.notice = o.combat.reason();
            }, c);
        }
// Original private item packets carry the local equipment stat lists. Native NOEQUIP/BROKEN
// flags and active body slots decide contributions; no server flavor or hidden remote stats.
std::optional<int> RemoteUiClients::Impl::knownEquipmentStat(std::string_view name, int level, std::optional<OnlineUnitKey> owner) const {
        const auto &online=session.read(); const auto &world=online.world;
        if (!owner && online.load.playerUnitId) owner=OnlineUnitKey{0,*online.load.playerUnitId};
        if (!owner) return {};
        const auto actor=world.units.find(*owner);
        if (actor==world.units.end()) return {};
        const EquipmentStatKey key{online.gameGeneration,world.itemRevision,items.read().revision,
            actor->second.appearanceRevision,level,world.weaponSet,actor->second.equipmentObserved};
        const auto identity=std::pair{*owner,std::string(name)};
        if (const auto cached=equipmentStats.find(identity);cached!=equipmentStats.end() && cached->second.key==key)
            return cached->second.value;
        const auto value=calculateKnownEquipmentStat(name,level,*owner);
        equipmentStats.insert_or_assign(identity,CachedEquipmentStat{key,value});
        return value;
    }
std::optional<int> RemoteUiClients::Impl::calculateKnownEquipmentStat(std::string_view name, int level, OnlineUnitKey owner) const {
        const auto &online=session.read(); const auto &world=online.world;
        int64_t total=0;
        const auto actor = world.units.find(owner);
        if (actor == world.units.end() || !actor->second.equipmentObserved) return {};
        const auto statRow = std::find_if(data.itemStats.begin(), data.itemStats.end(),
            [&](const auto &entry) { return entry.name == name; });
        if (statRow == data.itemStats.end() || !statRow->id) return {};
        std::map<std::string, std::set<int32_t>, std::less<>> equippedSets;
        std::vector<ItemInstance> setPieces;
        for (const auto &[equippedId, equipped] : world.items) {
            if (equipped.ownerType != owner.type || equipped.owner != owner.id) continue;
            if (equipped.flags & 0x4100u) continue;
            const auto *base = data.items.find(equipped.code);
            const bool active = (equipped.mode == 1 && equipped.body <= 10) || (equipped.mode == 0 && equipped.page == 1 && base && base->equipment.isType("char"));
            if (!active) continue;
            const auto equipment = items.read().items.find(equippedId);
            if (equipment == items.read().items.end() || !equipment->second.decoded || equipment->second.revision != equipped.revision) return {};
            const auto instance = completeItem(equipped, equipment->second, SocketLocation{{}, 0});
            if (!instance) return {};
            for (const auto &bonus : resolveItemStats(data, *instance, level))
                if (bonus.name == name) total += bonus.value;
            if (equipped.mode == 1 && instance->identified && instance->quality == ItemQuality::Set) {
                const auto record = std::find_if(data.setItems.begin(), data.setItems.end(),
                    [&](const auto &entry) { return int32_t(entry.row) == instance->specialRow; });
                if (record == data.setItems.end()) return {};
                equippedSets[record->set].insert(instance->specialRow);
                setPieces.push_back(*instance);
            }
        }
        auto hasStat = [&](const PropertyRange &property) {
            const auto definition = std::find_if(data.properties.begin(), data.properties.end(),
                [&](const auto &entry) { return entry.code == property.code; });
            return definition != data.properties.end() && std::any_of(definition->operations.begin(), definition->operations.end(),
                [&](const auto &operation) { return operation.stat == name; });
        };
        std::set<std::string, std::less<>> countedSets;
        for (const auto &piece : setPieces) {
            const auto record = std::find_if(data.setItems.begin(), data.setItems.end(),
                [&](const auto &entry) { return int32_t(entry.row) == piece.specialRow; });
            const auto &worn = equippedSets.at(record->set);
            const unsigned count = unsigned(worn.size());
            if(record->setAddFunction<0 || record->setAddFunction>2) return {};
            std::vector<int32_t> members;
            for(const auto &other:data.setItems) if(other.set==record->set) members.push_back(int32_t(other.row));
            const std::vector<int32_t> wornRows(worn.begin(),worn.end());
            const auto activeLayers=activeSetItemLayers(record->setAddFunction,int32_t(record->row),members,wornRows);
            for (unsigned layer = 0; layer < activeLayers.size(); ++layer) {
                if (!activeLayers[layer]) continue;
                bool assigned = false;
                for (const auto &bonus : piece.savedSetStats[layer]) {
                    if (bonus.id != *statRow->id || bonus.parameter) continue;
                    total += bonus.value;
                    assigned = true;
                }
                if (!assigned && std::any_of(record->setBonuses.begin(), record->setBonuses.end(), [&](const auto &bonus) {
                    return bonus.perItem && bonus.pieces == int(layer + 2) && hasStat(bonus.property);
                })) return {};
            }
            if (!countedSets.insert(record->set).second) continue;
            const unsigned fullCount = unsigned(std::count_if(data.setItems.begin(), data.setItems.end(),
                [&](const auto &entry) { return entry.set == record->set; }));
            for (const auto &bonus : record->setBonuses) {
                if (bonus.perItem || (bonus.pieces ? count < unsigned(bonus.pieces) : count != fullCount) || !hasStat(bonus.property)) continue;
                const auto &property = bonus.property;
                if (property.directRoll && property.minimum != property.maximum) return {};
                for (const auto &value : resolvePropertyStats(data, property, property.minimum.value_or(0), level, int(piece.level)))
                    if (value.name == name) total += value.value;
            }
        }
        if (total<INT32_MIN || total>INT32_MAX) return {};
        return int(total);
    }
std::optional<UnitLightAttributes> RemoteUiClients::Impl::light(OnlineUnitKey key) const {
        const auto &online=session.read();
        const auto unit=online.world.units.find(key);
        if (unit==online.world.units.end() || key.type!=0) return {};
        const bool self=online.load.playerUnitId && key.id==*online.load.playerUnitId;
        const auto base=[&](std::string_view name) -> int32_t {
            if (self) return int32_t(stat(name).value_or(0));
            const auto id=data.nativeStats->names.find(name);
            if (id==data.nativeStats->names.end()) return 0;
            const auto value=unit->second.attributes.find(uint8_t(id->second));
            return value==unit->second.attributes.end()?0:
                value->second >> data.nativeStats->definitions.at(id->second).valueShift;
        };
        const auto states=combat.states().find(key);
        if (!unit->second.stateSequence || states==combat.states().end() || !states->second.decoded ||
            states->second.sequence!=unit->second.stateSequence) return {};
        const auto full=[&](std::string_view name) -> std::optional<int32_t> {
            const auto equipment=knownEquipmentStat(name,base("level"),key);
            if (!equipment) return {};
            const auto id=data.nativeStats->names.at(std::string(name));
            const auto shift=data.nativeStats->definitions.at(id).valueShift;
            int64_t total=int64_t(base(name))+*equipment;
            for (const auto &[state,list]:states->second.states) {
                (void)state;
                for (const auto &entry:list.stats)
                    if (entry.id==id && !entry.parameter) total+=entry.value >> shift;
            }
            if (total<INT32_MIN || total>INT32_MAX) return {};
            return int32_t(total);
        };
        const auto radius=full("item_lightradius"), color=full("item_lightcolor");
        if (!radius || !color) return {};
        return UnitLightAttributes{*radius,uint32_t(*color)};
    }
std::optional<MovementTiming> RemoteUiClients::Impl::movement(OnlineUnitKey key,bool running) const {
        const auto &online=session.read();const auto &world=online.world;
        if(key.type!=0 || key.id!=online.load.playerUnitId || !world.playerBaseSkillsAssigned) return {};
        const auto unit=world.units.find(key);
        const auto strength=stat("strength"),dexterity=stat("dexterity"),level=stat("level");
        if(unit==world.units.end() || !unit->second.classId || *unit->second.classId>=data.characters.size() ||
            !strength || !dexterity || !level) return {};
        const auto &definition=data.characters[*unit->second.classId];
        const std::map<int,int> ranks(world.playerSkills.begin(),world.playerSkills.end());
        const auto equipment=observedCombatEquipment(key,
            {definition.code,int(*strength),int(*dexterity),int(*level),definition.blockFactor,std::min(world.weaponSet,1u)},
            ranks,0,definition.toHitFactor-35);
        if(!equipment || equipment->modifiers.fasterMoveVelocity<0) return {};
        return playerMovementTiming(definition.walkVelocity,definition.runVelocity,
            equipment->modifiers.fasterMoveVelocity,equipment->modifiers.velocityPercent,running);
    }
std::optional<int> RemoteUiClients::Impl::knownSelfEquipmentStat(std::string_view name,int level) const {
        if(name=="item_fastercastrate") if (const auto native=stat(name)) return int(*native);
        auto total=knownEquipmentStat(name,level);
        const auto &online=session.read();
        if (!total || !online.load.playerUnitId) return {};
        const OnlineUnitKey key{0,*online.load.playerUnitId};
        const auto unit=online.world.units.find(key);
        if (unit==online.world.units.end() || !unit->second.stateSequence) return {};
        const auto states=combat.states().find(key);
        if (states==combat.states().end() || !states->second.decoded || states->second.sequence!=unit->second.stateSequence) return {};
        const auto &costs=data.tables.at("itemstatcost");
        for (size_t row=0;row<costs.rows().size();++row) {
            if (costs.value(row,"Stat")!=name) continue;
            const auto id=costs.number(row,"ID"); if (!id) return {};
            int64_t value=*total;
            for (const auto &[state,list]:states->second.states) {
                (void)state;
                for (const auto &entry:list.stats) if (entry.id==*id && !entry.parameter) value+=entry.value;
            }
            if (value<INT32_MIN || value>INT32_MAX) return {};
            return int(value);
        }
        return {};
    }
void RemoteUiClients::Impl::projectSkillUseFacts(CharacterProjectionInput &input) const {
        const auto &online = session.read();
        if (!online.load.playerUnitId) return;
        const OnlineUnitKey key{0, *online.load.playerUnitId};
        const auto actor = online.world.units.find(key);
        if (actor == online.world.units.end()) return;
        if (actor->second.equipmentObserved) {
            constexpr std::array<uint8_t, 2> bodies{4, 5};
            for (size_t hand = 0; hand < bodies.size(); ++hand) {
                const auto equipped = std::find_if(online.world.items.begin(), online.world.items.end(),
                    [&](const auto &entry) {
                        const auto &item = entry.second;
                        return item.ownerType == 0 && item.owner == key.id && item.mode == 1 && item.body == bodies[hand];
                    });
                if (equipped == online.world.items.end()) {
                    input.handEquipment[hand] = SkillHandEquipment{};
                    continue;
                }
                const auto &wire = equipped->second;
                const auto *base = data.items.find(wire.code);
                if (!base || !base->equipment.known) continue;
                SkillHandEquipment facts;
                facts.present = true; facts.unusable = (wire.flags & 0x4100u) != 0;
                facts.types = base->equipment.types;
                facts.ammunitionType = base->equipment.shoots;
                facts.stackable = base->maxStack > 1;
                const auto decoded = items.read().items.find(wire.id);
                if (decoded != items.read().items.end() && decoded->second.decoded && decoded->second.revision == wire.revision)
                    facts.quantity = decoded->second.quantity;
                input.handEquipment[hand] = std::move(facts);
            }
        }
        const auto observed = combat.states().find(key);
        const auto bloodMana = data.states.find("blood_mana"), inferno = data.states.find("inferno");
        if (actor->second.stateSequence && observed != combat.states().end() && observed->second.decoded &&
            observed->second.sequence == actor->second.stateSequence && bloodMana != data.states.end() && inferno != data.states.end())
            input.resourceStates = CharacterProjectionInput::ResourceStates{
                observed->second.states.contains(bloodMana->second.definition.id),
                observed->second.states.contains(inferno->second.definition.id)};
    }
void RemoteUiClients::Impl::projectCharacter() {
        const auto &online = session.read(); const auto &w = online.world;
        CharacterProjectionInput input;
        input.revision = revision; input.name = online.selectedCharacter;
        input.actor = online.load.playerUnitId ? itemId(*online.load.playerUnitId) : EntityId{};
        if (online.load.playerUnitId)
            if (const auto it = w.units.find({0, *online.load.playerUnitId}); it != w.units.end() && it->second.classId)
                input.characterClass = *it->second.classId;
        const auto &costs = data.tables.at("itemstatcost");
        for (size_t row = 0; row < costs.rows().size(); ++row) {
            const auto name = costs.value(row, "Stat");
            if (auto value = stat(name)) input.stats.emplace(std::string(name), *value);
        }
        input.life = w.life; input.mana = w.mana; input.stamina = w.stamina;
        input.dead = onlinePlayerDead(w); input.running = run; input.town = scene && scene->town;
        input.aliveAfterDeathSave = w.life && !*w.life && w.deathPhase == OnlineDeathPhase::Alive;
        input.weaponSet = w.weaponSet;
        const unsigned weaponSet = std::min(w.weaponSet, 1u);
        input.selectedSkills[weaponSet * 2] = w.leftSkill ? int(w.leftSkill->skill) : -1;
        input.selectedSkills[weaponSet * 2 + 1] = w.rightSkill ? int(w.rightSkill->skill) : -1;
        for (size_t slot = 0; slot < input.hotkeys.size(); ++slot) {
            if (hotkeys[slot]) input.hotkeys[slot] = *hotkeys[slot];
            else if (const auto &native = w.skillHotkeys[slot]; native && native->selection)
                input.hotkeys[slot] = {native->selection->skill ? int(native->selection->skill) : -1,
                    native->hand == OnlineSkillHand::Right,native->selection->owner};
        }
        input.baseRanks.insert(w.playerBaseSkills.begin(), w.playerBaseSkills.end());
        input.effectiveRanks.insert(w.playerSkills.begin(), w.playerSkills.end());
        input.itemSkillQuantities.insert(w.itemSkillQuantities.begin(), w.itemSkillQuantities.end());
        input.baseRanksAssigned = w.playerBaseSkillsAssigned;input.chargedSkills=nativeChargedSkills(online,items.read(),data.tables.at("setitems"));
        input.difficulty = online.load.difficulty;
        projectSkillUseFacts(input);
        if(const auto level=stat("level")) {
            if(const auto itemPierce=knownSelfEquipmentStat("item_pierce",int(*level));itemPierce && w.playerBaseSkillsAssigned) {
                int chance=*itemPierce;
                for(const auto &[skillId,record]:data.skills.skills) if(record.passiveContribution.amazon && record.passiveContribution.amazon->stat==AmazonPassiveStat::Pierce) {
                    const auto rank=w.playerSkills.find(uint16_t(skillId));if(rank!=w.playerSkills.end()) chance+=amazonPassiveValue(*record.passiveContribution.amazon,rank->second);
                }
                input.missilePierceChance=chance;
            }
            const auto ias=knownSelfEquipmentStat("item_fasterattackrate",int(*level));
            const auto fhr=knownSelfEquipmentStat("item_fastergethitrate",int(*level));
            const auto fbr=knownSelfEquipmentStat("item_fasterblockrate",int(*level));
            const auto fcr=knownSelfEquipmentStat("item_fastercastrate",int(*level));
            if(fcr && *fcr>-120) input.stats.insert_or_assign("item_fastercastrate",double(*fcr));
            if(fhr && fbr && *fhr>-120 && *fbr>-120 && online.load.playerUnitId) {
                const auto states=combat.states().find({0,*online.load.playerUnitId});
                if(states!=combat.states().end()) input.recoveryTiming=KnownRecoveryTiming{*fhr,*fbr,states->second.states.contains(data.states.at("holyshield").definition.id)};
            }
            const auto rate=knownSelfEquipmentStat("attackrate",int(*level));
            if(ias && rate && *ias>-120) for(const auto &[id,item]:w.items) {
                (void)id;
                if(item.ownerType!=0 || item.owner!=online.load.playerUnitId || item.mode!=1 || item.body!=4 || (item.flags&0x4100u)) continue;
                const auto *base=data.items.find(item.code);
                if(base && base->base.speed) input.attackTiming=KnownAttackTiming{*ias,*base->base.speed,*rate};
                break;
            }
        }
        if (const auto level=stat("level"); level && online.load.playerUnitId && data.skills.descriptions) {
            const auto &catalog=*data.skills.descriptions;
            const auto unit=w.units.find({0,*online.load.playerUnitId});
            const auto states=combat.states().find({0,*online.load.playerUnitId});
            // Native mastery IDs exceed the single-byte attribute packet.
            // Reconstruct the same read-only stat lists as D2Common: observed
            // equipment + observed states + Skills.passivecalc. If a passive
            // already carries that stat in its native state, count it once.
            for (const auto *name:{"passive_fire_mastery","passive_ltng_mastery","passive_cold_mastery","passive_pois_mastery"}) {
                if (const auto native=stat(name)) { input.stats.insert_or_assign(name,*native); continue; }
                const auto equipmentAndStates=knownSelfEquipmentStat(name,int(*level));
                if (!equipmentAndStates || !input.baseRanksAssigned || unit==w.units.end() || states==combat.states().end()) continue;
                const auto statId=std::find_if(data.itemStats.begin(),data.itemStats.end(),[&](const auto &record){return record.name==name;});
                if (statId==data.itemStats.end() || !statId->id) continue;
                std::set<int> accounted;
                bool known=true;
                if (const auto passives=catalog.passives.find(name);passives!=catalog.passives.end())
                    for (const auto &passive:passives->second) {
                        const auto passiveState=data.states.find(passive.state);
                        if (passiveState==data.states.end()) { known=false; break; }
                        const auto observed=states->second.states.find(passiveState->second.definition.id);
                        if (observed!=states->second.states.end() && std::any_of(observed->second.stats.begin(),observed->second.stats.end(),
                            [&](const auto &entry){return entry.id==*statId->id && !entry.parameter;})) accounted.insert(passive.skill);
                    }
                if (!known) continue;
                SkillDescriptionInput description{0,{},input.baseRanks,input.effectiveRanks,input.stats};
                description.baseRanksComplete=input.baseRanksAssigned;
                const auto passive=projectDescriptionPassiveStat(catalog,description,name,accounted);
                if (!passive) continue;
                const int64_t total=int64_t(*equipmentAndStates)+*passive;
                if (total>=INT32_MIN && total<=INT32_MAX) input.stats.insert_or_assign(name,double(total));
            }
        }
        const auto knownMastery=[&](std::string_view name)->std::optional<int> {
            const auto found=input.stats.find(name);
            return found==input.stats.end()?std::nullopt:std::optional{int(found->second)};
        };
        input.fireMastery=knownMastery("passive_fire_mastery");
        input.lightningMastery=knownMastery("passive_ltng_mastery");
        input.coldDamagePercent=knownMastery("passive_cold_mastery");
        for (size_t hand = 0; hand < input.throwReady.size(); ++hand) {
            const bool leftHand = hand != 0;
            for (uint8_t body : {uint8_t(leftHand ? 5 : 4), uint8_t(5)}) {
                bool found = false;
                for (const auto &[id, item] : w.items) {
                    if (item.mode != 1 || item.body != body || item.ownerType != 0 || item.owner != online.load.playerUnitId) continue;
                    found = true;
                    const auto *definition = data.items.find(item.code);
                    const auto decoded = items.read().items.find(id);
                    input.throwReady[hand] = definition && definition->family == ItemFamily::Weapon &&
                        definition->equipment.throwable && decoded != items.read().items.end() && decoded->second.decoded &&
                        decoded->second.quantity.value_or(0) > 0;
                    break;
                }
                if (found || leftHand) break;
            }
        }
        if(input.characterClass && *input.characterClass<data.characters.size() && online.load.playerUnitId &&
            stat("strength") && stat("dexterity") && stat("level") && input.baseRanksAssigned) {
            const auto &definition=data.characters[*input.characterClass];
            input.combatEquipment=observedCombatEquipment({0,*online.load.playerUnitId},
                {definition.code,int(*stat("strength")),int(*stat("dexterity")),int(*stat("level")),definition.blockFactor,weaponSet},
                input.effectiveRanks,0,definition.toHitFactor-35);
            if(input.combatEquipment) {
                const auto &equipment=*input.combatEquipment;
                const auto &weapon=equipment.equipment.weapons[0];
                input.attackTiming=KnownAttackTiming{weapon.fasterAttack,weapon.baseSpeed,equipment.modifiers.combat.attackRate,weapon.itemBaseSpeed,weapon.itemAttackSpeed,bool(weapon.item)};
            }
        }
        if(input.combatEquipment && stat("vitality") && stat("energy") && stat("maxhp") && stat("maxmana") && stat("maxstamina"))
            for(const auto *name:{"statpts","newskills","experience"}) input.stats.try_emplace(name,0);
        characterView = projectCharacterDisplay(data, input);
        characterView.selectedSkillOwners[weaponSet*2]=w.leftSkill?w.leftSkill->owner:UINT32_MAX;
        characterView.selectedSkillOwners[weaponSet*2+1]=w.rightSkill?w.rightSkill->owner:UINT32_MAX;
    }
}
