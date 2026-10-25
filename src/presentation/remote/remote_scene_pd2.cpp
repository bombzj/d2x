#include "remote_scene_impl.hpp"
#include "gameplay/skills/cast_timing.hpp"
#include "gameplay/actions/timing.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/skills/targeting.hpp"
#include "network/protocol/bits.hpp"
#include "content/pd2/map_description.hpp"
#include "gameplay/stats/stat_list.hpp"
#include "core/random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <chrono>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace d2x {
std::optional<int32_t> RemoteScene::Impl::pd2ReplicaFormulaStat(OnlineUnitKey owner,std::string_view name) const {
    if(!pd2) return {};
    if(owner.type==0) {
        if(!uiClients) return {};
        if(!playerId || owner.id!=*playerId) return uiClients->pd2ObservedStat(owner,name);
        const auto &values=uiClients->skillFormulaStats();const auto value=values.find(name);
        if(value==values.end()) return {};
        for(const auto &stat:monsterText->itemStats) if(stat.name==name && stat.valueShift>=0 && stat.valueShift<=15) {
            const auto raw=value->second*double(1<<stat.valueShift);
            if(std::isfinite(raw) && raw>=INT32_MIN && raw<=INT32_MAX) return int32_t(raw);
        }
        return {};
    }
    // These PD2 extension stats have no retail monster constructor value.
    // The original client queries its own property/state lists, not the pet
    // owner's private equipment or the server's hidden monster inventory.
    if(owner.type!=1 || !combatBinding ||
       (name!="item_leap_speed" && name!="extra_bonespears" && name!="extra_holybolts")) return {};
    const auto unit=world().units.find(owner);if(unit==world().units.end()) return {};
    const auto properties=combatBinding->monsterPropertyStats(unit->second);
    const auto states=combatBinding->states().find(owner);
    if(!properties || states==combatBinding->states().end() || !states->second.decoded ||
       states->second.sequence!=unit->second.stateSequence) return {};
    const auto stat=std::find_if(monsterText->itemStats.begin(),monsterText->itemStats.end(),[&](const auto &entry){return entry.name==name;});
    if(stat==monsterText->itemStats.end() || !stat->id) return {};
    int32_t value=0;
    const auto add=[&](const auto &list) {for(const auto &entry:list) if(entry.id==*stat->id && !entry.parameter) value=nativeAdd(value,nativeNarrow(entry.value));};
    add(*properties);for(const auto &[id,list]:states->second.states) {(void)id;add(list.stats);}
    return value;
}
std::optional<RemoteScene::Impl::Pd2RoyalFire> RemoteScene::Impl::preparePd2RoyalFire(
    size_t row,const OnlineCombatEvent &event,const OnlineUnit &actor) {
    // Current 3A8BE0 delegates to retail 40F00 only for Skills.CltDo83.
    // Its progressive callback remains the separate retail delegate.
    const auto missile=missileNames.find(skills.value(row,"cltmissilea"));
    const auto step=skillCalculation(*event.skill,event.level.value_or(1),"calc1",event.source);
    if(missile==missileNames.end() || !step || *step<=0) {
        effectLimitations.insert("PD2 Royal Fire missile or positive step unavailable");return {};
    }
    const auto identity=unitClass(actor);
    if(actor.key.type==1 && !identity) return {};
    const bool arenaOrigin=actor.key.type==1 && identity && *identity==1113;
    if(!actor.position) return {};
    const auto seed=nextPd2PathSeed(event.source);
    auto &random=pd2VisualSeeds.at(event.source).random;
    random=(random&0xffffffff00000000ULL)|seed; // Only unit seed's low DWORD is overwritten.
    // These coordinates are the current DLL's class1113 arena branch,
    // not inferred map dimensions or fallback locations for other actors.
    const Vec center=arenaOrigin?Vec{float((seed+13u)%40u+0x620fu)+.5f,float((seed+19u)%40u+0x17cdu)+.5f}:
        Vec{float(actor.position->x)+.5f,float(actor.position->y)+.5f};
    // 40F00 reads Skills+14C (Param2), then writes Param2+1 to TargetX.
    return Pd2RoyalFire{missile->second,center,*step,nativeAdd(skills.number(row,"Param2").value_or(0),1),initialRandom(seed)};
}
std::vector<std::string> RemoteScene::Impl::pd2MapDescription(const OnlinePd2State::MapAttributeReport &report) const {
    // Current 0x391930 -> 0x36C9B0 describes the original item stats,
    // rather than the translated player/monster/global attribute lists.
    std::vector<StatValue> values;
    for(const auto &attribute:report.attributes) {
        values.emplace_back(StatKey{attribute.stat,attribute.parameter},attribute.value);
    }
    std::string_view name;
    const auto &levels=monsterText->tables.at("levels");
    for(size_t row=0;row<levels.rows().size();++row) if(levels.number(row,"Id")==report.area) {
        name=strings.find(levels.value(row,"LevelName"));
        break;
    }
    return pd2::describeMap(*monsterText,values,name,report.quality);
}
namespace {
uint32_t rollPd2VisualRandom(uint64_t &state) {
    // CN 36BAA0 uses SHRD 20 and an INT_MAX carry threshold. This is
    // distinct from the shared retail SEED_RollRandomNumber algorithm.
    const auto high=uint32_t(state>>32);
    const uint64_t product=uint64_t(uint32_t(state))*0x6ac690c5u;
    const auto low=uint32_t(product);
    const uint32_t carry=high && low>uint32_t(0x7fffffffu-high)?1:0;
    const auto next=low+high;
    state=(uint64_t(uint32_t(product>>20)+carry)<<32)|next;
    return next;
}
}
uint32_t RemoteScene::Impl::nextPd2PathSeed(OnlineUnitKey owner) {
    auto &seed=pd2VisualSeeds.try_emplace(owner,Pd2VisualSeed{initialRandom(owner.id),0}).first->second;
    // CN 37C460 initially uses (type<<28)^GUID, masked to 20 bits.
    // 37C400/37C470 then multiply the low bits and wrap the 12-bit counter.
    seed.path=pd2MissilePathSeed(seed.path,owner.type,owner.id);
    return seed.path;
}

