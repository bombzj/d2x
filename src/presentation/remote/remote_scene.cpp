#include "remote_scene_impl.hpp"

namespace d2x {

RemoteScene::Impl::Impl(Archives &a, int palette, RemoteMapDisplayState &display, ContentPreparation preparation)
    : archives(a), mapDisplay(display),
      artPalette(palette),
      portraits(a), strings(a), monsterText(sharedClassicData(a, preparation)),
      monstats(monsterText->tables.at("monstats")),
      monstats2(monsterText->tables.at("monstats2")), charstats(monsterText->tables.at("charstats")),
      skills(monsterText->tables.at("skills")), missiles(monsterText->tables.at("missiles")),
      overlays(a.read("data/global/excel/overlay.txt")), states(a.read("data/global/excel/states.txt")),
      weapons(monsterText->tables.at("weapons")),
      difficultyLevels(monsterText->tables.at("difficultylevels")),
      superuniques(a.read("data/global/excel/superuniques.txt")),
      objects(monsterText->tables.at("objects")) {
    const auto &statCosts = monsterText->tables.at("itemstatcost");
    for (size_t row = 0; row < statCosts.rows().size(); ++row) {
        if (statCosts.value(row,"Stat") == "skill_handofathena") slowMissileStat = statCosts.number(row,"ID").value_or(-1);
        if (statCosts.value(row,"Stat") == "damage_blood") damageBloodStat = statCosts.number(row,"ID").value_or(-1);
    }
    for (size_t row=0; row<superuniques.rows().size(); ++row)
        if (const auto id=superuniques.number(row,"hcIdx")) superUniqueRows.emplace(*id,row);
    for (const auto &character:loadCharacterDefinitions(charstats)) characterRows.push_back(character.sourceRow);
    for (size_t row = 0; row < skills.rows().size(); ++row)
        if (auto id = skills.number(row, "Id")) skillRows.emplace(*id, row);
    for (size_t row = 0; row < missiles.rows().size(); ++row) if (auto id = missiles.number(row, "Id")) {
        missileRows.emplace(*id, row); missileNames.emplace(missiles.value(row, "Missile"), row);
    }
    std::map<std::string_view,int,std::less<>> skillNames;
    for(const auto &[id,row]:skillRows) skillNames.emplace(skills.value(row,"skill"),id);
    for(const auto &[id,row]:missileRows) if(const auto skill=skillNames.find(missiles.value(row,"Skill"));skill!=skillNames.end()) missileSkills.emplace(id,skill->second);
    constexpr std::array bloodNames{"blood1", "blood2", "bigblood1", "bigblood2"};
    for (size_t index = 0; index < bloodNames.size(); ++index)
        if (const auto found = missileNames.find(bloodNames[index]); found != missileNames.end() &&
            missiles.number(found->second,"LocalBlood") == 1 && missiles.number(found->second,"pCltDoFunc") == 2)
            bloodMissiles[index] = missiles.number(found->second,"Id").value_or(-1);
    for (size_t row = 0; row < overlays.rows().size(); ++row)
        overlayNames.emplace(overlays.value(row, "overlay"), row);
    for (size_t row = 0; row < states.rows().size(); ++row)
        if (auto id = states.number(row, "ID")) stateRows.emplace(*id, row);
    for(const auto &[skill,row]:skillRows) if(skills.number(row,"cltstfunc")==49 && !skills.value(row,"cltmissilec").empty()) vineVisualSkill=skill;
    for(const auto &[state,stateRow]:stateRows) if(states.number(stateRow,"cltactivefunc")==90)
        for(const auto &[skill,row]:skillRows) if(skills.value(row,"aurastate")==states.value(stateRow,"state"))
            druidStateSkills.emplace(state,skill);
    for (const auto &[state,stateRow]:stateRows) {
        if (states.number(stateRow,"setfunc")!=3) continue;
        for (const auto &[skill,row]:skillRows) {
            (void)skill;
            if (skills.number(row,"srvdofunc")!=23 ||
                skills.value(row,"aurastate")!=states.value(stateRow,"state")) continue;
            const auto missile=missileNames.find(skills.value(row,"cltmissilea"));
            if (missile==missileNames.end()) continue;
            // The state packet need not expose rank. Only accept the native
            // rank-independent client fire definition, never guess its TTL.
            const int id=missiles.number(missile->second,"Id").value_or(-1);
            movementFireMissiles.emplace(uint8_t(state),
                missiles.number(missile->second,"LevRange").value_or(0)==0 ? id : -1);
        }
    }
    for (size_t row = 0; row < monstats.rows().size(); ++row)
        if (auto id = monstats.number(row, "hcIdx"))
            monsterRows.emplace(*id, row);
    const DataTable hirelings(a.read("data/global/excel/hireling.txt"));
    constexpr std::array monsterModes{"dt","nu","wl","gh","a1","a2","bl","sc",
        "s1","s2","s3","s4","dd","kb","sq","rn"};
    for(size_t row=0;row<hirelings.rows().size();++row) {
        if(hirelings.number(row,"Version")!=100) continue;
        const auto cls=hirelings.number(row,"Class");if(!cls) continue;
        for(int slot=1;slot<=6;++slot) {
            const auto name=hirelings.value(row,"Skill"+std::to_string(slot));
            const auto mode=hirelings.number(row,"Mode"+std::to_string(slot));
            if(name.empty() || !mode || *mode<0 || size_t(*mode)>=monsterModes.size()) continue;
            for(const auto &[id,skill]:skillRows) if(skills.value(skill,"skill")==name)
                hirelingSkillModes.emplace(std::pair{*cls,id},monsterModes[size_t(*mode)]);
        }
    }
    for (size_t row = 0; row < monstats2.rows().size(); ++row) {
        const auto id = monstats2.value(row, "Id");
        if (!id.empty())
            monsterExtra.emplace(std::string(id), row);
    }
    // MonsterTbls loads KB's rate from the base identity's WL AnimData,
    // scaling variants by their MPQ Velocity (UNITS_GetBaseAnimSpeed).
    const AnimDataTable animations(a.read("data/global/animdata.d2"));
    const DataTable sequences(a.read("data/global/excel/monseq.txt"));
    for(size_t row=0;row<sequences.rows().size();++row)
        monsterSequenceFlags[std::string(sequences.value(row,"sequence"))].push_back(uint8_t(sequences.number(row,"event").value_or(0)));
    std::map<std::string_view,size_t,std::less<>> baseRows;
    for(const auto &[id,row]:monsterRows) { (void)id;baseRows.emplace(monstats.value(row,"Id"),row); }
    for(const auto &[id,row]:monsterRows) {
        const auto base=baseRows.find(monstats.value(row,"BaseId"));
        if(base==baseRows.end()) continue;
        if(monstats.value(row,"AI")=="CatapultSpotter") {
            auto current=base->second;
            for(int offset=0;offset<4;++offset) {
                if(current==row) {catapultChainOffsets.emplace(id,offset);break;}
                const auto next=baseRows.find(monstats.value(current,"NextInClass"));
                if(next==baseRows.end()) break;
                current=next->second;
            }
        }
        const auto extra=monsterExtra.find(monstats.value(base->second,"MonStatsEx"));
        if(extra==monsterExtra.end()) continue;
        const auto token=lower(std::string(monstats.value(base->second,"Code")));
        const auto weapon=monsterModeWeapon(a,token,"wl",monstats2.value(extra->second,"BaseW"));
        if(weapon.empty()) continue;
        auto key=token+"wl"+weapon;
        for(auto &ch:key) ch=char(std::toupper(static_cast<unsigned char>(ch)));
        const auto *clock=animations.find(key);if(!clock || clock->speed<=0) continue;
        int rate=clock->speed;
        const int velocity=monstats.number(base->second,"Velocity").value_or(0);
        if(row!=base->second && velocity>0) rate=rate*monstats.number(row,"Velocity").value_or(0)/velocity;
        monsterWalkRates.emplace(id,std::clamp(rate,0,32767));
        // Pre-expansion RN uses half the base WL speed, scaled by Run.
        if(id<410) {
            int run=clock->speed/2;
            const int baseRun=monstats.number(base->second,"Run").value_or(0);
            if(row!=base->second && baseRun>0) run=run*monstats.number(row,"Run").value_or(0)/baseRun;
            monsterRunRates.emplace(id,std::clamp(run,0,32767));
        }
    }
}

bool RemoteScene::Impl::channelSkill(int id) const {
    const auto row = skillRows.find(id);
    return row != skillRows.end() && ((skills.number(row->second,"cltdofunc")==24 && (skills.number(row->second,"seqnum")==6 || skills.number(row->second,"seqnum")==18)) ||
        (skills.number(row->second,"cltdofunc")==6 && skills.value(row->second,"charclass")=="ass"));
}

std::optional<int32_t> RemoteScene::Impl::skillCalculation(int skill,int level,std::string_view field,OnlineUnitKey owner,int count) const {
    if (!monsterText->nativeFormulas) return {};
    std::map<int,int> base,effective;StatSnapshot observed;StatList actor;
    if(const auto unit=world().units.find(owner);unit!=world().units.end()) {
        for(const auto &[id,rank]:unit->second.baseSkills) base.emplace(id,rank);
        for(const auto &[id,rank]:unit->second.skills) effective.emplace(id,rank);
    }
    effective.insert_or_assign(skill,level);
    const auto &catalog=*monsterText->nativeFormulas;SkillFormulaInput input{base,effective};
    input.statRules=monsterText->nativeStats.get();input.stats=&observed;input.actor=&actor;
    input.referenceRanks=prepareSkillFormulaRanks(base,effective,{}, {},catalog.maximumSkillRank);
    const auto row=skillRows.find(skill);
    if(row!=skillRows.end()) {
        const auto stat=skills.value(row->second,"aurastat1");const auto key=monsterText->nativeStats->names.find(stat);
        if(key!=monsterText->nativeStats->names.end()) observed.values.insert_or_assign(StatKey{key->second,0},count);
    }
    SkillFormulaValues values(catalog,input);return values.calculation(FormulaDomain::Skill,skill,level,field);
}

bool RemoteScene::Impl::playerDead(const OnlineUnit &u) const {
    return world().corpseOwners.contains(u.key.id) ||
        (u.nativeMode ? (u.mode == 0 || u.mode == 17) : (u.mode == 8 || u.mode == 9));
}

const OnlineWorldView &RemoteScene::Impl::world() const {
    return *worldView;
}

std::optional<uint16_t> RemoteScene::Impl::unitClass(const OnlineUnit &unit) const {
    return combatBinding?combatBinding->monsterClass(unit):unit.classId;
}

std::optional<OnlinePoint> RemoteScene::Impl::targetPosition(OnlineUnitKey key) const {
    if (key.type == 4) {
        const auto item=world().items.find(key.id);
        if (item!=world().items.end() && item->second.mode==3)
            return OnlinePoint{item->second.groundX,item->second.groundY};
        return {};
    }
    const auto unit=world().units.find(key);
    return unit==world().units.end() ? std::nullopt : unit->second.position;
}

std::optional<Vec> RemoteScene::Impl::localUnitPosition(OnlineUnitKey key) const {
    const auto unit = world().units.find(key);
    if (unit == world().units.end() || !unit->second.position) return {};
    const auto display = motion.find(key);
    if (display != motion.end() && display->second.last &&
        display->second.assignmentRevision == unit->second.assignmentRevision &&
        display->second.discontinuity == unit->second.positionDiscontinuity)
        return display->second.position + display->second.correction;
    // A freshly assigned unit starts its local path at its assignment.
    return Vec{float(unit->second.position->x), float(unit->second.position->y)};
}

std::optional<int> RemoteScene::Impl::castMeleeRange(const OnlineUnit &actor, const OnlineCombatCommand &command) const {
    if (command.stationary || !command.target) return {};
    const auto skill = skillRows.find(command.skill);
    if (skill == skillRows.end()) return {};
    const auto range = skills.value(skill->second, "range");
    // D2Common_11016 resolves both from the equipped weapon. PlrMsg
    // approaches only h2h targets; rng and none start at the current feet.
    if (range != "h2h" && range != "both") return {};
    int reach = 0;
    bool ranged = false;
    for (const auto &[id, item] : world().items) {
        (void)id;
        if (item.ownerType != actor.key.type || item.owner != actor.key.id || item.mode != 1 ||
            (item.body != 4 && item.body != 5)) continue;
        for (size_t weapon = 0; weapon < weapons.rows().size(); ++weapon) {
            if (weapons.value(weapon, "code") != item.code) continue;
            reach = std::max(reach, weapons.number(weapon, "rangeadder").value_or(0));
            const auto kind = weapons.value(weapon, "wclass");
            ranged |= kind == "bow" || kind == "xbw";
        }
    }
    if (range == "both" && ranged) return {};
    return reach;
}

RemoteScene::RemoteScene(Archives &a, int palette, RemoteMapDisplayState &display, ContentPreparation preparation)
    : impl_(std::make_unique<Impl>(a, palette, display, preparation)) {}
RemoteScene::~RemoteScene() = default;
RemoteSceneFrame RemoteScene::frame(const OnlineView &v, const Map &m, const OnlineSceneView &s,
                                     SceneView &shared, const RemoteCombat &combat, const RemoteUiClients &clients, bool uiConsumed, Vec mouse, bool rightHand, bool paused) {
    return impl_->draw(v, m, s, shared, combat, clients, uiConsumed, mouse, rightHand, paused);
}
void RemoteScene::drawTargetInfo(const OnlineView &view, const RemoteSceneFrame &frame, SceneView &shared,
                                 const RemoteCombat &combat, EntityId lockedTarget) const {
    if (frame.input.gameGeneration != view.gameGeneration || frame.input.areaGeneration != view.world.areaGeneration ||
        !frame.input.targetingAvailable || shared.characterView().dead) return;
    // The held target takes precedence over other monsters or ground items under
    // the pointer. Never keep a separate display lock or a borrowed replica.
    if (!lockedTarget && (!frame.input.available || frame.input.pickup)) return;
    const auto target = lockedTarget ? lockedTarget : frame.hoveredMonster;
    if (!target || ((target.value - 1) >> 32) != 1) return;
    const auto found = view.world.units.find({1, uint32_t(target.value - 1)});
    if (found == view.world.units.end() || !combat.monsterTargetEligible(found->second, false)) return;
    const auto &unit = found->second;
    const auto row = impl_->monsterRows.find(combat.monsterClass(unit).value_or(UINT16_MAX));
    if (row == impl_->monsterRows.end()) return;
    auto [name, description] = impl_->monsterCaption(unit);
    if (name.empty()) return;
    Color titleColor = impl_->monstats.number(row->second, "boss").value_or(0) ? Color{199,179,119,255} : WHITE;
    if (const auto identity = impl_->monsterIdentities.find(unit.key); identity != impl_->monsterIdentities.end()) {
        if (identity->second.champion) titleColor = {105,105,255,255};
        else if (identity->second.unique || identity->second.superUnique) titleColor = {199,179,119,255};
    }
    std::optional<float> life;
    if (unit.lifePercent) {
        // MonsterMode uses 0..128; hit updates alone reserve bit 7 for a trigger.
        const unsigned raw = *unit.lifePercent;
        life = float(unit.lifeCarriesTriggerFlag ? raw & 0x7f : raw) / 128.f;
    }
    shared.drawEnemyBar(name, life, description, titleColor);
}
int RemoteScene::renderedUnits() const {
    return impl_->rendered;
}
int RemoteScene::unavailableUnits() const {
    return impl_->unavailable;
}
bool RemoteScene::playerDisplayed() const {
    return impl_->playerDisplayed;
}
std::optional<Vec> RemoteScene::playerDisplayPosition(const OnlineView &view) const {
    if (impl_->gameGeneration != view.gameGeneration || impl_->areaGeneration != view.world.areaGeneration ||
        impl_->playerId != view.load.playerUnitId) return {};
    if (!impl_->playerId || !impl_->playerDisplayed) return {};
    const auto found = impl_->motion.find({0, *impl_->playerId});
    if (found == impl_->motion.end()) return {};
    const auto actor = view.world.units.find({0, *impl_->playerId});
    if (actor == view.world.units.end() || !actor->second.position ||
        found->second.assignmentRevision != actor->second.assignmentRevision ||
        found->second.discontinuity != actor->second.positionDiscontinuity) return {};
    return found->second.position + found->second.correction;
}
const std::vector<OnlinePlayerDisplay> &RemoteScene::players() const { return impl_->players; }
std::vector<std::string> RemoteScene::effectLimitations() const {
    return {impl_->effectLimitations.begin(), impl_->effectLimitations.end()};
}
void RemoteScene::effectStatus(OnlineSceneView &view) const {
    view.monsters = impl_->monsters;
    view.clientMissiles = impl_->clientMissiles;
    view.localCastSkill = impl_->localCast ? std::optional{impl_->localCast->command.skill} : std::nullopt;
    view.localCastAge = impl_->localCast && impl_->localCast->started >= 0 ? impl_->time - impl_->localCast->started : -1;
}
} // namespace d2x
