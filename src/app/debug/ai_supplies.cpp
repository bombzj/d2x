#include "ai_impl.hpp"
#include "content/world/world_catalog.hpp"
#include "client/inventory_actions.hpp"
#include "content/npc/vendor_stock.hpp"
#include "gameplay/items/belt_rules.hpp"
#include "gameplay/items/equipment_rules.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace d2x {
namespace {
Vec position(OnlinePoint p) {return {float(p.x),float(p.y)};}
// GetFreeBeltSlot can use matching bottom-row columns, then unclaimed columns.
// Count the maximum a Shift-click could buy before committing the single request.
unsigned bulkBeltSpace(const OnlineView &view,int slots,std::string_view code) {
    if(slots<4 || slots>16 || slots%4) return 0;
    std::array<const OnlineItem *,16> belt{};
    for(const auto &[id,item]:view.world.items) {
        if(item.mode!=2 || !onlineItemOwnedBy(item,view.load.playerUnitId)) continue;
        if(!item.positionKnown || item.x>=slots || belt[item.x]) return 0;
        belt[item.x]=&item;
    }
    unsigned space=0;
    const auto family=beltPotionFamily(code);
    for(int column=0;column<4;++column) {
        const auto *bottom=belt[column];
        if(bottom && bottom->code!=code && (family.empty() || beltPotionFamily(bottom->code)!=family)) continue;
        for(int slot=column;slot<slots;slot+=4) if(!belt[slot]) ++space;
    }
    return space;
}
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
        potion.bulkBelt=misc.number(row,"autobelt").value_or(0) && misc.number(row,"PermStoreItem").value_or(0);
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
std::optional<DebugAi::Impl::Selector> DebugAi::Impl::Definitions::quiverSupplier(unsigned act,std::string_view type) const {
    static constexpr std::array names{"charsi","fara","asheara","halbu","larzuk"};
    if(act>=names.size()) return {};
    const std::string name=names[act];
    for(size_t row=0;row<misc.rows().size();++row)
        if(misc.value(row,"type")==type && misc.number(row,name+"Max").value_or(0)>0)
            return selector(Json{{"npc",name}});
    return {};
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
    p.quiverCount=integer("quiverCount",3,0,6);
    p.intervalMs=integer("intervalMs",1500,600,10000);return p;
}
DebugAi::Impl::PortalStock DebugAi::Impl::portalStock(const Context &c) const {
    PortalStock stock;unsigned maximum=0;
    for(size_t row=0;row<definitions->misc.rows().size();++row) if(definitions->misc.value(row,"code")=="tbk") {
        maximum=unsigned(std::max(0,definitions->misc.number(row,"maxstack").value_or(0)));break;
    }
    if(!maximum) throw std::runtime_error("Current MPQ has no verified town portal tome capacity");
    for(const auto &[id,item]:c.observation.read().world.items) {
        if(item.code!="tbk" || !onlineItemOwnedBy(item,c.observation.read().load.playerUnitId)) continue;
        if(!item.positionKnown) {stock.known=false;continue;}
        if(item.mode!=0 || item.page!=1) continue; // Only usable backpack books, never stash/cube contents.
        ++stock.books;stock.capacity+=maximum;
        const auto decoded=c.inventory.read().items.find(id);
        if(decoded==c.inventory.read().items.end() || !decoded->second.decoded || !decoded->second.quantity) stock.known=false;
        else stock.pages+=*decoded->second.quantity;
    }
    return stock;
}
DebugAi::Impl::QuiverStock DebugAi::Impl::quiverStock(const Context &c) const {
    QuiverStock stock;
    if(!battle.excursionSkill) return stock;
    const auto row=definitions->skillRows.find(*battle.excursionSkill);
    if(row==definitions->skillRows.end() || definitions->skills.number(row->second,"noammo").value_or(0)) return stock;
    const auto &view=c.observation.read();
    std::array<const OnlineItem *,2> hands{};
    for(const auto &[id,item]:view.world.items)
        if(item.mode==1 && (item.body==4 || item.body==5) && onlineItemOwnedBy(item,view.load.playerUnitId))
            hands[item.body-4]=&item;
    EquipmentDefinition weapon;
    for(size_t hand=0;hand<hands.size();++hand) {
        if(!hands[hand] || hands[hand]->flags&0x4100u) continue;
        weapon=c.inventory.equipmentDefinition(*hands[hand]);
        if(weapon.shoots.empty()) continue;
        stock.type=weapon.shoots;stock.body=uint8_t(5-hand);stock.equipped=hands[1-hand];
        break;
    }
    if(!stock.body) return stock;
    const auto quantity=[&](const OnlineItem &item) -> std::optional<unsigned> {
        const auto found=c.inventory.read().items.find(item.id);
        if(found==c.inventory.read().items.end() || !found->second.decoded ||
            found->second.revision!=item.revision || !found->second.identified) return {};
        return found->second.quantity;
    };
    const auto compatible=[&](const OnlineItem &item) {
        const auto base=c.inventory.equipmentDefinition(item);
        return !base.quiver.empty() && base.isType(stock.type) && weapon.isType(base.quiver);
    };
    if(!stock.equipped) stock.equippedQuantity=0;
    else if(compatible(*stock.equipped)) stock.equippedQuantity=quantity(*stock.equipped);
    if(stock.equippedQuantity.value_or(0)>0) {
        ++stock.stacks;stock.rounds+=*stock.equippedQuantity;
    }
    unsigned reserveRounds=0;
    for(const auto &[id,item]:view.world.items) {
        if(!item.positionKnown || !onlineItemOwnedBy(item,view.load.playerUnitId) ||
            !(item.mode==4 || (item.mode==0 && item.page==1)) ||
            (item.mode==4 && (!supply.quiverTransfer || supply.quiverTransfer->item!=id))) continue;
        const auto count=quantity(item).value_or(0);
        if(!count || !compatible(item)) continue;
        ++stock.stacks;stock.rounds+=count;
        if(item.mode==0 && count>reserveRounds) {stock.reserve=&item;reserveRounds=count;}
    }
    return stock;
}
void DebugAi::Impl::observeQuiver(Context &c) {
    if(!supply.quiverTransfer || !supply.quiverTransfer->sequence) return;
    const auto &pending=*supply.quiverTransfer;
    const auto &view=c.observation.read();const auto &request=view.world.itemRequest;
    if(!request || request->sequence!=pending.sequence)
        throw std::runtime_error("Ammunition inventory request was replaced");
    const auto item=view.world.items.find(pending.item);
    if(item!=view.world.items.end() && onlineItemOwnedBy(item->second,view.load.playerUnitId) &&
        (pending.equipping?item->second.mode==1 && item->second.body==pending.body:item->second.mode==4)) {
        note(pending.equipping?"quiver-equipped":"quiver-taken",std::to_string(pending.item));
        if(pending.equipping) supply.quiverTransfer.reset();
        else supply.quiverTransfer->sequence.reset();
        return;
    }
    if(request->state==OnlineItemRequest::State::Rejected || request->state==OnlineItemRequest::State::Interrupted ||
        request->state==OnlineItemRequest::State::TimedOut || Clock::now()>=pending.deadline)
        throw std::runtime_error("Ammunition inventory result was not confirmed");
}
bool DebugAi::Impl::reloadQuiver(Context &c) {
    if(!supplyPolicy.enabled && !supply.quiverTransfer) return false;
    if(supply.quiverTransfer && supply.quiverTransfer->sequence) {
        wait("quiver-result","Waiting for the actual owned quiver position");return true;
    }
    const auto &view=c.observation.read();const auto &world=view.world;
    const auto stock=quiverStock(c);
    if(!supply.quiverTransfer) {
        if(!stock.empty() || stock.equipped || !stock.reserve ||
            (world.combatRequest && world.combatRequest->state==OnlineCombatRequest::State::Pending) || questTransaction(c) || questState.questRun.pending ||
            supply.potionRequest || pickup || portalIssued || world.storage.kind!=OnlineStorageKind::None ||
            world.storage.requested!=OnlineStorageKind::None || onlineHasCursorItem(world.items,view.load.playerUnitId) ||
            (world.itemRequest && world.itemRequest->state==OnlineItemRequest::State::Pending)) return false;
        if(world.npcRequested) {
            if(!supply.shopIssued || !supply.supplyNpc || world.npcRequested!=supply.supplyNpc->id) return false;
            if(supply.supplyStage==SupplyStage::Shop && !supply.quiverTopUpDeferred && stock.stacks<unsigned(supplyPolicy.quiverCount)) return false;
            if(c.control.closeNpc()) {supply.shopIssued=false;supply.shelfObserved=false;supply.supplyNpc.reset();supply.shopDeadline={};}
            wait("quiver-close-shop","Closing the completed ammunition transaction before equipping");return true;
        }
        if(navigation.routeExecution && navigation.routeExecution->phase==RouteExecution::Phase::Relocate)
            navigation.routeExecution->resumeRelocation=true;
        releaseCombat(c);cancelWalk(c);
        OnlineItemCommand take;take.action=OnlineItemAction::Take;take.item=stock.reserve->id;take.itemRevision=stock.reserve->revision;
        if(submitItem(c,take,"quiver-take",DebugActionOwner::Supply))
            supply.quiverTransfer=QuiverTransfer{take.item,stock.body,c.observation.read().world.itemRequest->sequence,false,Clock::now()+std::chrono::seconds(12)};
        return true;
    }
    const auto item=world.items.find(supply.quiverTransfer->item);
    if(!stock.empty() || stock.equipped || stock.body!=supply.quiverTransfer->body || item==world.items.end() ||
        item->second.mode!=4 || !onlineItemOwnedBy(item->second,view.load.playerUnitId))
        throw std::runtime_error("Quiver destination or owned cursor assignment changed before equip");
    OnlineItemCommand equip;equip.action=OnlineItemAction::Equip;equip.item=item->first;
    equip.itemRevision=item->second.revision;equip.body=stock.body;
    if(submitItem(c,equip,"quiver-equip",DebugActionOwner::Supply)) {
        supply.quiverTransfer->sequence=c.observation.read().world.itemRequest->sequence;
        supply.quiverTransfer->equipping=true;supply.quiverTransfer->deadline=Clock::now()+std::chrono::seconds(12);
    }
    return true;
}
unsigned DebugAi::Impl::potionCount(const Context &c,PotionKind kind) const {
    unsigned count=0;
    for(const auto &[id,item]:c.observation.read().world.items) {
        (void)id;
        if(!item.positionKnown || !onlineItemOwnedBy(item,c.observation.read().load.playerUnitId) || !(item.mode==2 || (item.mode==0 && item.page==1))) continue;
        const auto decoded=c.inventory.read().items.find(item.id);
        if(decoded==c.inventory.read().items.end() || !decoded->second.decoded) continue;
        const auto found=definitions->potions.find(item.code);
        if(found!=definitions->potions.end() && found->second.kind==kind) ++count;
    }
    return count;
}
bool DebugAi::Impl::recovering(const Context &c,std::optional<uint8_t> state) const {
    if(!state || !c.observation.read().load.playerUnitId) return false;
    const auto player=c.combat.states().find({0,*c.observation.read().load.playerUnitId});
    return player!=c.combat.states().end() && player->second.decoded && player->second.states.contains(*state);
}
bool DebugAi::Impl::observePotion(Context &c) {
    const auto &world=c.observation.read().world;const auto now=Clock::now();
    if(supply.lastPotion.is_object() && supply.lastPotion.value("consumed",false) && !supply.lastPotion.value("recoveryObserved",false) &&
        !supply.lastPotion.value("observationClosed",false)) {
        const auto code=supply.lastPotion.at("code").get<std::string>();const auto p=definitions->potions.find(code);
        const bool life=world.life && supply.lastPotion.at("lifeBefore").is_number() && *world.life>supply.lastPotion.at("lifeBefore").get<unsigned>();
        const bool mana=world.mana && supply.lastPotion.at("manaBefore").is_number() && *world.mana>supply.lastPotion.at("manaBefore").get<unsigned>();
        const bool effect=p!=definitions->potions.end() && (recovering(c,p->second.state) ||
            (p->second.kind==PotionKind::Life?life:p->second.kind==PotionKind::Mana?mana:life || mana));
        if(now>supply.potionEffectUntil) {supply.lastPotion["observationClosed"]=true;note("potion-recovery-unconfirmed",code);}
        else if(effect) {supply.lastPotion["recoveryObserved"]=true;note("potion-recovery-observed",code);}
    }
    if(!supply.potionRequest) return false;
    const auto request=*supply.potionRequest;
    if(world.itemRequest && world.itemRequest->sequence!=request.sequence)
        throw std::runtime_error("Potion operation was replaced before its actual result was observed");
    if(request.buying) {
        const bool ack=world.tradeResult && world.tradeResult->revision>request.tradeRevision;
        if(ack && world.tradeResult->result>=7) throw std::runtime_error("Supply purchase rejected by the normal merchant service");
        const auto stock=request.portalSupply?portalStock(c):PortalStock{};
        const unsigned count=request.quiverSupply?quiverStock(c).rounds:
            request.portalSupply?(request.code=="tbk"?stock.books:stock.pages):potionCount(c,request.kind);
        if(ack && (!request.portalSupply || stock.known) && count>=request.beforeCount+request.expectedGain) {
            ++supply.purchases;note("supply-purchase-confirmed",request.code+" +"+std::to_string(count-request.beforeCount));supply.potionRequest.reset();supply.shopDeadline=now+std::chrono::seconds(12);return false;
        }
    } else {
        const auto item=world.items.find(request.item);
        if(item==world.items.end() || !onlineItemOwnedBy(item->second,c.observation.read().load.playerUnitId)) {
            ++supply.drinks;supply.lastPotion={{"item",request.item},{"code",request.code},{"consumed",true},{"recoveryObserved",false},
                {"lifeBefore",request.life?Json(*request.life):Json(nullptr)},{"manaBefore",request.mana?Json(*request.mana):Json(nullptr)}};
            note("potion-consumption-observed",request.code);supply.potionRequest.reset();
            supply.nextPotion=now+std::chrono::milliseconds(supplyPolicy.intervalMs);supply.potionEffectUntil=now+std::chrono::seconds(12);return false;
        }
    }
    if(world.itemRequest && (world.itemRequest->state==OnlineItemRequest::State::Rejected ||
        world.itemRequest->state==OnlineItemRequest::State::Interrupted || world.itemRequest->state==OnlineItemRequest::State::TimedOut))
        throw std::runtime_error("Potion operation did not confirm its actual inventory result; it will not be repeated");
    if(now>=request.deadline) throw std::runtime_error(request.buying?
        "Purchase result/owned supply not observed; no blind purchase retry":"Potion consumption not observed; no blind use retry");
    wait("potion-wait",request.buying?"Waiting for native transaction result and owned stock":"Waiting for actual potion consumption");return true;
}
bool DebugAi::Impl::drink(Context &c) {
    if(!supplyPolicy.enabled || supply.potionRequest || !c.supplies || Clock::now()<supply.nextPotion) return false;
    if(c.town.read().town && supplyPolicy.buy) return false;
    if(questState.questRun.pending && questState.questRun.pending->sequence) return false;
    const auto &view=c.observation.read();const auto &world=view.world;
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
    // Native potion use is independent of a cast's action/receipt. Keep the
    // inventory transaction serialized, not the movement/skill domain.
    if(world.itemRequest && world.itemRequest->state==OnlineItemRequest::State::Pending) return false;
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
    const auto use = selected->mode == 2 && selected->x < 4 ? useBeltColumnCommand(view, selected->x) :
        std::optional{useItemCommand(selected->id, selected->revision)};
    if (!use || use->item != selected->id || use->itemRevision != selected->revision) return false;
    if(!submitItem(c,*use,"potion-use",DebugActionOwner::Potion)) return false;
    PotionRequest pending;pending.item=selected->id;pending.code=selected->code;pending.kind=definitions->potions.at(selected->code).kind;
    pending.life=world.life;pending.mana=world.mana;pending.sequence=c.observation.read().world.itemRequest->sequence;
    pending.deadline=Clock::now()+std::chrono::seconds(12);supply.potionRequest=pending;
    note("potion-use-submitted",selected->code);navigation.mobilityHold=true;return true;
}
bool DebugAi::Impl::restock(Context &c) {
    const auto &view=c.observation.read();const auto &world=view.world;
    // A few missing points regenerate; a healer walk costs seconds per town visit.
    const bool injured=world.life && c.supplies && c.supplies->maxLife && *c.supplies->maxLife>0 &&
        uint64_t(*world.life)*100<uint64_t(*c.supplies->maxLife)*90;
    if(supply.healingIssued) {
        if(!supply.healingObserved && (!world.life || !c.supplies || !c.supplies->maxLife || *world.life<*c.supplies->maxLife)) {
            if(Clock::now()>=supply.shopDeadline) throw std::runtime_error("Town healer interaction did not restore observed life; no blind retry");
            wait("supply-heal-wait","Waiting for actual life recovery from the normal healer conversation");return false;
        }
        if(supply.supplyNpc && world.npcRequested==supply.supplyNpc->id) {
            if(!prepareNpcService(c,*supply.supplyNpc,DebugActionOwner::Supply)) return false;
        }
        supply.healingIssued=false;supply.healingObserved=false;supply.shopDeadline={};note("town-healing-observed");return false;
    }
    if(injured) {
        if(!view.load.act || !c.town.read().town) throw std::runtime_error("Town recovery requires the native town");
        std::optional<Selector> healer;
        for(size_t row=0;row<definitions->monsters.rows().size();++row) {
            const auto code=definitions->monsters.value(row,"Id");
            if(!npcCanHeal(code)) continue;
            const auto candidate=definitions->selector(Json{{"npc",code}});
            // Prefer a currently assigned healer. Otherwise use the act's
            // verified healer; Act II's potion supplier cannot heal.
            if(find(c,candidate)) {healer=candidate;break;}
        }
        if(!healer) {
            static constexpr std::array names{"akara","fara","ormus","jamella","malah"};
            if(*view.load.act>=names.size()) throw std::runtime_error("No verified town healer for this act");
            healer=definitions->selector(Json{{"npc",names[*view.load.act]}});
        }
        const auto npc=find(c,*healer);
        if(!npc) {if(!approachKnown(c,*healer)) explore(c,"town healer");return false;}
        supply.supplyNpc=npc->key;
        if(walk(c,{.goal=DebugWalkGoal::Unit,.unit=OnlineUnitKey{1,npc->key.id},.interact=true,.purpose="supply-heal"})) {
            supply.healingIssued=true;supply.healingObserved=false;supply.shopDeadline=Clock::now()+std::chrono::seconds(12);cancelWalk(c);
        }
        wait("supply-heal","Approaching the normal town healer before departure");return false;
    }
    const bool life=potionCount(c,PotionKind::Life)<unsigned(supplyPolicy.healthCount);
    const bool mana=potionCount(c,PotionKind::Mana)<unsigned(supplyPolicy.manaCount);
    const auto stock=portalStock(c);
    if(supplyPolicy.fillPortalBook && !stock.known) {
        if(supply.shopDeadline==Clock::time_point{}) supply.shopDeadline=Clock::now()+std::chrono::seconds(12);
        if(Clock::now()>=supply.shopDeadline) throw std::runtime_error("Owned town portal tome quantity remains unknown");
        wait("supply-book-observation","Waiting for actual backpack tome quantities");return false;
    }
    // Top up a part-used tome only when the merchant is already visited for potions.
    const bool portal=supplyPolicy.fillPortalBook && (!stock.books || stock.pages<=stock.capacity/2 ||
        ((life || mana) && stock.pages<stock.capacity));
    const auto quivers=quiverStock(c);
    const bool ammo=!life && !mana && !portal && quivers.equippedQuantity && !supply.quiverTopUpDeferred &&
        quivers.stacks<unsigned(supplyPolicy.quiverCount);
    if(!life && !mana && !portal && !ammo) {
        if(world.npcRequested && supply.supplyNpc && world.npcRequested==supply.supplyNpc->id) c.control.closeNpc();
        supply.shelfReopens=0;supply.shopIssued=false;supply.shelfObserved=false;supply.supplyNpc.reset();return true;
    }
    if(!view.load.act || !c.town.read().town) throw std::runtime_error("Potion restock requires the current act's native town");
    const auto ammoSupplier=ammo?definitions->quiverSupplier(*view.load.act,quivers.type):std::nullopt;
    if(ammo && !ammoSupplier) {
        if(!quivers.stacks) throw std::runtime_error("Current MPQ has no verified supplier for required ammunition");
        supply.quiverTopUpDeferred=true;note("quiver-top-up-deferred","no verified ammunition merchant in the current MPQ");return false;
    }
    const auto supplier=ammo?*ammoSupplier:definitions->supplier(*view.load.act,portal);
    const auto npc=find(c,supplier);
    // Retail healers can return to their service menu after the last line.
    // Close it before walking to a different supplier; reuse it for the same NPC.
    const bool changingNpc=world.npcRequested && (!npc || world.npcRequested!=npc->key.id);
    if(changingNpc || (supply.shopIssued && supply.supplyNpc && (!npc || *supply.supplyNpc!=npc->key))) {
        if(changingNpc && !c.control.closeNpc()) {
            wait("supply-npc-close","Waiting to close the previous NPC before approaching the merchant");return false;
        }
        cancelWalk(c);supply.shopIssued=false;supply.shelfObserved=false;supply.supplyNpc.reset();supply.shopDeadline={};return false;
    }
    if(!npc) {if(!approachKnown(c,supplier)) explore(c,"supply merchant "+supplier.name);return false;}
    supply.supplyNpc=npc->key;
    if(supply.shopIssued && supply.shopGeneration!=world.interactionGeneration) {supply.shopIssued=false;supply.shelfObserved=false;supply.shopDeadline={};}
    if(!supply.shopIssued) {
        if (!prepareNpcService(c, npc->key, DebugActionOwner::Supply)) return false;
        OnlineItemCommand open;open.action=OnlineItemAction::TradeOpen;open.npc=npc->key.id;
        if(submitItem(c,open,"supply-shop",DebugActionOwner::Supply)) {
            supply.shopIssued=true;supply.shelfObserved=false;supply.shopGeneration=c.observation.read().world.interactionGeneration;
            supply.shopDeadline=Clock::now()+std::chrono::seconds(12);cancelWalk(c);note("supply-shop-submitted",supplier.name);
        }
        return false;
    }
    if(world.shopSource!=npc->key.id || world.shopGamble ||
        (world.itemRequest && world.itemRequest->state==OnlineItemRequest::State::Pending)) {
        if(Clock::now()>=supply.shopDeadline) {
            if(supply.shelfReopens || (world.itemRequest && world.itemRequest->state==OnlineItemRequest::State::Pending))
                throw std::runtime_error("Potion vendor shelf unavailable after bounded shop observation");
            if(c.control.closeNpc()) {
                ++supply.shelfReopens;supply.shopIssued=false;supply.shelfObserved=false;supply.shopDeadline={};cancelWalk(c);
                note("merchant-reopen","No shelf or pending transaction; reopening the conversation once");
            }
            return false;
        }
        wait("supply-shop","Waiting for the real merchant shelf");return false;
    }
    std::optional<OnlineItem> best;unsigned price{};int strength=std::numeric_limits<int>::min();
    bool offered=false,quoted=false,affordable=false,knownAffordable=false,spaceUnavailable=false;
    const auto gold=world.playerAttributes.find(14),bank=world.playerAttributes.find(15);
    const std::optional<uint64_t> funds=gold!=world.playerAttributes.end() && bank!=world.playerAttributes.end()?
        std::optional<uint64_t>{uint64_t(gold->second)+bank->second}:std::nullopt;
    const uint64_t knownFunds=(gold!=world.playerAttributes.end()?uint64_t(gold->second):0)+
        (bank!=world.playerAttributes.end()?uint64_t(bank->second):0);
    for(const auto &[id,item]:world.items) {
        if(item.action!=11 || item.mode!=0 || item.ownerType!=1 || item.owner!=npc->key.id) continue;
        const auto p=definitions->potions.find(item.code);
        if(ammo) {
            const auto base=c.inventory.equipmentDefinition(item);
            if(base.quiver.empty() || !base.isType(quivers.type)) continue;
            const auto decoded=c.inventory.read().items.find(id);
            if(decoded==c.inventory.read().items.end() || !decoded->second.decoded || !decoded->second.quantity.value_or(0)) continue;
            if(!firstBackpackSpace(view,c.inventory.read(),id)) {spaceUnavailable=true;continue;}
        } else if(portal) {if(item.code!=(stock.books?"tsc":"tbk")) continue;}
        else {
            if(p==definitions->potions.end()) continue;
            if((life && p->second.kind!=PotionKind::Life) || (!life && (!mana || p->second.kind!=PotionKind::Mana))) continue;
        }
        offered=true;const auto quote=c.supplies && c.supplies->buyQuote?c.supplies->buyQuote(id):std::nullopt;
        if(!quote) continue;
        quoted=true;if(funds && *funds<*quote) continue;
        affordable=true;const bool known=knownFunds>=*quote;
        const int potency=portal || ammo?0:p->second.strength;
        if(!best || (known && !knownAffordable) || (known==knownAffordable && potency>strength)) {
            strength=potency;best=item;price=*quote;knownAffordable=known;
        }
    }
    if(!best) {
        if(ammo && spaceUnavailable && !quivers.stacks) throw std::runtime_error("No verified backpack space for required ammunition");
        if(!spaceUnavailable && Clock::now()<supply.shopDeadline && (!offered || !quoted)) {
            wait("supply-offers","Waiting for actual stock and normal vendor prices");return false;
        }
        if(ammo && quivers.stacks) {
            supply.quiverTopUpDeferred=true;
            note("quiver-top-up-deferred",spaceUnavailable?"no backpack space":"no usable ammunition offer");
            return false;
        }
        if(ammo) throw std::runtime_error("No purchasable compatible ammunition on the observed merchant shelf");
        throw std::runtime_error(!offered?"Potion merchant did not offer the required potion":!quoted?
            "Potion purchase price is unknown; no zero-price request is invented":!affordable?"Insufficient observed gold for a potion":"No usable potion offer");
    }
    const auto kind=portal || ammo?PotionKind::Life:definitions->potions.at(best->code).kind;
    const auto before=ammo?quivers.rounds:portal?(stock.books?stock.pages:stock.books):potionCount(c,kind);
    const auto trade=world.tradeResult?world.tradeResult->revision:0;
    OnlineItemCommand buy;buy.action=OnlineItemAction::Buy;buy.item=best->id;buy.itemRevision=best->revision;buy.npc=npc->key.id;buy.amount=price;
    buy.multibuy=portal && stock.books;
    unsigned expectedGain=1;
    if(!portal && !ammo && definitions->potions.at(best->code).bulkBelt) {
        const auto room=bulkBeltSpace(view,c.inventory.read().beltSlots,best->code);
        const unsigned target=life?supplyPolicy.healthCount:supplyPolicy.manaCount;
        if(room>1 && before+room<=target && uint64_t(price)*room<=knownFunds) {
            buy.multibuy=true;expectedGain=room;
        }
    }
    if(!submitItem(c,buy,"supply-buy",DebugActionOwner::Supply)) return false;
    PotionRequest pending;pending.buying=true;pending.portalSupply=portal;pending.item=best->id;pending.code=best->code;pending.kind=kind;
    pending.quiverSupply=ammo;
    pending.beforeCount=before;pending.expectedGain=expectedGain;pending.tradeRevision=trade;pending.sequence=c.observation.read().world.itemRequest->sequence;
    pending.deadline=Clock::now()+std::chrono::seconds(12);supply.potionRequest=pending;
    note(ammo?"quiver-purchase-submitted":buy.multibuy?"supply-multibuy-submitted":"potion-purchase-submitted",best->code);return false;
}
bool DebugAi::Impl::autoSupply(Context &c,const Goal &goal) {
    if(!supplyPolicy.enabled || !supplyPolicy.buy || goal.kind==Kind::Restock) return false;
    if(supply.supplyStage!=SupplyStage::None) return supplyDetour(c,goal);
    if(questTransaction(c) || questState.questRun.pending) return false;
    // Town-to-town quest travel consumes no field reserves. Restock at the
    // destination before its next field departure, preserving a delayed gate.
    if(c.town.read().town && cursor<goals.size() && questState.questRun.step<goals[cursor].steps.size()) {
        const auto &step=goals[cursor].steps[questState.questRun.step];
        if(step.action==QuestAction::Portal && step.area &&
            std::find(actTownLevels.begin(),actTownLevels.end(),*step.area)!=actTownLevels.end()) return false;
    }
    const auto &world=c.observation.read().world;
    if(supply.potionRequest || objectIssued || pickup || portalAttempted || portalIssued || navigation.pendingDoor || navigation.mobilityTarget ||
        world.npcRequested || world.waypointSource || world.waypointRequested || world.townPortalPending ||
        world.storage.kind!=OnlineStorageKind::None || world.storage.requested!=OnlineStorageKind::None ||
        (goal.kind==Kind::Deliver && actionDeadline!=Clock::time_point{}) ||
        (world.itemRequest && world.itemRequest->state==OnlineItemRequest::State::Pending) ||
        (world.combatRequest && world.combatRequest->state==OnlineCombatRequest::State::Pending) ||
        navigation.travelPhase==TravelPhase::WaypointWait || navigation.travelPhase==TravelPhase::CrossWait) return false;
    const auto stock=portalStock(c);
    const bool needsHealing=world.life && c.supplies && c.supplies->maxLife && *c.supplies->maxLife>0 &&
        uint64_t(*world.life)*100<uint64_t(*c.supplies->maxLife)*90;
    const bool needsHealth=potionCount(c,PotionKind::Life)<unsigned(supplyPolicy.healthCount);
    const bool needsMana=potionCount(c,PotionKind::Mana)<unsigned(supplyPolicy.manaCount);
    const bool needsPortal=supplyPolicy.fillPortalBook && (!stock.known || !stock.books || stock.pages<=stock.capacity/2);
    const auto quivers=quiverStock(c);
    const bool needsQuivers=quivers.equippedQuantity && !supply.quiverTopUpDeferred && quivers.stacks<unsigned(supplyPolicy.quiverCount);
    const bool needs=needsHealing || needsHealth || needsMana || needsPortal || needsQuivers;
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
        uint16_t peak=*world.life;for(const auto &sample:battle.combatLifeSamples) peak=std::max(peak,sample.life);
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
    empty|=supplyPolicy.returnWhenEmpty && (bossPressure || (needsQuivers && quivers.empty() && !quivers.reserve));
    if(!(c.town.read().town?needs:empty)) return false;
    supply.supplyOrigin=c.town.read().area;
    // Do not return to the field only to execute an already queued trip to
    // this same town (including delivery/find goals whose area is the town).
    if(goal.kind==Kind::ReturnTown || (goal.area && goal.area==c.observation.read().load.townArea))
        supply.supplyOrigin=c.observation.read().load.townArea;
    supply.supplyStarted=Clock::now();supply.supplyDeadline=supply.supplyStarted+std::chrono::seconds(600);
    // Passive scene observation can be re-established after returning;
    // submitted quest operations above can never be abandoned this way.
    questState.questRun.worldWait.reset();
    stop(c.walk,c.control,c.combat);resetStep();battle.combatTarget.reset();battle.retreatPoint.reset();
    supply.supplyStage=c.town.read().town?SupplyStage::Shop:SupplyStage::ToTown;
    supply.shopIssued=false;supply.shelfObserved=false;supply.supplyNpc.reset();
    note("supply-detour-started",std::string(bossPressure?"boss-pressure":"travel-reserves")+
        " heal="+std::to_string(needsHealing)+" hp="+std::to_string(needsHealth)+
        " mp="+std::to_string(needsMana)+" tp="+std::to_string(needsPortal)+" ammo="+std::to_string(needsQuivers));return true;
}
bool DebugAi::Impl::supplyDetour(Context &c,const Goal &goal) {
    const auto now=Clock::now();
    if(now>=supply.supplyDeadline) throw std::runtime_error("Automatic potion supply detour exceeded 600 seconds");
    Goal movement;movement.kind=Kind::Travel;
    const bool transfer=portalIssued || navigation.travelPhase==TravelPhase::WaypointWait ||
        (navigation.travelPhase==TravelPhase::CrossWait && navigation.legUnit);
    if(!transfer && supply.supplyStage!=SupplyStage::ToTown && supplyCombat(c,movement)) return true;
    if(supply.supplyStage==SupplyStage::ToTown) {
        if(reachTown(c)) {resetStep();supply.supplyStage=SupplyStage::Shop;supply.shopIssued=false;supply.shelfObserved=false;supply.supplyNpc.reset();}
    } else if(supply.supplyStage==SupplyStage::Shop) {
        if(restock(c)) {
            resetStep();
            // The parent quest is selected again from actual facts in town.
            // Its next action may be a reward, assembly or a different area;
            // returning to the interrupted leaf first would create a loop.
            const bool quest=cursor<goals.size() && goals[cursor].kind==Kind::Quest;
            if(quest) {
                supply.supplyStage=SupplyStage::None;goalDeadline+=now-supply.supplyStarted;
                supply.supplyOrigin.reset();
                note("supply-detour-completed","Re-evaluating the parent quest from the observed town scene");
                return true;
            }
            if(goal.kind==Kind::ReturnTown) supply.supplyOrigin=c.observation.read().load.townArea;
            else if(goal.area) supply.supplyOrigin=goal.area;
            supply.supplyStage=SupplyStage::Return;
        }
    } else if(supply.supplyStage==SupplyStage::Return) {
        if(c.town.read().area==supply.supplyOrigin) {
            resetStep();supply.supplyStage=SupplyStage::None;goalDeadline+=now-supply.supplyStarted;supply.supplyOrigin.reset();
            note("supply-detour-completed","Original high-level goal retained");return true;
        }
        // Use the same destination-based planner as ordinary travel. Its
        // transfer receipt owns the wait, including an assigned own portal.
        if(supply.supplyOrigin) travel(c,*supply.supplyOrigin);
    }
    if(!portalUsed && canInput(c,DebugActionOwner::Mobility,DebugMotion|DebugSkill)) mobility(c);
    return true;
}
bool DebugAi::Impl::supplyCombat(Context &c,const Goal &goal) {
    if(!supplyPolicy.enabled) return combat(c,goal);
    const auto &world=c.observation.read().world;
    const bool noRejuvenation=!potionCount(c,PotionKind::Rejuvenation);
    const bool dry=noRejuvenation && c.supplies &&
        ((!potionCount(c,PotionKind::Mana) && world.mana && c.supplies->maxMana && *c.supplies->maxMana>0 &&
            uint64_t(*world.mana)*100<uint64_t(*c.supplies->maxMana)*supplyPolicy.manaPercent) ||
        (!potionCount(c,PotionKind::Life) && world.life && c.supplies->maxLife && *c.supplies->maxLife>0 &&
            uint64_t(*world.life)*100<uint64_t(*c.supplies->maxLife)*supplyPolicy.criticalLifePercent));
    if(!dry && !quiverStock(c).empty()) return combat(c,goal);
    // A supply trip must keep trying the normal escape route rather than
    // standing still and submitting an unusable fixed spell indefinitely.
    if(battle.combatTarget || battle.combatOwned) {stop(c.walk,c.control,c.combat);navigation.walkIntent={};battle.combatTarget.reset();battle.retreatPoint.reset();}
    return false;
}
}