std::optional<size_t> RemoteScene::Impl::pd2ThrownMissile(size_t skillRow,int weaponMissile,bool split) const {
    // CltDo98/118 switch on Items.MissileType (Common 10321), not
    // ItemTypes. Split Throw adds adjacent glaive/pilum C variants.
    const int slot=weaponMissile==36?1:(weaponMissile==1 || weaponMissile==37 || weaponMissile==371)?2:0;
    const auto base=missileNames.find(skills.value(skillRow,"cltmissile"+std::string(1,char('a'+slot))));
    if(base==missileNames.end()) return {};
    const int offset=split?(weaponMissile==37?1:weaponMissile==371?2:0):0;
    const auto missile=missileRows.find(missiles.number(base->second,"Id").value_or(-1)+offset);
    return missile==missileRows.end()?std::nullopt:std::optional{missile->second};
}
std::optional<size_t> RemoteScene::Impl::pd2DoomKnightMissile(size_t skillRow,const OnlineUnit &unit) const {
    // Current CltDo94 (0x3A8CF0) reads SrvMissileA, then the monster's
    // S3 component (MonsterData+0x0E), with four explicit class overrides.
    const auto base=missileNames.find(skills.value(skillRow,"srvmissilea"));
    if(base==missileNames.end()) return {};
    unsigned offset=0;
    if(unit.key.type==1) {
        const auto identity=unitClass(unit);if(!identity) return {};
        switch(*identity) {
        case 968:offset=0;break;case 969:offset=1;break;
        case 1106:offset=2;break;case 1107:offset=3;break;
        default: {
            const auto monster=monsterRows.find(*identity);if(monster==monsterRows.end()) return {};
            const auto extra=monsterExtra.find(monstats.value(monster->second,"MonStatsEx"));if(extra==monsterExtra.end()) return {};
            try {
                net::protocol::BitReader bits(unit.appearanceBits);bits.read(4);const bool present=bits.read(1)!=0;
                for(size_t component=0;component<=10;++component) {
                    const auto choices=variants(monstats2.value(extra->second,std::string(componentCodes[component])+"v"));
                    const auto value=present?bits.read(choices.size()>=3?std::bit_width(unsigned(choices.size()-1)):1):0;
                    if(component==10) offset=value;
                }
            } catch(const std::exception &) {return {};}
        }}
    }
    const auto result=missileRows.find(missiles.number(base->second,"Id").value_or(-1)+int(offset));
    return result==missileRows.end()?std::nullopt:std::optional{result->second};
}

