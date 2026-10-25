#include "remote_scene_impl.hpp"
#include "gameplay/skills/cast_timing.hpp"
#include "gameplay/actions/timing.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "network/protocol/bits.hpp"
#include "content/items/item_properties.hpp"

namespace d2x {
std::vector<std::string> RemoteScene::Impl::pd2MapDescription(const OnlinePd2State::MapAttributeReport &report) const {
    // Current 0x391930 -> 0x36C9B0 describes the original item stats,
    // rather than the translated player/monster/global attribute lists.
    std::vector<ResolvedItemStat> values;
    for(const auto &attribute:report.attributes) {
        if(attribute.stat==360 || attribute.stat==361 || attribute.stat==458) continue;
        const auto definition=std::find_if(monsterText->itemStats.begin(),monsterText->itemStats.end(),
            [&](const auto &stat){return stat.id==attribute.stat;});
        if(definition!=monsterText->itemStats.end()) values.push_back({definition->name,{},attribute.value,attribute.parameter,attribute.value});
    }
    auto lines=describeItemStats(*monsterText,values);
    for(auto &line:lines) line=std::string("\xff" "c3")+line;
    const auto &levels=monsterText->tables.at("levels");
    for(size_t row=0;row<levels.rows().size();++row) if(levels.number(row,"Id")==report.area) {
        const auto name=strings.find(levels.value(row,"LevelName"));
        if(!name.empty()) {
            const char color=report.quality==4?'3':report.quality==6?'9':report.quality==7?'4':'0';
            lines.push_back(std::string("\xff" "c")+color+std::string(name));
        }
        break;
    }
    return lines;
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

bool RemoteScene::Impl::displayPd2PlayerCharge(const OnlineUnit &u,Motion &m,const Map &map,Vec origin) {
    const auto row=localCast?skillRows.find(localCast->command.skill):skillRows.end();
    if(u.key.type!=0 || u.key.id!=playerId || !localCast || localCast->started<0 ||
       row==skillRows.end() || skills.number(row->second,"cltdofunc")!=112 || playerDead(u)) {
        if(!localCast && m.pd2PlayerCharge && m.pd2PlayerCharge->confirmed && m.pd2PlayerCharge->attackAt>=0 &&
           (!world().movementRequest || world().movementRequest->revision<=m.pd2PlayerCharge->revision)) {
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
    if(!travelling && !m.pd2PlayerCharge && std::isfinite(localCast->duration)) return false;
    const auto target=localCast->command.target;
    const auto victim=target?world().units.find(*target):world().units.end();
    const auto point=target?localUnitPosition(*target):std::nullopt;
    if(victim==world().units.end() || !point) return false;
    const auto cls=unitClass(u);
    const auto &attributes=uiClients->skillFormulaStats();
    const auto velocity=attributes.find("velocitypercent");
    const auto timing=uiClients->attackTiming();
    if(!cls || *cls>=characterRows.size() || velocity==attributes.end() || !timing || timing->itemIAS<=-120) {
        effectLimitations.insert("PD2 charge velocity or attack rate unresolved");return false;
    }
    const auto runSpeed=charstats.number(characterRows[*cls],"RunVelocity");
    if(!runSpeed || *runSpeed<=0) return false;
    // PlayerStats/PlrSave2 initialize stat 67/68 to 100. The projection
    // carries equipment/state/passive contributions, and weapon speed is
    // the equipped weapon's native negative stat 68 contribution.
    const int base=world().playerAttributes.contains(67)?0:100;
    const int percent=skills.number(row->second,"Param1").value_or(0)+std::max(50,nativeAdd(base,int(velocity->second)));
    const float speed=float(nativePercentage(nativeShiftLeft(*runSpeed,8),percent,100))*25.f/4096.f;
    if(!m.pd2PlayerCharge || m.pd2PlayerCharge->revision!=localCast->revision) {
        const int attack=std::min(256,nativeAdd(nativeSubtract(nativeAdd(100,
            nativeAdd(effectiveFasterRate(timing->itemIAS),timing->skillRate)),timing->weaponSpeed),
            skills.number(row->second,"Param6").value_or(0)));
        if(attack<=0) {effectLimitations.insert("PD2 charge nonpositive attack rate");return false;}
        m.pd2PlayerCharge=Motion::Pd2PlayerCharge{localCast->revision,time+NativePathStep::seconds,-1,attack};
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
    int reach=0;
    for(const auto &[id,item]:world().items) {
        (void)id;if(item.ownerType!=0 || item.owner!=u.key.id || item.mode!=1 || item.body!=4 || (item.flags&0x4100u)) continue;
        for(size_t weapon=0;weapon<weapons.rows().size();++weapon)
            if(weapons.value(weapon,"code")==item.code) reach=weapons.number(weapon,"rangeadder").value_or(0);
    }
    const auto inReach=[&] {return playerMeleeReach(grid,m.position-origin,*point-origin,unitSize(victim->second),reach+(walking(victim->second)?1:0));};
    const auto finish=[&](bool attack) {
        m.route.clear();m.goal.reset();m.pathMoving=false;charge.attackAt=time;
        localCast->duration=time-localCast->started+(attack?float((7*256+charge.attackSpeed-1)/charge.attackSpeed)/25.f:1.f/25.f);
    };
    if((charge.confirmed && !travelling) || onlineMonsterCorpse(victim->second) || playerDead(victim->second)) {
        if(!travelling) {
            const Vec displayed=m.position+m.correction;
            m.position={float(u.position->x),float(u.position->y)};
            m.correction=displayed-m.position;m.correctionLeft=.12f;
        }
        finish(!onlineMonsterCorpse(victim->second) && !playerDead(victim->second) && inReach());return true;
    }
    if(inReach()) {finish(true);return true;}
    const Vec center{.5f,.5f};
    if(!m.goal || std::floor(m.goal->x)!=std::floor(point->x) || std::floor(m.goal->y)!=std::floor(point->y)) {
        auto route=grid.nativePlayerPath(m.position-origin+center,*point-origin+center,playerMovement,target->type);
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
    const auto row=localCast?skillRows.find(localCast->command.skill):skillRows.end();
    if(u.key.type!=0 || u.key.id!=playerId || !localCast || localCast->started<0 ||
       row==skillRows.end() || skills.number(row->second,"cltdofunc")!=97 || playerDead(u)) {
        m.pd2PlayerLeap.reset();return false;
    }
    const auto snapshot=combatBinding->states().find(u.key);
    if(snapshot==combatBinding->states().end() || !snapshot->second.decoded || snapshot->second.sequence!=u.stateSequence)
        return false;
    bool flying=false;
    for(const auto &[id,list]:snapshot->second.states) {
        (void)list;const auto state=stateRows.find(id);
        if(state!=stateRows.end() && states.value(state->second,"state")=="skill_move") flying=true;
    }
    // The server's movement-state removal can precede the final position
    // packet. A confirmed local flight still owns its native arc/landing.
    if(!flying && !m.pd2PlayerLeap) return false;
    if(m.leapRevision==localCast->revision && m.leapLanding>=0) return false;
    const auto sequence=playerCastSequence(skills.number(row->second,"seqnum").value_or(-1));
    if(!sequence || sequence->fixedSpeed<=0 ||
       time<localCast->started+float(playerCastSequenceTiming(*sequence,sequence->fixedSpeed).impact)/25.f)
        return false;
    if(!m.pd2PlayerLeap || m.pd2PlayerLeap->revision!=localCast->revision) {
        auto destination=localCast->command.point;
        if(localCast->command.target) destination=targetPosition(*localCast->command.target);
        const auto rank=world().playerSkills.find(localCast->command.skill);
        if(!destination || rank==world().playerSkills.end()) return false;
        const auto range=skillCalculation(localCast->command.skill,rank->second,"aurarangecalc",u.key);
        const auto speed=skillCalculation(localCast->command.skill,rank->second,"calc2",u.key);
        if(!range || !speed || *range<1) {
            effectLimitations.insert("PD2 leap range or velocity unresolved");return false;
        }
        // Retail CltSt29/C66A0 uses the range formula, then three rotated
        // candidates with the original free-coordinate search and 804 ray.
        const int x=int(std::floor(m.position.x)),y=int(std::floor(m.position.y));
        int tx=destination->x,ty=destination->y;
        const int distance=std::max(1,unitCoordinateDistance(x,y,tx,ty));
        if(distance>*range) {tx=x+nativeMultiply(tx-x,*range)/distance;ty=y+nativeMultiply(ty-y,*range)/distance;}
        const int dx=std::clamp(ty-y,-2,2),dy=std::clamp(x-tx,-2,2);
        const auto collision=navigationCollision(map.grid,origin,u.key);
        const auto clear=[&](int column,int line) {
            // Remote maps use linked native collision rooms. Their absent
            // cells are FFFF; the self-hosted RoomLayout is not that index.
            return collision.walkable(Vec{float(column),float(line)}-origin,{0x1c09,unitSize(u)});
        };
        std::optional<std::pair<int,int>> landing;
        for(const auto &[cx,cy]:{std::pair{tx,ty},std::pair{tx+dx,ty+dy},std::pair{tx-dx,ty-dy}}) {
            const auto free=nativeSpawnCoordinate(cx,cy,50,clear);
            if(free && unitCoordinateDistance(x,y,free->first,free->second)<=*range &&
               map.grid.collisionSegment(Vec{float(x),float(y)}-origin,Vec{float(free->first),float(free->second)}-origin,0x804)) {
                landing=free;break;
            }
        }
        if(!landing) {effectLimitations.insert("PD2 leap landing unavailable");return false;}
        // CN CltDo97: Calc2*41+2560, with the native minimum of 2560.
        const int velocity=std::max(2560,nativeAdd(nativeMultiply(*speed,41),2560));
        const int travel=std::max(1,unitCoordinateDistance(x,y,landing->first,landing->second));
        int frames=travel*16/(velocity>>8);if(frames>1) --frames;frames=std::max(1,frames);
        Motion::Pd2PlayerLeap run;run.revision=localCast->revision;run.position=m.position;
        run.destination={float(landing->first),float(landing->second)};
        run.velocity=float(velocity)*25.f/4096.f;run.nextTick=time+NativePathStep::seconds;
        // Retail C65B0/C7C80: distance-dependent gravity and zero initial
        // height; flight ticks use the common native graphics accumulator.
        const int gravity=std::max(500,travel<27?8462-270*travel:1224-72*travel);
        run.graphics.flags=2;run.graphics.remaining=frames;run.graphics.acceleration[2]=-gravity;
        run.graphics.velocity[2]=nativeDivide(nativeMultiply(nativeMultiply(frames,frames),gravity)/2,frames);
        m.pd2PlayerLeap=std::move(run);m.leapRevision=localCast->revision;m.leapFlying=true;m.leapLanding=-1;
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
