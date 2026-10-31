#include "ai_impl.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace d2x {
namespace {
Vec position(OnlinePoint p) {return {float(p.x),float(p.y)};}
}
void DebugAi::Impl::Definitions::loadPotions() {
    const auto isType=[&](std::string_view code,std::string_view wanted) {
        std::vector<std::string> pending{std::string(code)};std::set<std::string> seen;
        while(!pending.empty()) {
            auto next=std::move(pending.back());pending.pop_back();
            if(next==wanted) return true;
            if(next.empty() || !seen.insert(next).second) continue;
            for(size_t row=0;row<types.rows().size();++row) if(types.value(row,"Code")==next) {
                pending.emplace_back(types.value(row,"Equiv1"));pending.emplace_back(types.value(row,"Equiv2"));break;
            }
        }
        return false;
    };
    for(size_t row=0;row<misc.rows().size();++row) {
        if(!misc.number(row,"useable").value_or(0)) continue;
        const auto type=misc.value(row,"type");const int spell=misc.number(row,"pSpell").value_or(0);
        Potion potion;
        if(spell==5 && isType(type,"rpot")) potion.kind=PotionKind::Rejuvenation;
        else if(spell==3 && isType(type,"hpot")) potion.kind=PotionKind::Life;
        else if(spell==3 && isType(type,"mpot")) potion.kind=PotionKind::Mana;
        else continue;
        // Selection hint only: never evaluate or apply recovery on the client.
        potion.strength=misc.number(row,"calc1").value_or(misc.number(row,"cost").value_or(0));
        const auto state=misc.value(row,"state");
        if(!state.empty()) for(size_t s=0;s<states.rows().size();++s)
            if(states.value(s,"state")==state) if(const auto id=states.number(s,"Id");id && *id>=0 && *id<256) potion.state=uint8_t(*id);
        potions.emplace(std::string(misc.value(row,"code")),potion);
    }
}
DebugAi::Impl::Selector DebugAi::Impl::Definitions::supplier(unsigned act,bool portal) const {
    // D2MOO SUnitProxy's native NPC/act table. Names resolve to this MPQ's
    // compiled identities; the actual shelf, prices and positions remain live.
    static constexpr std::array names{"akara","lysander","ormus","jamella","malah"};
    if(act>=names.size()) throw std::runtime_error("No verified potion supplier for this act");
    const std::string name=portal && act==1?"drognan":names[act];
    bool vendor=false,life=false,mana=false;
    for(size_t row=0;row<npcs.rows().size();++row) vendor|=npcs.value(row,"npc")==name;
    for(size_t row=0;row<misc.rows().size();++row) {
        const auto found=potions.find(misc.value(row,"code"));
        if(found==potions.end() || misc.number(row,name+"Max").value_or(0)<=0) continue;
        life|=found->second.kind==PotionKind::Life;mana|=found->second.kind==PotionKind::Mana;
    }
    bool book=false,scroll=false;
    for(size_t row=0;row<misc.rows().size();++row) if(misc.number(row,name+"Max").value_or(0)>0) {
        book|=misc.value(row,"code")=="tbk";scroll|=misc.value(row,"code")=="tsc";
    }
    if(!vendor || (portal?(!book || !scroll):(!life || !mana)))
        throw std::runtime_error("Current MPQ does not identify the expected supply vendor: "+name);
    return selector(Json{{"npc",name}});
}
DebugAi::Impl::SupplyPolicy DebugAi::Impl::parseSupplies(const Json &j) const {
    if(!j.is_object()) throw std::invalid_argument("supplies must be an object");
    const auto integer=[&](const char *key,int fallback,int lo,int hi) {
        if(!j.contains(key)) return fallback;
        const auto &value=j.at(key);if(!value.is_number_integer()) throw std::invalid_argument(std::string(key)+" must be an integer");
        const auto n=value.get<int64_t>();if(n<lo || n>hi) throw std::invalid_argument(std::string(key)+" outside supported range");
        return int(n);
    };
    SupplyPolicy p;p.enabled=j.value("enabled",true);p.buy=j.value("buy",true);p.returnWhenEmpty=j.value("returnWhenEmpty",true);
    p.fillPortalBook=j.value("fillPortalBook",true);
    p.lifePercent=integer("lifePercent",60,1,99);p.manaPercent=integer("manaPercent",35,1,99);
    p.criticalLifePercent=integer("criticalLifePercent",std::min(25,p.lifePercent),1,p.lifePercent);
    p.healthCount=integer("healthCount",4,0,16);p.manaCount=integer("manaCount",4,0,16);
    p.intervalMs=integer("intervalMs",1500,600,10000);return p;
}
DebugAi::Impl::PortalStock DebugAi::Impl::portalStock(const Context &c) const {
    PortalStock stock;unsigned maximum=0;
    for(size_t row=0;row<definitions->misc.rows().size();++row) if(definitions->misc.value(row,"code")=="tbk") {
        maximum=unsigned(std::max(0,definitions->misc.number(row,"maxstack").value_or(0)));break;
    }
    if(!maximum) throw std::runtime_error("Current MPQ has no verified town portal tome capacity");
    for(const auto &[id,item]:c.session.read().world.items) {
        if(item.code!="tbk" || !onlineItemOwnedBy(item,c.session.read().load.playerUnitId)) continue;
        if(!item.positionKnown) {stock.known=false;continue;}
        if(item.mode!=0 || item.page!=1) continue; // Only usable backpack books, never stash/cube contents.
        ++stock.books;stock.capacity+=maximum;
        const auto decoded=c.inventory.read().items.find(id);
        if(decoded==c.inventory.read().items.end() || !decoded->second.decoded || !decoded->second.quantity) stock.known=false;
        else stock.pages+=*decoded->second.quantity;
    }
    return stock;
}
unsigned DebugAi::Impl::potionCount(const Context &c,PotionKind kind) const {
    unsigned count=0;
    for(const auto &[id,item]:c.session.read().world.items) {
        (void)id;
        if(!item.positionKnown || !onlineItemOwnedBy(item,c.session.read().load.playerUnitId) || !(item.mode==2 || (item.mode==0 && item.page==1))) continue;
        const auto decoded=c.inventory.read().items.find(item.id);
        if(decoded==c.inventory.read().items.end() || !decoded->second.decoded) continue;
        const auto found=definitions->potions.find(item.code);
        if(found!=definitions->potions.end() && found->second.kind==kind) ++count;
    }
    return count;
}
bool DebugAi::Impl::recovering(const Context &c,std::optional<uint8_t> state) const {
    if(!state || !c.session.read().load.playerUnitId) return false;
    const auto player=c.combat.states().find({0,*c.session.read().load.playerUnitId});
    return player!=c.combat.states().end() && player->second.decoded && player->second.states.contains(*state);
}
bool DebugAi::Impl::observePotion(Context &c) {
    const auto &world=c.session.read().world;const auto now=Clock::now();
    if(lastPotion.is_object() && lastPotion.value("consumed",false) && !lastPotion.value("recoveryObserved",false) &&
        !lastPotion.value("observationClosed",false)) {
        const auto code=lastPotion.at("code").get<std::string>();const auto p=definitions->potions.find(code);
        const bool life=world.life && lastPotion.at("lifeBefore").is_number() && *world.life>lastPotion.at("lifeBefore").get<unsigned>();
        const bool mana=world.mana && lastPotion.at("manaBefore").is_number() && *world.mana>lastPotion.at("manaBefore").get<unsigned>();
        const bool effect=p!=definitions->potions.end() && (recovering(c,p->second.state) ||
            (p->second.kind==PotionKind::Life?life:p->second.kind==PotionKind::Mana?mana:life || mana));
        if(now>potionEffectUntil) {lastPotion["observationClosed"]=true;note("potion-recovery-unconfirmed",code);}
        else if(effect) {lastPotion["recoveryObserved"]=true;note("potion-recovery-observed",code);}
    }
    if(!potionRequest) return false;
    const auto request=*potionRequest;
    if(world.itemRequest && world.itemRequest->sequence!=request.sequence)
        throw std::runtime_error("Potion operation was replaced before its actual result was observed");
    if(request.buying) {
        const bool ack=world.tradeResult && world.tradeResult->revision>request.tradeRevision;
        if(ack && world.tradeResult->result>=7) throw std::runtime_error("Potion purchase rejected by the normal merchant service");
        const auto stock=request.portalSupply?portalStock(c):PortalStock{};
        const unsigned count=request.portalSupply?(request.code=="tbk"?stock.books:stock.pages):potionCount(c,request.kind);
        if(ack && (!request.portalSupply || stock.known) && count>request.beforeCount) {
            ++purchases;note("supply-purchase-confirmed",request.code);potionRequest.reset();shopDeadline=now+std::chrono::seconds(12);return false;
        }
    } else {
        const auto item=world.items.find(request.item);
        if(item==world.items.end() || !onlineItemOwnedBy(item->second,c.session.read().load.playerUnitId)) {
            ++drinks;lastPotion={{"item",request.item},{"code",request.code},{"consumed",true},{"recoveryObserved",false},
                {"lifeBefore",request.life?Json(*request.life):Json(nullptr)},{"manaBefore",request.mana?Json(*request.mana):Json(nullptr)}};
            note("potion-consumption-observed",request.code);potionRequest.reset();
            nextPotion=now+std::chrono::milliseconds(supplyPolicy.intervalMs);potionEffectUntil=now+std::chrono::seconds(12);return false;
        }
    }
    if(world.itemRequest && (world.itemRequest->state==OnlineItemRequest::State::Rejected ||
        world.itemRequest->state==OnlineItemRequest::State::Interrupted || world.itemRequest->state==OnlineItemRequest::State::TimedOut))
        throw std::runtime_error("Potion operation did not confirm its actual inventory result; it will not be repeated");
    if(now>=request.deadline) throw std::runtime_error(request.buying?
        "Purchase result/owned potion not observed; no blind purchase retry":"Potion consumption not observed; no blind use retry");
    wait("potion-wait",request.buying?"Waiting for native transaction result and an owned potion":"Waiting for actual potion consumption");return true;
}
bool DebugAi::Impl::drink(Context &c) {
    if(!supplyPolicy.enabled || potionRequest || !c.supplies || Clock::now()<nextPotion) return false;
    if(questRun.pending && questRun.pending->sequence) return false;
    const auto &view=c.session.read();const auto &world=view.world;
    if(onlineHasCursorItem(world.items,view.load.playerUnitId)) return false;
    const bool lowLife=world.life && c.supplies->maxLife && *c.supplies->maxLife>0 &&
        uint64_t(*world.life)*100<uint64_t(*c.supplies->maxLife)*supplyPolicy.lifePercent;
    const bool lowMana=world.mana && c.supplies->maxMana && *c.supplies->maxMana>0 &&
        uint64_t(*world.mana)*100<uint64_t(*c.supplies->maxMana)*supplyPolicy.manaPercent;
    if(!lowLife && !lowMana) return false;
    const bool critical=lowLife && uint64_t(*world.life)*100<uint64_t(*c.supplies->maxLife)*supplyPolicy.criticalLifePercent;
    bool healing=false,recharging=false;
    for(const auto &[code,potion]:definitions->potions) {
        (void)code;
        if(recovering(c,potion.state)) {
            healing|=potion.kind==PotionKind::Life;recharging|=potion.kind==PotionKind::Mana;
        }
    }
    const bool needsLife=lowLife && (!healing || critical),needsMana=lowMana && !recharging;
    if(!needsLife && !needsMana) return false;
    if(world.townPortalPending || (world.itemRequest && world.itemRequest->state==OnlineItemRequest::State::Pending) ||
        (world.combatRequest && world.combatRequest->state==OnlineCombatRequest::State::Pending)) return false;
    std::optional<OnlineItem> selected;int best=std::numeric_limits<int>::min();
    for(const auto &[id,item]:world.items) {
        (void)id;
        if(!item.positionKnown || !onlineItemOwnedBy(item,view.load.playerUnitId) || !(item.mode==2 || (item.mode==0 && item.page==1))) continue;
        const auto potion=definitions->potions.find(item.code);
        if(potion==definitions->potions.end()) continue;
        const auto &p=potion->second;const bool rejuvenation=p.kind==PotionKind::Rejuvenation;
        if(!rejuvenation && !((needsLife && p.kind==PotionKind::Life) || (needsMana && p.kind==PotionKind::Mana))) continue;
        if(recovering(c,p.state) && !(critical && p.kind==PotionKind::Life)) continue;
        const auto decoded=c.inventory.read().items.find(item.id);
        if(decoded==c.inventory.read().items.end() || !decoded->second.decoded) continue;
        int score=p.strength+(item.mode==2?1:0);
        if(rejuvenation) score+=critical?100000:needsLife?5000:-100000;
        else if(needsLife && p.kind==PotionKind::Life) score+=10000;
        if(score>best) {best=score;selected=item;}
    }
    if(!selected) return false;
    OnlineItemCommand use;use.action=OnlineItemAction::Use;use.item=selected->id;use.itemRevision=selected->revision;
    if(!submitItem(c,use,"potion-use",DebugActionOwner::Potion)) return false;
    PotionRequest pending;pending.item=selected->id;pending.code=selected->code;pending.kind=definitions->potions.at(selected->code).kind;
    pending.life=world.life;pending.mana=world.mana;pending.sequence=c.session.read().world.itemRequest->sequence;
    pending.deadline=Clock::now()+std::chrono::seconds(12);potionRequest=pending;
    note("potion-use-submitted",selected->code);mobilityHold=true;return true;
}
bool DebugAi::Impl::restock(Context &c) {
    const auto &view=c.session.read();const auto &world=view.world;
    const bool life=potionCount(c,PotionKind::Life)<unsigned(supplyPolicy.healthCount);
    const bool mana=potionCount(c,PotionKind::Mana)<unsigned(supplyPolicy.manaCount);
    const auto stock=portalStock(c);
    if(supplyPolicy.fillPortalBook && !stock.known) {
        if(shopDeadline==Clock::time_point{}) shopDeadline=Clock::now()+std::chrono::seconds(12);
        if(Clock::now()>=shopDeadline) throw std::runtime_error("Owned town portal tome quantity remains unknown");
        wait("supply-book-observation","Waiting for actual backpack tome quantities");return false;
    }
    const bool portal=supplyPolicy.fillPortalBook && (!stock.books || stock.pages<stock.capacity);
    if(!life && !mana && !portal) {
        if(world.npcRequested && supplyNpc && world.npcRequested==supplyNpc->id) c.session.close_npc();
        shopIssued=false;shelfObserved=false;supplyNpc.reset();return true;
    }
    if(!view.load.act || !c.town.read().town) throw std::runtime_error("Potion restock requires the current act's native town");
    const auto supplier=definitions->supplier(*view.load.act,portal);const auto npc=find(c,supplier);
    if(shopIssued && supplyNpc && (!npc || *supplyNpc!=npc->key)) {
        c.session.close_npc();cancelWalk(c);shopIssued=false;shelfObserved=false;supplyNpc.reset();shopDeadline={};return false;
    }
    if(!npc) {if(!approachKnown(c,supplier)) explore(c,"potion supplier "+supplier.name);return false;}
    supplyNpc=npc->key;
    if(shopIssued && shopGeneration!=world.interactionGeneration) {shopIssued=false;shelfObserved=false;shopDeadline={};}
    if(world.npcRequested && world.npcRequested!=npc->key.id) {c.session.close_npc();return false;}
    if(!world.npcConversation || world.npcConversation->type!=1 || world.npcConversation->source!=npc->key.id) {
        wait("supply-approach","Finding and opening the normal potion merchant conversation");
        walk(c,{{"goal","unit"},{"unitType",1},{"unitId",npc->key.id},{"interact",true}});return false;
    }
    if(!shopIssued) {
        OnlineItemCommand open;open.action=OnlineItemAction::TradeOpen;open.npc=npc->key.id;
        if(submitItem(c,open,"supply-shop",DebugActionOwner::Supply)) {
            shopIssued=true;shelfObserved=false;shopGeneration=c.session.read().world.interactionGeneration;
            shopDeadline=Clock::now()+std::chrono::seconds(12);cancelWalk(c);note("potion-shop-submitted");
        }
        return false;
    }
    if(world.shopSource!=npc->key.id || world.shopGamble ||
        (world.itemRequest && world.itemRequest->state==OnlineItemRequest::State::Pending)) {
        if(Clock::now()>=shopDeadline) throw std::runtime_error("Potion vendor shelf was not observed; shop request is not repeated");
        wait("supply-shop","Waiting for the real merchant shelf");return false;
    }
    std::optional<OnlineItem> best;unsigned price{};int strength=std::numeric_limits<int>::min();
    bool offered=false,quoted=false,affordable=false,knownAffordable=false;
    const auto gold=world.playerAttributes.find(14),bank=world.playerAttributes.find(15);
    const std::optional<uint64_t> funds=gold!=world.playerAttributes.end() && bank!=world.playerAttributes.end()?
        std::optional<uint64_t>{uint64_t(gold->second)+bank->second}:std::nullopt;
    const uint64_t knownFunds=(gold!=world.playerAttributes.end()?uint64_t(gold->second):0)+
        (bank!=world.playerAttributes.end()?uint64_t(bank->second):0);
    for(const auto &[id,item]:world.items) {
        if(item.action!=11 || item.mode!=0 || item.ownerType!=1 || item.owner!=npc->key.id) continue;
        const auto p=definitions->potions.find(item.code);
        if(portal) {if(item.code!=(stock.books?"tsc":"tbk")) continue;}
        else {
            if(p==definitions->potions.end()) continue;
            if((life && p->second.kind!=PotionKind::Life) || (!life && (!mana || p->second.kind!=PotionKind::Mana))) continue;
        }
        offered=true;const auto quote=c.supplies && c.supplies->buyQuote?c.supplies->buyQuote(id):std::nullopt;
        if(!quote) continue;
        quoted=true;if(funds && *funds<*quote) continue;
        affordable=true;const bool known=knownFunds>=*quote;
        const int potency=portal?0:p->second.strength;
        if(!best || (known && !knownAffordable) || (known==knownAffordable && potency>strength)) {
            strength=potency;best=item;price=*quote;knownAffordable=known;
        }
    }
    if(!best) {
        if(Clock::now()<shopDeadline && (!offered || !quoted)) {wait("supply-offers","Waiting for actual potion stock and known normal prices");return false;}
        throw std::runtime_error(!offered?"Potion merchant did not offer the required potion":!quoted?
            "Potion purchase price is unknown; no zero-price request is invented":!affordable?"Insufficient observed gold for a potion":"No usable potion offer");
    }
    const auto kind=portal?PotionKind::Life:definitions->potions.at(best->code).kind;
    const auto before=portal?(stock.books?stock.pages:stock.books):potionCount(c,kind);
    const auto trade=world.tradeResult?world.tradeResult->revision:0;
    OnlineItemCommand buy;buy.action=OnlineItemAction::Buy;buy.item=best->id;buy.itemRevision=best->revision;buy.npc=npc->key.id;buy.amount=price;
    buy.multibuy=portal && stock.books; // Native Shift-buy fills the actual owned tome; never invent a quantity.
    if(!submitItem(c,buy,"supply-buy",DebugActionOwner::Supply)) return false;
    PotionRequest pending;pending.buying=true;pending.portalSupply=portal;pending.item=best->id;pending.code=best->code;pending.kind=kind;
    pending.beforeCount=before;pending.tradeRevision=trade;pending.sequence=c.session.read().world.itemRequest->sequence;
    pending.deadline=Clock::now()+std::chrono::seconds(12);potionRequest=pending;note("potion-purchase-submitted",best->code);return false;
}
bool DebugAi::Impl::autoSupply(Context &c,const Goal &goal) {
    if(!supplyPolicy.enabled || !supplyPolicy.buy || goal.kind==Kind::Restock) return false;
    if(supplyStage!=SupplyStage::None) return supplyDetour(c,goal);
    if(questTransaction(c)) return false;
    const auto &world=c.session.read().world;
    if(potionRequest || objectIssued || pickupIssued || portalAttempted || portalIssued || pendingDoor || mobilityTarget ||
        world.npcRequested || world.waypointSource || world.waypointRequested || world.townPortalPending ||
        world.storage.kind!=OnlineStorageKind::None || world.storage.requested!=OnlineStorageKind::None ||
        (goal.kind==Kind::Deliver && actionDeadline!=Clock::time_point{}) ||
        (world.itemRequest && world.itemRequest->state==OnlineItemRequest::State::Pending) ||
        (world.combatRequest && world.combatRequest->state==OnlineCombatRequest::State::Pending) ||
        travelPhase==TravelPhase::WaypointWait || travelPhase==TravelPhase::CrossWait) return false;
    const auto stock=portalStock(c);
    const bool needs=potionCount(c,PotionKind::Life)<unsigned(supplyPolicy.healthCount) || potionCount(c,PotionKind::Mana)<unsigned(supplyPolicy.manaCount) ||
        (supplyPolicy.fillPortalBook && (!stock.known || !stock.books || stock.pages<stock.capacity));
    bool empty=false;
    if(c.supplies && supplyPolicy.returnWhenEmpty && !potionCount(c,PotionKind::Rejuvenation)) {
        empty=(supplyPolicy.healthCount && !potionCount(c,PotionKind::Life) && world.life && c.supplies->maxLife &&
            *c.supplies->maxLife>0 && uint64_t(*world.life)*100<uint64_t(*c.supplies->maxLife)*supplyPolicy.lifePercent) ||
            (supplyPolicy.manaCount && !potionCount(c,PotionKind::Mana) && world.mana && c.supplies->maxMana &&
            *c.supplies->maxMana>0 && uint64_t(*world.mana)*100<uint64_t(*c.supplies->maxMana)*supplyPolicy.manaPercent);
    }
    // An observed boss plus real damage/low life is evidence of pressure;
    // the boss label alone is not enough to abandon an otherwise safe fight.
    bool bossPressure=false;
    if(supplyPolicy.healthCount && c.supplies && c.supplies->maxLife && *c.supplies->maxLife>0 && world.life &&
        !potionCount(c,PotionKind::Life) && !potionCount(c,PotionKind::Rejuvenation)) {
        uint16_t peak=*world.life;for(const auto &sample:combatLifeSamples) peak=std::max(peak,sample.life);
        const bool hurt=uint64_t(*world.life)*100<uint64_t(*c.supplies->maxLife)*70 ||
            uint64_t(peak-*world.life)*100>=uint64_t(*c.supplies->maxLife)*policy.lifeDropPercent;
        if(hurt) for(const auto &[key,unit]:world.units) {
            (void)key;
            if(!unit.position || onlineMonsterCorpse(unit) || !c.combat.hostile(unit) ||
                (position(*unit.position)-c.position).length()>policy.engage) continue;
            if(const auto id=c.combat.monsterClass(unit)) if(const auto row=findCompiledMonsterRow(definitions->monsters,*id))
                bossPressure|=definitions->monsters.number(*row,"boss").value_or(0)!=0;
        }
    }
    empty|=supplyPolicy.returnWhenEmpty && bossPressure;
    if(!(c.town.read().town?needs:empty)) return false;
    supplyOrigin=c.town.read().area;
    // Do not return to the field only to execute an already queued trip to
    // this same town (including delivery/find goals whose area is the town).
    if(goal.kind==Kind::ReturnTown || (goal.area && goal.area==c.session.read().load.townArea))
        supplyOrigin=c.session.read().load.townArea;
    supplyStarted=Clock::now();supplyDeadline=supplyStarted+std::chrono::seconds(600);
    stop(c.walk,c.control,c.combat);resetStep();combatTarget.reset();retreatPoint.reset();
    supplyStage=c.town.read().town?SupplyStage::Shop:SupplyStage::ToTown;
    shopIssued=false;shelfObserved=false;supplyNpc.reset();supplyPortalUsed=false;
    note("supply-detour-started",bossPressure?"Observed boss pressure without healing; retaining the original goal":"Preparing normal travel reserves");return true;
}
bool DebugAi::Impl::supplyDetour(Context &c,const Goal &) {
    const auto now=Clock::now();
    if(now>=supplyDeadline) throw std::runtime_error("Automatic potion supply detour exceeded 600 seconds");
    Goal movement;movement.kind=Kind::Travel;
    const bool transfer=portalIssued || supplyPortalUsed || travelPhase==TravelPhase::WaypointWait ||
        (travelPhase==TravelPhase::CrossWait && legUnit);
    if(!transfer && supplyStage!=SupplyStage::ToTown && supplyCombat(c,movement)) return true;
    if(supplyStage==SupplyStage::ToTown) {
        if(reachTown(c)) {resetStep();supplyStage=SupplyStage::Shop;shopIssued=false;shelfObserved=false;supplyNpc.reset();}
    } else if(supplyStage==SupplyStage::Shop) {
        if(restock(c)) {resetStep();supplyStage=SupplyStage::Return;}
    } else if(supplyStage==SupplyStage::Return) {
        if(c.town.read().area==supplyOrigin) {
            resetStep();supplyStage=SupplyStage::None;goalDeadline+=now-supplyStarted;supplyOrigin.reset();supplyPortalUsed=false;
            note("supply-detour-completed","Original high-level goal retained");return true;
        }
        if(supplyPortalUsed) {
            if(now>=supplyPortalDeadline) throw std::runtime_error("Return portal was used but the original supply area was not observed");
            wait("supply-return-wait","Waiting for the original area's native scene");return true;
        }
        const auto &view=c.session.read();
        if(c.town.read().town) for(const auto &entry:c.town.read().mapTargets) {
            const auto u=view.world.units.find(entry.unit);
            if(entry.interaction!=OnlineMapInteraction::Portal || entry.destination!=supplyOrigin || u==view.world.units.end() || u->second.portalOwner!=view.load.playerUnitId) continue;
            wait("supply-return","Returning through the actually assigned own portal");
            if(walk(c,{{"goal","unit"},{"unitType",entry.unit.type},{"unitId",entry.unit.id},{"interact",true},{"purpose","supply-return"}})) {
                supplyPortalUsed=true;supplyPortalDeadline=now+std::chrono::seconds(15);cancelWalk(c);
            }
            return true;
        }
        if(supplyOrigin) travel(c,*supplyOrigin);
    }
    if(!portalIssued && !supplyPortalUsed) mobility(c);
    return true;
}
bool DebugAi::Impl::supplyCombat(Context &c,const Goal &goal) {
    if(!supplyPolicy.enabled) return combat(c,goal);
    const auto &world=c.session.read().world;
    const bool noRejuvenation=!potionCount(c,PotionKind::Rejuvenation);
    const bool dry=noRejuvenation && c.supplies &&
        ((!potionCount(c,PotionKind::Mana) && world.mana && c.supplies->maxMana && *c.supplies->maxMana>0 &&
            uint64_t(*world.mana)*100<uint64_t(*c.supplies->maxMana)*supplyPolicy.manaPercent) ||
        (!potionCount(c,PotionKind::Life) && world.life && c.supplies->maxLife && *c.supplies->maxLife>0 &&
            uint64_t(*world.life)*100<uint64_t(*c.supplies->maxLife)*supplyPolicy.criticalLifePercent));
    if(!dry) return combat(c,goal);
    // A supply trip must keep trying the normal escape route rather than
    // standing still and submitting an unusable fixed spell indefinitely.
    if(combatTarget || combatOwned) {stop(c.walk,c.control,c.combat);walkIntent=nullptr;combatTarget.reset();retreatPoint.reset();}
    return false;
}
}