std::optional<std::pair<int,int>> RemoteScene::Impl::findPd2LeapLanding(
    const OnlineUnit &u,Vec start,Vec target,int range,const Map &map,Vec origin) const {
    const int x=int(std::floor(start.x)),y=int(std::floor(start.y));
    int tx=int(std::floor(target.x)),ty=int(std::floor(target.y));
    const int distance=std::max(1,unitCoordinateDistance(x,y,tx,ty));
    if(distance>range) {tx=x+nativeMultiply(tx-x,range)/distance;ty=y+nativeMultiply(ty-y,range)/distance;}
    const int dx=std::clamp(ty-y,-2,2),dy=std::clamp(x-tx,-2,2);
    const auto collision=navigationCollision(map.grid,origin,u.key);
    const auto clear=[&](int column,int line) {
        // Linked collision rooms already return FFFF for unavailable cells.
        return collision.walkable(Vec{float(column),float(line)}-origin,{0x1c09,unitSize(u)});
    };
    for(const auto &[cx,cy]:{std::pair{tx,ty},std::pair{tx+dx,ty+dy},std::pair{tx-dx,ty-dy}}) {
        const auto free=nativeSpawnCoordinate(cx,cy,50,clear);
        if(free && unitCoordinateDistance(x,y,free->first,free->second)<=range &&
           map.grid.collisionSegment(Vec{float(x),float(y)}-origin,Vec{float(free->first),float(free->second)}-origin,0x804)) return free;
    }
    return {};
}
bool RemoteScene::Impl::displayPd2MonsterCharge(const OnlineUnit &u,Motion &m,const Map &map,Vec origin) {
    if(!combatBinding || !u.actionSkill || onlineMonsterCorpse(u)) {m.scriptedSequence.reset();return false;}
    if(m.pd2InvalidatedMovement==u.actionRevision) return false;
    const auto skill=skillRows.find(*u.actionSkill),monster=monsterRows.find(unitClass(u).value_or(UINT16_MAX));
    if(skill==skillRows.end() || monster==monsterRows.end()) return false;
    const auto pose=monsterSkillMode(monster->second,*u.actionSkill);
    const auto sequence=monsterSequenceFlags.find(pose);
    const bool sequenced=sequence!=monsterSequenceFlags.end() && !sequence->second.empty();
    const auto clock=monsterChargeTimings.find({*unitClass(u),pose});
    const auto properties=combatBinding->monsterPropertyStats(u);
    const auto snapshot=combatBinding->states().find(u.key);
    if((!sequenced && clock==monsterChargeTimings.end()) || !properties ||
       snapshot==combatBinding->states().end() || !snapshot->second.decoded || snapshot->second.sequence!=u.stateSequence) return false;
    const int nativeFrames=sequenced?int(sequence->second.size()):15;
    const auto stat=[&](int id,int base) {
        const auto add=[&](const auto &list) {for(const auto &entry:list) if(entry.id==id && !entry.parameter) base=nativeAdd(base,nativeNarrow(entry.value));};
        add(*properties);for(const auto &[state,list]:snapshot->second.states) {(void)state;add(list.stats);}
        return base;
    };
    const int itemIAS=stat(93,0);if(itemIAS<=-120) return false;
    const int animationSpeed=sequenced?256:effectiveAttackSpeed(clock->second.speed,itemIAS,0,nativeSubtract(stat(68,100),100));
    const auto collision=movementRule(u);const auto grid=navigationCollision(map.grid,origin,u.key,u.destinationUnit);
    auto destination=u.destination?std::optional{Vec{float(u.destination->x),float(u.destination->y)}}:
        u.destinationUnit?localUnitPosition(*u.destinationUnit):std::nullopt;
    if(!destination) return false;
    if(!m.scriptedSequence || m.scriptedSequence->revision!=u.actionRevision) {
        // Retail Client 4D610 initializes monster rate stats to 100; the
        // previous walking packet's velocity includes another mode's scaling.
        const int percent=stat(67,100);
        const auto speed=monstats.number(monster->second,"Run");if(!speed || *speed<=0) return false;
        const int raw=nativePercentage(nativeShiftLeft(*speed,8),nativeAdd(skills.number(skill->second,"Param1").value_or(0),std::max(50,percent)),100);
        Motion::ScriptedSequence run;run.revision=u.actionRevision;run.nativeFrame=true;
        run.animation.remaining=(sequenced?nativeFrames:clock->second.frames)*256;
        run.destination=*destination;run.velocity=float(raw)*25.f/4096.f;run.nextTick=time+NativePathStep::seconds;run.moving=true;
        const auto now=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        if(u.actionReceivedMilliseconds && now>=u.actionReceivedMilliseconds) run.nextTick-=float(now-u.actionReceivedMilliseconds)/1000.f;
        m.scriptedSequence=std::move(run);m.route.clear();m.goal.reset();m.pathStep.reset();m.correction={};m.correctionLeft=0;
    }
    auto &run=*m.scriptedSequence;
    const int eventFrame=!sequenced || sequence->second.front()?7:-1; // Retail 2B6E0 reads the first MonSeq event byte repeatedly.
    for(const auto &[id,list]:snapshot->second.states) {(void)id;if(list.name=="freeze") {run.nextTick=time+NativePathStep::seconds;return true;}}
    const auto inReach=[&](const OnlineUnit &target) {
        const auto point=localUnitPosition(target.key);
        return point && unitMeleeReach(grid,m.position-origin,unitSize(u),*point-origin,unitSize(target),monstats.number(monster->second,"MeleeRng").value_or(0));
    };
    while(!run.ended && run.nextTick<=time+1e-5f) {
        run.nextTick+=NativePathStep::seconds;
        if(run.animation.remaining<=0) {run.ended=true;run.moving=false;break;}
        if(run.moving) {
            if(!m.goal || std::floor(m.goal->x)!=std::floor(destination->x) || std::floor(m.goal->y)!=std::floor(destination->y)) {
                const Vec center{.5f,.5f};m.route.clear();
                const auto path=grid.nativeUnitPath(m.position-origin+center,*destination-origin+center,2,30,collision,u.destinationUnit.has_value());
                for(const auto point:path) m.route.push_back(point+origin-center);
                m.goal=*destination;m.pathStep.reset();
            }
            bool contact=false,retargeted=false;
            if(u.destinationUnit) if(const auto target=world().units.find(*u.destinationUnit);target!=world().units.end()) contact=inReach(target->second);
            bool stopped=m.route.empty();
            if(!contact && !stopped) {
                const Vec center{.5f,.5f},next=m.route.front();m.look=next-m.position;
                const auto step=m.pathStep.advance(grid,m.position+center,next+center,run.velocity*NativePathStep::seconds,collision,origin);
                m.position=step.position-center;if(step.blocked) m.route.clear();else if(step.reached) m.route.pop_front();
                stopped=m.route.empty();
                if(u.destinationUnit) if(const auto target=world().units.find(*u.destinationUnit);target!=world().units.end()) contact=inReach(target->second);
            }
            if(stopped && !contact) {
                // Native 2B680/ A09F0 selects a GUID successor in range3,
                // then tests melee reach; it does not choose nearest distance.
                std::vector<EntityId> candidates;
                for(const auto &[key,target]:world().units) if(combatBinding->pd2ChargeTargetEligible(u,target)) {
                    const auto point=localUnitPosition(key);
                    if(point && unitCoordinateDistance(int(std::floor(m.position.x)),int(std::floor(m.position.y)),
                        int(std::floor(point->x)),int(std::floor(point->y)))<=3 &&
                       map.grid.collisionSegment(m.position-origin,*point-origin,4)) candidates.push_back(EntityId{key.id});
                }
                const auto selected=nativeSkillSuccessor(candidates,uint32_t(run.chargeSearchParameter));
                for(const auto &[key,target]:world().units) if(key.id==selected.value &&
                    combatBinding->pd2ChargeTargetEligible(u,target)) {contact=inReach(target);retargeted=contact;break;}
            }
            if(contact || stopped) {
                run.moving=false;run.stopping=true;m.route.clear();m.goal.reset();
                if(retargeted) run.animation.remaining=nativeFrames*256-(sequenced?run.animation.frame:0);
                else if(contact) {
                    run.animation.frame=(run.animation.frame&255)+(eventFrame+1)*256;
                    run.animation.remaining=nativeFrames*256;
                } else {
                    const auto base=monstats.value(monster->second,"BaseId");
                    if(base!="reanimatedhorde1" && base!="siegebeast1") run.animation.remaining=256;
                }
            } else {
                // Native 2B8A0 reads dwSeqFrame, which is not the ordinary
                // graphics cursor when MonStats selects A2 instead of SQ.
                if(sequenced && (run.animation.frame>>8)>=eventFrame) {run.animation.frame&=255;run.animation.remaining=nativeFrames*256;}
                run.chargeSearchParameter=std::min(run.chargeSearchParameter+1,2);
            }
        }
        const auto step=advanceMonsterAnimation(run.animation,sequenced?nativeFrames:std::max(1,run.animation.remaining>>8),
            animationSpeed,0,sequenced,sequenced?sequence->second:clock->second.flags);
        run.event=step.event;
        if(!sequenced && step.ended) {run.ended=true;run.moving=false;m.route.clear();m.goal.reset();}
    }
    return true;
}
bool RemoteScene::Impl::displayPd2PlayerCharge(const OnlineUnit &u,Motion &m,const Map &map,Vec origin) {
    const bool controlled=u.key.id==playerId;
    const bool local=controlled && localCast && localCast->started>=0;
    const auto skill=local?std::optional<int>{localCast->command.skill}:!controlled && u.actionSkill?std::optional<int>{*u.actionSkill}:std::nullopt;
    const auto row=skill?skillRows.find(*skill):skillRows.end();
    if(u.key.type!=0 || !skill ||
       row==skillRows.end() || skills.number(row->second,"cltdofunc")!=112 || playerDead(u)) {
        if(m.pd2PlayerCharge && m.pd2PlayerCharge->confirmed && m.pd2PlayerCharge->attackAt>=0 &&
           (!controlled || (!localCast && (!world().movementRequest || world().movementRequest->revision<=m.pd2PlayerCharge->revision)))) {
            // The last native stop sample can follow skill_move removal.
            // Reconcile it before discarding the skill's path ownership;
            // the old walk handoff must not classify it as a lagging walk.
            const Vec displayed=m.position+m.correction;
            m.position={float(u.position->x),float(u.position->y)};
            m.correction=displayed-m.position;m.correctionLeft=.12f;
        }
        m.pd2PlayerCharge.reset();return false;
    }
    const auto snapshot=combatBinding->states().find(u.key);
    if(snapshot==combatBinding->states().end() || !snapshot->second.decoded || snapshot->second.sequence!=u.stateSequence)
        return false;
    bool travelling=false;
    for(const auto &[id,list]:snapshot->second.states) {
        (void)list;const auto state=stateRows.find(id);
        if(state!=stateRows.end() && states.value(state->second,"state")=="skill_move") travelling=true;
    }
    // CltSt57 computes the owner's path immediately. Waiting for the
    // server state first adds a round trip and ends short of native contact.
    // Near-target CltSt57 falls back to the ordinary melee action instead.
    if(!travelling && !m.pd2PlayerCharge && (!local || std::isfinite(localCast->duration))) return false;
    const auto revision=local?localCast->revision:u.actionRevision;
    if(m.pd2InvalidatedMovement==revision) return false;
    const auto target=local?localCast->command.target:u.destinationUnit;
    const auto victim=target?world().units.find(*target):world().units.end();
    const auto coordinates=local?localCast->command.point:u.destination;
    const auto point=target?localUnitPosition(*target):coordinates?
        std::optional{Vec{float(coordinates->x),float(coordinates->y)}}:std::nullopt;
    if(!point || (target && victim==world().units.end())) return false;
    const auto cls=unitClass(u);
    std::optional<int> velocity,attackRate,itemIAS;
    if(const auto rates=uiClients->pd2PlayerRates(u.key)) {
        velocity=rates->velocityPercent;attackRate=rates->attackRate;itemIAS=rates->itemIAS;
    }
    if(!cls || *cls>=characterRows.size() || !velocity || !attackRate || !itemIAS || *itemIAS<=-120) {
        effectLimitations.insert("PD2 charge velocity or attack rate unresolved");return false;
    }
    const auto runSpeed=charstats.number(characterRows[*cls],"RunVelocity");
    if(!runSpeed || *runSpeed<=0) return false;
    // PlayerStats/PlrSave2 initialize stat 67/68 to 100. The projection
    // carries equipment/state/passive contributions, and weapon speed is
    // the equipped weapon's native negative stat 68 contribution.
    const int percent=nativeAdd(skills.number(row->second,"Param1").value_or(0),std::max(50,*velocity));
    const float speed=float(nativePercentage(nativeShiftLeft(*runSpeed,8),percent,100))*25.f/4096.f;
    if(!m.pd2PlayerCharge || m.pd2PlayerCharge->revision!=revision) {
        const int attack=std::min(256,nativeAdd(nativeAdd(effectiveFasterRate(*itemIAS),*attackRate),skills.number(row->second,"Param6").value_or(0)));
        if(attack<=0) {effectLimitations.insert("PD2 charge nonpositive attack rate");return false;}
        m.pd2PlayerCharge=Motion::Pd2PlayerCharge{revision,time+NativePathStep::seconds,-1,attack};
        m.route.clear();m.goal.reset();m.pathStep.reset();m.correction={};m.correctionLeft=0;
    }
    auto &charge=*m.pd2PlayerCharge;
    charge.confirmed|=travelling;
    if(charge.attackAt>=0) {
        while(charge.nextTick<=time) {
            charge.nextTick+=NativePathStep::seconds;
            if(m.correctionLeft>0) {
                m.correction=m.correction*(1.f-std::min(1.f,NativePathStep::seconds/m.correctionLeft));
                m.correctionLeft=std::max(0.f,m.correctionLeft-NativePathStep::seconds);
            }
        }
        m.pathMoving=false;return true;
    }
    const auto grid=navigationCollision(map.grid,origin,u.key,target);
    std::array<std::optional<int>,2> weaponReach;
    for(const auto &[id,item]:world().items) {
        (void)id;if(item.ownerType!=0 || item.owner!=u.key.id || item.mode!=1 ||
            (item.body!=4 && item.body!=5) || (item.flags&0x4100u)) continue;
        for(size_t weapon=0;weapon<weapons.rows().size();++weapon)
            if(weapons.value(weapon,"code")==item.code) weaponReach[size_t(item.body-4)]=weapons.number(weapon,"rangeadder").value_or(0);
    }
    const int reach=weaponReach[0].value_or(weaponReach[1].value_or(0));
    const auto inReach=[&] {return victim!=world().units.end() &&
        playerMeleeReach(grid,m.position-origin,*point-origin,unitSize(victim->second),reach+(walking(victim->second)?1:0));};
    const auto victimDead=[&] {return victim!=world().units.end() &&
        (onlineMonsterCorpse(victim->second) || playerDead(victim->second));};
    const auto finish=[&](bool attack) {
        m.route.clear();m.goal.reset();m.pathMoving=false;charge.attackAt=time;
        if(local) localCast->duration=time-localCast->started+(attack?float((7*256+charge.attackSpeed-1)/charge.attackSpeed)/25.f:1.f/25.f);
    };
    if((charge.confirmed && !travelling) || victimDead()) {
        if(!travelling) {
            const Vec displayed=m.position+m.correction;
            m.position={float(u.position->x),float(u.position->y)};
            m.correction=displayed-m.position;m.correctionLeft=.12f;
        }
        finish(!victimDead() && inReach());return true;
    }
    if(inReach()) {finish(true);return true;}
    const Vec center{.5f,.5f};
    if(!m.goal || std::floor(m.goal->x)!=std::floor(point->x) || std::floor(m.goal->y)!=std::floor(point->y)) {
        auto route=grid.nativeUnitPath(m.position-origin+center,*point-origin+center,7,100,playerMovement,target.has_value());
        m.route.clear();for(auto cell:route) m.route.push_back(cell+origin-center);
        m.goal=*point;m.pathStep.reset();
    }
    while(charge.nextTick<=time) {
        charge.nextTick+=NativePathStep::seconds;
        if(m.route.empty()) {finish(false);return true;}
        const auto next=m.route.front();m.look=next-m.position;
        const auto step=m.pathStep.advance(grid,m.position+center,next+center,speed*NativePathStep::seconds,playerMovement,origin);
        m.position=step.position-center;
        if(step.blocked) {finish(false);return true;}
        if(inReach()) {finish(true);return true;}
        if(step.reached) m.route.pop_front();
    }
    m.pathMoving=true;return true;
}

