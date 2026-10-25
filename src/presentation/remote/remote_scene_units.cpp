#include "content/monsters/monster_identity.hpp"
#include "remote_scene_impl.hpp"

namespace d2x {

const RemoteScene::Impl::Art *RemoteScene::Impl::character(const OnlineUnit &u, bool moving, bool town, SceneView &shared) {
    // PlrMsg::sub_6FC81C00 uses wire 19 for correction, not PLRMODE_DEAD.
    const bool death = playerDead(u);
    const auto parts = portraits.decode(u, world(), death);
    if (!parts) {
        effectLimitations.insert("Original character equipment components unavailable for unit " + std::to_string(u.key.id));
        return nullptr;
    }
    if (!parts->equipmentEffectsKnown)
        effectLimitations.insert("Per-component equipment coloring and ethereal transparency are not implemented; original body components remain visible");
    ActorAnimationRequest request; request.category = "chars"; request.appearance = *parts; request.shadow = true;
    if(const auto shift=stateColorShifts.find(u.key);shift!=stateColorShifts.end()) request.stateColorShift=shift->second;
    if (u.key.type==0 && u.key.id==playerId) request.fasterCast=shared.characterView().fasterCast;
    if (u.key.type==0 && u.key.id==playerId) request.attackTiming=shared.characterView().attackTiming;
    if (u.key.type==0 && u.key.id==playerId) request.recoveryTiming=shared.characterView().recoveryTiming;
    std::string mode;
    if (u.actionSkill) {
        const auto row = skillRows.find(*u.actionSkill);
        if (row == skillRows.end()) return nullptr;
        mode = lower(std::string(skills.value(row->second, "anim")));
        request.skillUsesAttackRate=skills.number(row->second,"UseAttackRate").value_or(0)!=0;
        if((skills.number(row->second,"cltdofunc")==4 && skills.value(row->second,"charclass")=="ass") ||
           (skills.number(row->second,"cltstfunc")==53 && skills.value(row->second,"charclass")=="dru")) {
            const auto *known=shared.characterView().skill(*u.actionSkill);
            const int rank=u.actionSkillLevel.value_or(u.key.id==playerId && known && known->effectiveRankKnown?known->effectiveRank:1);
            request.repeatCount=std::max(1,skillCalculation(*u.actionSkill,rank,"calc1",u.key).value_or(1));
            request.rollbackPercent=skills.value(row->second,"charclass")=="dru"?skills.number(row->second,"Param2").value_or(0):100;
        }
        if(skills.number(row->second,"cltstfunc")==9 && skills.value(row->second,"charclass")=="ass" && request.attackTiming)
            request.attackTiming->skillRate+=skills.number(row->second,"Param4").value_or(0);
        if(skills.number(row->second,"seqnum")==23 && (channels.contains(u.key) ||
           (u.key.type==0 && u.key.id==playerId && localCast && localCast->command.skill==*u.actionSkill))) request.animationHoldFrame=12;
        if (mode == "sq") {
            // D2Common SequenceTbls: player sequence 12 is independent of weapon
            // class, and samples the original SC frames with a release at step 7.
            const auto sequence=skills.number(row->second,"seqnum").value_or(-1);
            if(sequence==4 && moving) {request.mode="rn";return shared.actorAnimation(request,artPalette);}
            if(pd2 && sequence==4) if(const auto &charge=motion[u.key].pd2PlayerCharge;charge && charge->attackAt>=0)
                request.animationRate=charge->attackSpeed;
            if((sequence==12 && skills.value(row->second,"seqtrans")!="SC") ||
               (sequence==6 && skills.value(row->second,"seqtrans")!="SQ") || (sequence!=12 && sequence!=6 && sequence!=18 && sequence!=1 && sequence!=8 && sequence!=4 && sequence!=13 && sequence!=14 && sequence!=11 && sequence!=15 && sequence!=10 && sequence!=16 && sequence!=21 && sequence!=23)) return nullptr;
            if(sequence==13 || sequence==14) request.sequenceLanding=motion[u.key].leapLanding>=0;
            if(sequence==11 && skills.value(row->second,"cltcalc1")=="par5") request.sequenceRateBonus=skills.number(row->second,"Param5").value_or(0);
            request.mode = "sc"; request.playerSequence = sequence;
            return shared.actorAnimation(request, artPalette);
        }
        if(skills.number(row->second,"cltdofunc")==20) {
            int count=0;
            const int radius=skills.number(row->second,"Param5").value_or(0);
            for(const auto &[key,target]:world().units) if(key.type==1 && target.position && combatBinding && combatBinding->hostileSource(target) && target.mode!=0 && target.mode!=12 && missileDistance({float(u.position->x),float(u.position->y)},{float(target.position->x),float(target.position->y)})<=radius) ++count;
            const auto *known=shared.characterView().skill(*u.actionSkill);
            const int rank=u.actionSkillLevel.value_or(u.key.type==0 && u.key.id==playerId && known && known->effectiveRankKnown?known->effectiveRank:1);
            request.repeatCount=std::max(1,strafeShotCount(count,std::min(skills.number(row->second,"Param4").value_or(0),skills.number(row->second,"Param3").value_or(0)+rank-1),2+rank/4));
            request.rollbackPercent=skills.number(row->second,"Param6").value_or(0);
        }
        if(skills.number(row->second,"cltdofunc")==21 && u.position) {
            std::optional<int> reach;
            for(const auto &[id,item]:world().equipment) {
                (void)id;if(item.ownerType!=u.key.type || item.owner!=u.key.id || item.bodyLocation!=4) continue;
                for(size_t weapon=0;weapon<weapons.rows().size();++weapon) if(weapons.value(weapon,"code")==item.code) reach=weapons.number(weapon,"rangeadder").value_or(0)+1;
            }
            if(!reach) return nullptr;
            int count=0;
            for(const auto &[key,target]:world().units) if(key.type==1 && target.position && combatBinding && combatBinding->hostileSource(target) && target.mode!=0 && target.mode!=12) {
                const auto monster=monsterRows.find(unitClass(target).value_or(UINT16_MAX));if(monster==monsterRows.end()) continue;
                const auto extra=monsterExtra.find(monstats.value(monster->second,"MonStatsEx"));if(extra==monsterExtra.end()) continue;
                if(meleeDistance({float(u.position->x),float(u.position->y)},2,{float(target.position->x),float(target.position->y)},monstats2.number(extra->second,"SizeX").value_or(0))<=*reach) ++count;
            }
            if(skills.number(row->second,"srvdofunc")==13) {
                const auto *known=shared.characterView().skill(*u.actionSkill);
                const int rank=u.actionSkillLevel.value_or(u.key.id==playerId && known && known->effectiveRankKnown?known->effectiveRank:1);
                request.repeatCount=std::max(1,std::min(skills.number(row->second,"Param6").value_or(0),skills.number(row->second,"Param5").value_or(0)+rank-1));
            } else request.repeatCount=std::max(1,std::min(count,skills.number(row->second,"calc1").value_or(0)));
            request.rollbackPercent=skills.number(row->second,"Param2").value_or(0);
        }
        if (mode.empty()) return nullptr;
    } else if (u.nativeMode) {
        constexpr std::array modes{"dt", "nu", "wl", "rn", "gh", "tn", "tw", "a1", "a2",
                                   "bl", "sc", "th", "kk", "s1", "s2", "s3", "s4", "dd", "sq", "kb"};
        if (!u.mode || *u.mode >= modes.size() || *u.mode == 18) return nullptr;
        mode = modes[*u.mode];
    } else if (u.mode == 8)
        mode = "dt";
    else if (u.mode == 9) mode = "dd";
    else if (u.mode == 6) mode = "gh";
    else if (u.mode == 0x12) mode = "bl";
    // PlrMsg's PLRMODE_KNOCKBACK maps to wire 0x14, not KK.
    else if (u.mode == 20) mode = "kb";
    else if (moving) {
        // Player wire action bytes differ from PLRMODE. PlrMsg's native table:
        // 0F: 01 walk / 17 run; 10: 00 walk / 18 run. A presentation path
        // can start from an enqueued own request without advancing the replica.
        const bool requestedRun = motion.at(u.key).running;
        const bool own = u.key.type == 0 && u.key.id == playerId;
        mode = u.mode == 0x17 || u.mode == 0x18 || (own && requestedRun)
            ? "rn" : town ? "tw" : "wl";
    } else if (!u.mode || *u.mode == 7 || *u.mode == 19 || *u.mode == 1 || *u.mode == 0 || *u.mode == 0x17 || *u.mode == 0x18)
        mode = town ? "tn" : "nu";
    else
        return nullptr;
    request.mode = mode;
    if(u.key.type==0 && (mode=="wl" || mode=="tw" || mode=="rn"))
        if(const auto &timing=motion[u.key].movementTiming) request.animationRate=timing->animationRate;
    if(combatBinding) if(const auto snapshot=combatBinding->states().find(u.key);snapshot!=combatBinding->states().end() && snapshot->second.decoded)
        for(const auto &[state,entry]:snapshot->second.states) {
            (void)entry;const auto stateRow=stateRows.find(state);
            if(stateRow==stateRows.end() || !states.number(stateRow->second,"transform").value_or(0) || states.number(stateRow->second,"gfxtype")!=1) continue;
            const auto identity=states.number(stateRow->second,"gfxclass");if(!identity) return nullptr;
            const auto monster=monsterRows.find(*identity);if(monster==monsterRows.end()) return nullptr;
            const auto extra=monsterExtra.find(monstats.value(monster->second,"MonStatsEx"));if(extra==monsterExtra.end()) return nullptr;
            constexpr std::array<std::string_view,20> playerModes{"dt","nu","wl","rn","gh","tn","tw","a1","a2","bl","sc","th","kk","s1","s2","s3","s4","dd","sq","kb"};
            constexpr std::array<std::string_view,16> monsterModes{"dt","nu","wl","gh","a1","a2","bl","sc","s1","s2","s3","s4","dd","kb","sq","rn"};
            const auto index=std::find(playerModes.begin(),playerModes.end(),mode);if(index==playerModes.end()) return nullptr;
            uint16_t enabled=0;for(size_t native=0;native<monsterModes.size();++native)
                if(monstats2.number(extra->second,"m"+std::string(monsterModes[native])).value_or(0)) enabled|=uint16_t{1}<<native;
            const auto native=shapeMonsterMode(uint8_t(index-playerModes.begin()),enabled);
            request.shapeSource=*parts;request.category="monsters";request.appearance={};
            request.appearance.token=lower(std::string(monstats.value(monster->second,"Code")));
            request.mode=monsterModes[native];request.appearance.weapon=monsterModeWeapon(archives,request.appearance.token,request.mode,monstats2.value(extra->second,"BaseW"));
            for(size_t component=0;component<request.appearance.components.size();++component) {
                const auto choices=variants(monstats2.value(extra->second,std::string(componentCodes[component])+"v"));
                request.appearance.components[component]=monstats2.number(extra->second,componentCodes[component]).value_or(0) && !choices.empty()?choices.front():"nil";
            }
            request.playerSequence=-1;break;
        }
    return shared.actorAnimation(request, artPalette);
}

std::string RemoteScene::Impl::monsterSkillMode(size_t monster, uint16_t skill) const {
    const auto row=skillRows.find(skill);
    if (row==skillRows.end()) return {};
    if(skills.value(row->second,"skill")=="Baal Inferno") return "s1";
    if(monstats.value(monster,"AI")=="Imp" && skills.number(row->second,"cltdofunc")==71) return "sc";
    // D2Common SKILLS_GetSeqNumFromSkill resolves the owning monster's slot,
    // not Skills.seqnum. The same Nest skill has different crow/Blood Raven sequences.
    // Native NAMETOWORD links are case-insensitive: current MonStats BIN
    // infernosentry +170 is skill 311 despite "Mon"/"mon" in the TXT names.
    const auto name=lower(std::string(skills.value(row->second,"skill")));
    for (int slot=1; slot<=8; ++slot)
        if (lower(std::string(monstats.value(monster,"Skill"+std::to_string(slot))))==name)
            return lower(std::string(monstats.value(monster,"Sk"+std::to_string(slot)+"mode")));
    // Hireling skills use Hireling.ModeN; their player Skills.monanim is
    // intentionally "xx". MonStats still takes precedence for Jab's sequence.
    const auto cls=compiledMonsterIndex(monstats,monster);
    if(const auto mode=hirelingSkillModes.find({cls,int(skill)});mode!=hirelingSkillModes.end()) return mode->second;
    return lower(std::string(skills.value(row->second,"monanim")));
}

std::string RemoteScene::Impl::monsterAutomapResist(const OnlineUnit &unit) const {
    // BH OnAutomapDraw immunity line: PL2 text-color digit plus letter,
    // immune at 100+, resistant at the 99 threshold (PD2 show_monster_resistance).
    const auto row = monsterRows.find(unitClass(unit).value_or(UINT16_MAX));
    if (row == monsterRows.end()) return {};
    const std::string suffix = worldDifficulty==1?"(N)":worldDifficulty==2?"(H)":"";
    const auto res = [&](const char *field) {
        return monstats.number(row->second, std::string(field) + suffix).value_or(0);
    };
    constexpr std::array<std::pair<const char *, char>, 6> fields{
        {{"ResDm", '7'}, {"ResMa", '8'}, {"ResFi", '1'}, {"ResLi", '9'}, {"ResCo", '3'}, {"ResPo", '2'}}};
    std::string text;
    for (const auto &[field, code] : fields) {
        const int value = res(field);
        if (value >= 100) { text.push_back(code); text.push_back('i'); }
        else if (value >= 99) { text.push_back(code); text.push_back('r'); }
    }
    return text;
}

std::string RemoteScene::Impl::monsterAutomapEnchant(const OnlineUnit &unit) const {
    // BH "Monster Enchantments": boss-only letters using the native ENCH ids
    // (25 mana burn, 9 fire, 17 lightning, 18 cold).
    const auto identity = monsterIdentities.find(unit.key);
    if (identity == monsterIdentities.end()) return {};
    if (!identity->second.unique && !identity->second.superUnique) return {};
    const auto has = [&](uint8_t id) {
        return std::find(identity->second.modifiers.begin(), identity->second.modifiers.end(), id) !=
            identity->second.modifiers.end();
    };
    std::string text;
    if (has(25)) text += "3m";
    if (has(9)) text += "1e";
    if (has(17)) text += "9e";
    if (has(18)) text += "3e";
    return text;
}

std::pair<std::string,std::string> RemoteScene::Impl::monsterCaption(const OnlineUnit &unit, bool includeResistances) const {
    const auto row=monsterRows.find(unitClass(unit).value_or(UINT16_MAX));
    if(row==monsterRows.end()) return {};
    // Nameless placeholder rows resolve empty and produce no caption;
    // superunique ranks still rename from SuperUniques.Name above.
    const std::string species(strings.findDisplayName(monstats.value(row->second,"NameStr")));
    std::string name=species,description;
    if(const auto identity=monsterIdentities.find(unit.key);identity!=monsterIdentities.end()) {
        const auto &native=identity->second;
        d2x::MonsterIdentity publicIdentity;
        publicIdentity.rank=native.champion?MonsterRank::Champion:native.superUnique?MonsterRank::SuperUnique:
            native.unique?MonsterRank::Unique:native.minion?MonsterRank::Minion:MonsterRank::Normal;
        MonsterEnchantment modifiers; modifiers.nameSeed=native.nameSeed;
        modifiers.ids.assign(native.modifiers.begin(),native.modifiers.end());
        if(native.superUnique) {
            name=monsterDisplayName(*monsterText,publicIdentity,name,&modifiers);
            description=monsterModifierDescription(*monsterText,publicIdentity,&modifiers);
            if(const auto fixed=superUniqueRows.find(*native.superUnique);fixed!=superUniqueRows.end())
                name=strings.find(superuniques.value(fixed->second,"Name"));
        } else if(!species.empty()) {
            name=monsterDisplayName(*monsterText,publicIdentity,name,&modifiers);
            description=monsterModifierDescription(*monsterText,publicIdentity,&modifiers);
        } else return {};
    }
    // BH monster_resistances: append base resistances from MonStats with the
    // difficulty suffix, matching monster_difficulty_combat.cpp.
    if (includeResistances) {
        const std::string suffix = worldDifficulty==1?"(N)":worldDifficulty==2?"(H)":"";
        const auto res = [&](const char *field) {
            return monstats.number(row->second, std::string(field) + suffix).value_or(0);
        };
        const int rd = res("ResDm"), rm = res("ResMa"), rf = res("ResFi"),
                  rl = res("ResLi"), rc = res("ResCo"), rp = res("ResPo");
        auto append = [&](std::string &target, const char *label, int value) {
            if (value == 0) return;
            if (!target.empty()) target += ' ';
            target += label;
            target += std::to_string(value);
        };
        append(description, "Ph:", rd);
        append(description, "Ma:", rm);
        append(description, "Fi:", rf);
        append(description, "Li:", rl);
        append(description, "Co:", rc);
        append(description, "Po:", rp);
    }
    return {std::move(name),std::move(description)};
}

std::optional<std::pair<uint8_t, RemoteScene::Impl::MonsterIdentity>>
RemoteScene::Impl::readMonsterAppearance(const OnlineUnit &u, size_t extraRow, RealmPortraitParts &parts) const {
    try {
        net::protocol::BitReader bits(u.appearanceBits);
        const auto mode = uint8_t(bits.read(4));
        const bool components = bits.read(1) != 0;
        for (size_t c = 0; c < parts.components.size(); ++c) {
            auto values = variants(monstats2.value(extraRow, std::string(componentCodes[c]) + "v"));
            const auto selected =
                components
                    ? bits.read(values.size() >= 3 ? std::bit_width(unsigned(values.size() - 1)) : 1)
                    : 0;
            if (!monstats2.number(extraRow, componentCodes[c]).value_or(0)) {
                parts.components[c] = "nil";
                continue;
            }
            // MonStats2CompositLinker leaves an empty variant list at count zero;
            // D2Common_11069 returns no component code for it. Warriv/Charsi
            // enable S1 but leave S1v empty: omit that layer, not the whole NPC.
            if (values.empty()) {
                if (selected) return std::nullopt;
                parts.components[c] = "nil";
                continue;
            }
            if (selected >= values.size())
                return std::nullopt;
            parts.components[c] = values[selected];
        }
        // SCmd::sub_6FC3FC80: rank/mercenary flags do not invalidate the components.
        // Fixed superuniques use the original identity/name/difficulty palette.
        MonsterIdentity identity;
        if (bits.read(1)) {
            identity.champion=bits.read(1)!=0; identity.unique=bits.read(1)!=0;
            const bool superUnique = bits.read(1) != 0;
            identity.minion=bits.read(1)!=0; identity.ghostly=bits.read(1)!=0;
            if (superUnique) identity.superUnique=uint16_t(bits.read(16));
            bool terminated = false;
            for (int i = 0; i < 9; ++i) {
                const auto modifier=uint8_t(bits.read(8));
                if (!modifier) { terminated=true; break; }
                identity.modifiers.push_back(modifier);
            }
            if (!terminated && bits.read(8) != 0) return std::nullopt;
            identity.nameSeed=uint16_t(bits.read(16));
            if (bits.read(1)) bits.read(32);
        }
        return std::pair{mode, std::move(identity)};
    } catch (const net::protocol::ProtocolError &) {
        return std::nullopt;
    }
}

void RemoteScene::Impl::decodeMonsterIdentity(const OnlineUnit &u) {
    // Appearance decoding otherwise runs only for drawn monsters; the BH
    // automap needs the rank flags of every known unit in the area.
    if (monsterIdentities.contains(u.key)) return;
    const auto cls = unitClass(u);
    if (!cls) return;
    const auto row = monsterRows.find(*cls);
    if (row == monsterRows.end()) return;
    const auto extra = monsterExtra.find(monstats.value(row->second, "MonStatsEx"));
    if (extra == monsterExtra.end()) return;
    RealmPortraitParts parts;
    if (const auto appearance = readMonsterAppearance(u, extra->second, parts))
        monsterIdentities[u.key] = appearance->second;
}

const RemoteScene::Impl::Art *RemoteScene::Impl::monster(const OnlineUnit &u, bool moving, SceneView &shared) {
    // D2Common SKILLS_GetGfxType/GetGfxClass: States.gfxtype=2 uses
    // player components on a monster unit (Decoy and Valkyrie included).
    if(combatBinding) if(const auto snapshot=combatBinding->states().find(u.key);snapshot!=combatBinding->states().end() && snapshot->second.decoded)
        for(const auto &[state,entry]:snapshot->second.states) {
            (void)entry;const auto row=stateRows.find(state);
            if(row==stateRows.end() || states.number(row->second,"gfxtype")!=2) continue;
            const auto cls=states.number(row->second,"gfxclass");if(!cls || *cls<0 || *cls>6) return nullptr;
            const auto parts=portraits.decode(u,world(),false,uint8_t(*cls));if(!parts) return nullptr;
            constexpr std::array modes{"dt","nu","wl","gh","a1","a2","bl","sc","s1","s2","s3","s4","dd","kb","sq","rn"};
            const auto mode=u.mode.value_or(1);if(mode>=modes.size()) return nullptr;
            ActorAnimationRequest request;request.category="chars";request.appearance=*parts;request.shadow=true;
            if(const auto shift=stateColorShifts.find(u.key);shift!=stateColorShifts.end()) request.stateColorShift=shift->second;
            request.monsterUnit=true;
            request.monsterKick=*cls==6;
            request.mode=moving && mode==1?"wl":modes[mode];request.finalFrame=mode==12;
            if(request.monsterKick && request.mode=="a1") request.mode="kk";
            if(u.actionSkill && mode!=0 && mode!=3 && mode!=6 && mode!=12 && mode!=13) {
                const auto monsterRow=monsterRows.find(unitClass(u).value_or(UINT16_MAX));
                if(monsterRow!=monsterRows.end()) request.mode=monsterSkillMode(monsterRow->second,*u.actionSkill);
                if(request.monsterKick && request.mode=="a1") request.mode="kk";
                if(const auto skill=skillRows.find(*u.actionSkill);skill!=skillRows.end()) {
                    request.sequenceUsesAttackRate=skills.number(skill->second,"UseAttackRate").value_or(0)!=0;
                    if(skills.number(skill->second,"cltdofunc")==4 && skills.value(skill->second,"charclass")=="ass") {
                        request.repeatCount=std::max(1,skillCalculation(*u.actionSkill,u.actionSkillLevel.value_or(1),"calc1",u.key).value_or(1));
                        request.rollbackPercent=100;
                    }
                }
            }
            if(mode==12) request.mode="dt";
            // D2Common COMPOSIT_GetWeaponClassCode: player death uses HTH,
            // including monsters converted to player graphics by States.
            if(mode==0 || mode==12) request.appearance.weapon="hth";
            return shared.actorAnimation(request,artPalette);
        }
    if (!unitClass(u))
        return nullptr;
    const auto row = monsterRows.find(*unitClass(u));
    if (row == monsterRows.end())
        return nullptr;
    const auto extra = monsterExtra.find(monstats.value(row->second, "MonStatsEx"));
    if (extra == monsterExtra.end())
        return nullptr;
    const auto e = extra->second;
    RealmPortraitParts parts;
    parts.token = lower(std::string(monstats.value(row->second, "Code")));
    if (parts.token.empty())
        return nullptr;
    const auto appearance = readMonsterAppearance(u, e, parts);
    if (!appearance) return nullptr;
    const auto mode = appearance->first;
    const auto &identity = appearance->second;
    monsterIdentities[u.key]=identity;
    try {
        int palette=monstats.number(row->second,"TransLvl").value_or(0);
        bool randomPalette=false;
        // D2Client 1.13c A7A5A / 539E0: native transform numbers include
        // the two leading palshift rows; unique RNG is class + name seed.
        int nativeTransform = palette < 8 ? palette + 2 : 2;
        if (identity.unique && std::ranges::find(identity.modifiers,uint8_t{15})==identity.modifiers.end()) {
            uint64_t random = initialRandom(uint32_t(*unitClass(u)) + identity.nameSeed);
            nativeTransform = 9 + int(limitedRandom(random,30));
        }
        const auto suffix=worldDifficulty==1?"(N)":worldDifficulty==2?"(H)":"";
        const int configured=monstats2.number(e,"Utrans"+std::string(suffix)).value_or(0);
        if (configured && configured!=255) nativeTransform=configured;
        // Utrans 255 is the native pet-owner hostility transform.
        if (configured==255) {
            nativeTransform=combatBinding?combatBinding->ownershipTransform(u):0;
        }
        if (identity.superUnique) {
            const auto fixed=superUniqueRows.find(*identity.superUnique);
            if (fixed!=superUniqueRows.end()) {
                const int fixedTransform=superuniques.number(fixed->second,"Utrans"+std::string(suffix)).value_or(0);
                if(fixedTransform) nativeTransform=fixedTransform;
            }
        }
        // A7B1A retains the original upper bound, including RNG fallback.
        if (nativeTransform>=30) nativeTransform=2;
        randomPalette=nativeTransform>=8;
        palette=nativeTransform;
        monsterTransforms[u.key]={palette,randomPalette};
        const auto actual = u.mode.value_or(uint8_t(mode));
        constexpr std::array modes{"dt", "nu", "wl", "gh", "a1", "a2", "bl", "sc",
                                   "s1", "s2", "s3", "s4", "dd", "kb", "sq", "rn"};
        if (actual >= modes.size())
            return nullptr;
        const bool walkingPose = moving && (actual == 1 || actual == 2 || actual == 15);
        std::string pose = walkingPose ? (actual == 15 ? "rn" : "wl") : modes[actual];
        if(actual==0 && monstats.value(row->second,"AI")=="SandMaggotQueen") {
            for(int slot=1;slot<=8;++slot) if(monstats.value(row->second,"Skill"+std::to_string(slot))=="QueenDeath") pose=lower(std::string(monstats.value(row->second,"Sk"+std::to_string(slot)+"mode")));
        }
        const bool burrowed=actual==8 && !u.actionSkill && monstats.value(row->second,"AI")=="SandMaggot";
        const bool submerged=actual==8 && !u.actionSkill && monstats.value(row->second,"AI")=="FrogDemon";
        if(burrowed) for(int slot=1;slot<=8;++slot) if(monstats.value(row->second,"Skill"+std::to_string(slot))=="MagottDown") pose=lower(std::string(monstats.value(row->second,"Sk"+std::to_string(slot)+"mode")));
        if(submerged) for(int slot=1;slot<=8;++slot) if(monstats.value(row->second,"Skill"+std::to_string(slot))=="Submerge") pose=lower(std::string(monstats.value(row->second,"Sk"+std::to_string(slot)+"mode")));
        // D2Common COMPOSIT maps MONMODE_KNOCKBACK to the original GH art.
        if(actual==13) pose="gh";
        if (u.actionSkill || pose.starts_with("seq_")) {
            if(u.actionSkill) pose = monsterSkillMode(row->second,*u.actionSkill);
            if (pose.starts_with("seq_")) {
                ActorAnimationRequest request; request.category = "monsters"; request.mode = pose;
                if(const auto &hireling=shared.hirelingView();hireling.id.value==u.key.id) {
                    request.attackTiming=hireling.attackTiming;request.recoveryTiming=hireling.recoveryTiming;request.fasterCast=hireling.fasterCast;
                }
                request.finalFrame=burrowed;
                if(submerged) {request.sequenceStart=0;request.sequenceEnd=1;request.persistentCycle=true;}
                if(u.actionSkill) {
                    const auto skill=skillRows.find(*u.actionSkill);
                    if(skill!=skillRows.end()) {
                        request.sequenceUsesAttackRate=skills.number(skill->second,"UseAttackRate").value_or(0)!=0;
                        const int clientProgram=skills.number(skill->second,"cltdofunc").value_or(0);
                        if(clientProgram==46 || (pd2 && clientProgram==109)) {
                            // Retail 56780 clears dwSeqSpeed at the action
                            // event. Current PD2 3A9710 calls it after Nova.
                            // The next server action selects a new request;
                            // never replace the hatched egg with an idle pose.
                            if(const auto flags=monsterSequenceFlags.find(pose);flags!=monsterSequenceFlags.end()) {
                                const auto event=std::find_if(flags->second.begin(),flags->second.end(),
                                    [](uint8_t flag){return flag==1 || flag==2 || flag==4;});
                                if(event!=flags->second.end()) request.animationHoldFrame=int(event-flags->second.begin());
                            }
                        }
                        const auto name=skills.value(skill->second,"skill");
                        if(name=="Charge" || name=="SerpentCharge") {
                            const bool travelling=motion[u.key].leapFlying && motion[u.key].leapLanding<0;
                            // Charge's native server rewind bounds are 7;
                            // Duriel initializes its movement bound to 15.
                            const int boundary=monstats.value(row->second,"AI")=="Duriel"?16:8;
                            if(travelling) {request.sequenceEnd=boundary;request.persistentCycle=true;}
                            else request.sequenceStart=boundary;
                        } else if(name=="Swarm Move" && moving) request.persistentCycle=true;
                    }
                }
                parts.weapon = lower(std::string(monstats2.value(e, "BaseW"))); request.appearance = parts;
                request.paletteTransform = palette; request.randomTransform = randomPalette;
                request.nativeMonsterTransform = true;
                if(const auto shift=stateColorShifts.find(u.key);shift!=stateColorShifts.end()) request.stateColorShift=shift->second;
                request.shadow = monstats2.number(e, "Shadow").value_or(0) != 0;
                const auto *sequence = shared.actorAnimation(request, artPalette);
                if (!sequence) effectLimitations.insert("Monster sequence unavailable: "+std::string(monstats.value(row->second,"Id"))+":"+pose);
                return sequence;
            }
            if (pose.empty() || pose == "sq") return nullptr;
        }
        parts.weapon = monsterModeWeapon(archives, parts.token, pose, monstats2.value(e, "BaseW"));
        const bool deadFrame = pose == "dd" && parts.weapon.empty();
        if (deadFrame) {
            pose = "dt";
            parts.weapon = monsterModeWeapon(archives, parts.token, pose, monstats2.value(e,"BaseW"));
        }
        if (parts.weapon.empty())
            return nullptr;
        ActorAnimationRequest request; request.category = "monsters"; request.mode = pose;
        if(monstats.value(row->second,"AI")=="Imp") if(const auto channel=channels.find(u.key);channel!=channels.end() && time>=channel->second.releaseAt &&
            skills.number(channel->second.skillRow,"cltdofunc")==71) request.animationHoldFrame=monstats2.number(e,"InfernoAnim").value_or(0);
        if(const auto channel=channels.find(u.key);channel!=channels.end() && time>=channel->second.releaseAt &&
            skills.number(channel->second.skillRow,"cltdofunc")==56) request.animationHoldFrame=std::max(0,monstats2.number(e,"InfernoAnim").value_or(0)-1);
        if(const auto &hireling=shared.hirelingView();hireling.id.value==u.key.id) {
            request.attackTiming=hireling.attackTiming;request.recoveryTiming=hireling.recoveryTiming;request.fasterCast=hireling.fasterCast;
        }
        request.appearance = parts; request.shadow = monstats2.number(e, "Shadow").value_or(0) != 0;
        request.paletteTransform = palette; request.finalFrame = deadFrame; request.randomTransform = randomPalette;
        request.nativeMonsterTransform = true;
        if(const auto shift=stateColorShifts.find(u.key);shift!=stateColorShifts.end()) request.stateColorShift=shift->second;
        request.persistentCycle=(actual==8 && monstats.value(row->second,"AI")=="Vulture") || (actual==11 && monstats.value(row->second,"AI")=="BatDemon");
        if(actual==13) {
            const auto rate=monsterWalkRates.find(*unitClass(u));
            if(rate==monsterWalkRates.end() || rate->second<=0) return nullptr;
            request.animationRate=rate->second;
        } else if(walkingPose) {
            const auto &rates=actual==15?monsterRunRates:monsterWalkRates;
            const auto rate=rates.find(*unitClass(u));
            if(rate!=rates.end()) request.animationRate=rate->second*std::max(25,int(u.velocityPercent.value_or(100)))/100;
        }
        // Do95's client mode callback rewinds the normal graphics frame
        // before advancing it. A MonStats sequence keeps its own cursor.
        if(const auto channel=channels.find(u.key);channel!=channels.end() &&
            skills.number(channel->second.skillRow,"cltdofunc")==54 && !pose.starts_with("seq_")) {
            if(const auto *base=shared.actorAnimation(request,artPalette);base && base->fps>0) {
                const auto movement=motion.find(u.key);
                const int ticks=movement==motion.end()?0:int(std::floor(movement->second.animation.elapsed(time)*25.f+1e-5f));
                const int speed=std::max(1,int(std::round(base->fps*256.f/25.f)));
                const int threshold=monstats2.number(e,"InfernoAnim").value_or(0),rollback=monstats2.number(e,"InfernoRollback").value_or(0);
                const int crossing=((threshold+1)*256+speed-1)/speed;
                if(ticks>crossing) {
                    const int period=std::max(1,((threshold+1-rollback)*256+speed-1)/speed);
                    const int frame=(rollback*256+((ticks-crossing-1)%period+1)*speed)>>8;
                    request.animationHoldFrame=std::clamp(frame,0,base->frameCount-1);
                }
            }
        }
        return shared.actorAnimation(request, artPalette);
    } catch (const net::protocol::ProtocolError &) {
        return nullptr;
    }
}

const RemoteScene::Impl::Art *RemoteScene::Impl::object(const OnlineUnit &u, SceneView &shared) {
    return unitClass(u) && u.mode ? shared.objectAnimation(*unitClass(u), *u.mode, artPalette) : nullptr;
}
} // namespace d2x