bool RemoteScene::Impl::displayPd2PlayerLeap(const OnlineUnit &u,Motion &m,const Map &map,Vec origin) {
    const bool controlled=u.key.id==playerId;
    const bool local=controlled && localCast && localCast->started>=0;
    const auto skill=local?std::optional<int>{localCast->command.skill}:
        !controlled && u.actionSkill?std::optional<int>{*u.actionSkill}:
        !controlled && m.pd2PlayerLeap && m.pd2PlayerLeap->revision==u.actionRevision?
            std::optional<int>{m.pd2PlayerLeap->skill}:std::nullopt;
    const auto row=skill?skillRows.find(*skill):skillRows.end();
    if(u.key.type!=0 || !skill ||
       row==skillRows.end() || skills.number(row->second,"cltdofunc")!=97 || playerDead(u)) {
        m.pd2PlayerLeap.reset();return false;
    }
    const auto revision=local?localCast->revision:!u.actionSkill && m.pd2PlayerLeap?m.pd2PlayerLeap->revision:u.actionRevision;
    if(m.pd2InvalidatedMovement==revision) return false;
    const auto now=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    const float age=u.actionReceivedMilliseconds && now>=u.actionReceivedMilliseconds?float(now-u.actionReceivedMilliseconds)/1000.f:0.f;
    const float started=local?localCast->started:m.pd2PlayerLeap && m.pd2PlayerLeap->revision==revision?m.pd2PlayerLeap->started:time-age;
    const auto snapshot=combatBinding->states().find(u.key);
    if(snapshot==combatBinding->states().end() || !snapshot->second.decoded || snapshot->second.sequence!=u.stateSequence)
        return false;
    bool flying=false;
    for(const auto &[id,list]:snapshot->second.states) {
        (void)list;const auto state=stateRows.find(id);
        if(state!=stateRows.end() && states.value(state->second,"state")=="skill_move") flying=true;
    }
    // The server's movement-state removal can precede the final position
    // packet. A confirmed flight still owns its native arc/landing.
    if(!flying && !m.pd2PlayerLeap) return false;
    if(m.leapRevision==revision && m.leapLanding>=0) return false;
    const auto sequence=playerCastSequence(skills.number(row->second,"seqnum").value_or(-1));
    if(!sequence || sequence->fixedSpeed<=0 ||
       time<started+float(playerCastSequenceTiming(*sequence,sequence->fixedSpeed).impact)/25.f)
        return false;
    if(!m.pd2PlayerLeap || m.pd2PlayerLeap->revision!=revision) {
        auto destination=local?localCast->command.point:u.destination;
        const auto target=local?localCast->command.target:u.destinationUnit;
        if(target) destination=targetPosition(*target);
        int rank=u.actionSkillLevel.value_or(0);
        if(local) {
            const auto *known=uiClients->character().read().skill(*skill,localCast->command.owner);
            if(!known || !known->effectiveRankKnown) return false;
            rank=known->effectiveRank;
        }
        if(!destination || rank<1) return false;
        const auto range=skillCalculation(*skill,rank,"aurarangecalc",u.key);
        const auto speed=skillCalculation(*skill,rank,"calc2",u.key);
        if(!range || !speed || *range<1) {
            effectLimitations.insert("PD2 leap range or velocity unresolved");return false;
        }
        // Retail CltSt29/C66A0 uses the range formula, then three rotated
        // candidates with the original free-coordinate search and 804 ray.
        const int x=int(std::floor(m.position.x)),y=int(std::floor(m.position.y));
        const auto landing=findPd2LeapLanding(u,m.position,Vec{float(destination->x),float(destination->y)},*range,map,origin);
        if(!landing) {effectLimitations.insert("PD2 leap landing unavailable");return false;}
        // CN CltDo97: Calc2*41+2560, with the native minimum of 2560.
        const int velocity=std::max(2560,nativeAdd(nativeMultiply(*speed,41),2560));
        const int travel=std::max(1,unitCoordinateDistance(x,y,landing->first,landing->second));
        int frames=travel*16/(velocity>>8);if(frames>1) --frames;frames=std::max(1,frames);
        Motion::Pd2PlayerLeap run;run.revision=revision;run.position=m.position;run.skill=*skill;run.started=started;
        run.destination={float(landing->first),float(landing->second)};
        run.velocity=float(velocity)*25.f/4096.f;run.nextTick=time+NativePathStep::seconds;
        // Retail C65B0/C7C80: distance-dependent gravity and zero initial
        // height; flight ticks use the common native graphics accumulator.
        const int gravity=std::max(500,travel<27?8462-270*travel:1224-72*travel);
        run.graphics.flags=2;run.graphics.remaining=frames;run.graphics.acceleration[2]=-gravity;
        run.graphics.velocity[2]=nativeDivide(nativeMultiply(nativeMultiply(frames,frames),gravity)/2,frames);
        m.pd2PlayerLeap=std::move(run);m.leapRevision=revision;m.leapFlying=true;m.leapLanding=-1;
        m.route.clear();m.goal.reset();m.correction={};m.correctionLeft=0;m.pathStep.reset();
    }
    auto &run=*m.pd2PlayerLeap;
    while(run.nextTick<=time) {
        const Vec center{.5f,.5f};
        const auto step=run.path.advance(map.grid,run.position+center,run.destination+center,
            run.velocity*NativePathStep::seconds,{0,unitSize(u)},origin);
        m.look=run.destination-run.position;run.position=step.position-center;
        run.graphics.step();run.nextTick+=NativePathStep::seconds;
        if(step.reached || (run.graphics.position[2]>>11)<=0) {
            m.position=run.destination;m.pathMoving=false;m.leapLanding=time;m.pd2PlayerLeap.reset();
            return true;
        }
        if(step.blocked) {
            effectLimitations.insert("PD2 leap flight path unavailable");m.pd2PlayerLeap.reset();return false;
        }
    }
    m.position=run.position;m.pathMoving=true;
    return true;
}

void RemoteScene::Impl::emitPd2Stream(Channel &channel,OnlineUnitKey owner,Vec start,Vec target,
                                     SceneView &shared,const RemoteCombat &combat) {
    // Fixed CN CltDo111: 3A7390 (old core). Calc1 controls frames,
    // Calc3 controls the lateral candidates. It is not retail CltDo24.
    const int skill=skills.number(channel.skillRow,"Id").value_or(-1);
    const auto range=skillCalculation(skill,channel.level,"calc1",owner);
    const auto width=skillCalculation(skill,channel.level,"calc3",owner);
    if(!range || !width) {
        effectLimitations.insert("PD2 stream formula unavailable: "+std::to_string(skill));
        channel.next=time+1.f/25.f;return;
    }
    const int frames=std::max(1,*range),count=std::max(1,*width);
    const auto actor=world().units.find(owner);
    if(actor==world().units.end()) return;
    auto &randomState=pd2VisualSeeds.try_emplace(owner,Pd2VisualSeed{initialRandom(owner.id),0}).first->second.random;
    // Visual seeds are local; they never drive authoritative hits or debuffs.
    // Preserve draw order, the signed remainders and the 40% spread branch.
    int callbacks=0;
    while(channel.next<=time && callbacks++<8) {
        const int spreadRoll=int32_t(rollPd2VisualRandom(randomState));
        const int extension=count>1 && spreadRoll%100<40?count:0;
        const auto center=pd2MissileEndpoint(start,target,count/2+extension+8);
        const auto layout=pd2MissileFanLayout(start,center,count);
        for(int index=0;index<count;++index) {
            const int random=int32_t(rollPd2VisualRandom(randomState));
            if(random%count>=2) continue;
            ClientMissileInitialization init;
            // 0x8020 overrides TotalFrames (+0x4C), then SetCurrentFrame
            // writes Calc1. The computed +0x28 velocity is ignored: the
            // creator's 0x04 velocity flag is absent (D2Client B31F8).
            init.totalFrames=nativeAdd(frames,random%2-1);
            if(!shared.launchClientMissile(missiles.number(channel.missileRow,"Id").value_or(-1),
                start,layout.first+layout.step*float(index),channel.level,channel.next-time,
                float(frames)/25.f,-1,effectOwner(owner),combat.hostileSource(actor->second),0,
                ClientMissileSource::Cast,0,{},0,init))
                effectLimitations.insert("PD2 stream missile unavailable: "+std::to_string(skill));
        }
        channel.next+=1.f/25.f;
    }
    if(channel.next<time) channel.next=time;
}
} // namespace d2x
