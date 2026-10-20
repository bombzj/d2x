#include "remote_ui_clients_impl.hpp"

namespace d2x {
const NpcConversationView &RemoteUiClients::Impl::Npc::read(EntityId) const { return o.npcView; }
const NpcSceneView &RemoteUiClients::Impl::Npc::scene() const { return o.npcScene; }
const ShopView &RemoteUiClients::Impl::Npc::shop(EntityId, bool) const { return o.shopView; }
const ShopOfferView *RemoteUiClients::Impl::Npc::inspectShopOffer(EntityId npc, uint32_t slot, bool gamble) const {
            if (npc != o.shopView.npc || gamble != o.shopView.gamble || !onlineInteractionMatches(o.context, o.session.read())) return nullptr;
            const auto it = std::find_if(o.shopView.offers.begin(), o.shopView.offers.end(),
                [&](const auto &v) { return v.slot == slot; });
            return it == o.shopView.offers.end() ? nullptr : &*it;
        }
std::optional<unsigned> RemoteUiClients::Impl::Npc::quote(EntityId npc, ItemHandle item, bool repair) const {
            if (!npc || o.context.npc != npcGuid(npc) || !onlineInteractionMatches(o.context, o.session.read())) return {};
            if (repair && !item.id) return o.repairAllQuote();
            const auto *entry = o.inventoryView.item(item.id);
            if (!entry || entry->revision != item.revision || o.context.npc != npcGuid(npc)) return {};
            return o.itemQuote(guid(item.id), repair ? OnlineItemAction::Repair : OnlineItemAction::Sell);
        }
bool RemoteUiClients::Impl::Npc::canRequestSale(EntityId npc, ItemHandle item) const {
            const auto *entry = o.inventoryView.item(item.id);
            const auto &online = o.session.read();
            if (!npc || !entry || entry->revision != item.revision || o.context.npc != npcGuid(npc) ||
                !onlineInteractionMatches(o.context, online)) return false;
            const auto wire = online.world.items.find(guid(item.id));
            const auto *definition = o.data.items.find(entry->definition);
            return wire != online.world.items.end() && definition && !definition->questTag &&
                !(wire->second.flags & 0x1000u) && wire->second.ownerType == 0 && wire->second.owner == online.load.playerUnitId &&
                ((wire->second.mode == 0 && wire->second.page == 1) || wire->second.mode == 1 || wire->second.mode == 4);
        }
const HirelingView &RemoteUiClients::Impl::Npc::hireling() const { return o.hirelingView; }
const HirelingListView &RemoteUiClients::Impl::Npc::hirelings(EntityId) const { return o.hirelingList; }
void RemoteUiClients::Impl::Npc::submit(NpcIntent c) {
            if (!onlineInteractionMatches(o.context, o.session.read())) { o.notice = "NPC interaction changed; select it again."; return; }
            std::visit([&](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                OnlineItemCommand request;
                request.npc = o.context.npc.value_or(0);
                if constexpr (std::is_same_v<T, EndNpcConversation>) { o.session.close_npc(o.context); return; }
                else if constexpr (std::is_same_v<T, OpenHirelingList>) {
                    if(!o.session.hireling_service(OnlineHirelingAction::List,{},o.context)) o.notice="Hireling list request is unavailable or pending.";
                    return;
                }
                else if constexpr (std::is_same_v<T, HireMercenary>) {
                    if(v.slot>UINT16_MAX || !o.session.hireling_service(OnlineHirelingAction::Hire,uint16_t(v.slot),o.context)) o.notice="Hireling offer changed or a request is pending.";
                    return;
                }
                else if constexpr (std::is_same_v<T, ResurrectHireling>) {
                    if(!o.session.hireling_service(OnlineHirelingAction::Resurrect,{},o.context)) o.notice="Hireling resurrection is unavailable or pending.";
                    return;
                }
                else if constexpr (std::is_same_v<T, TalkToNpc>) {
                    if (v.action == TalkToNpc::Action::Acknowledge && v.message && *v.message <= UINT16_MAX) {
                        const auto &conversation = o.session.read().world.npcConversation;
                        if (conversation && !conversation->acknowledged.contains(uint16_t(*v.message)))
                            o.session.acknowledge_npc_message(uint16_t(*v.message), o.context);
                    } else if (v.action == TalkToNpc::Action::Trade) {
                        request.action = OnlineItemAction::TradeOpen; o.enqueue(request);
                    }
                    return;
                }
                else if constexpr (std::is_same_v<T, CompleteActOne> || std::is_same_v<T, CompleteActTwo>) {
                    if(o.scene && o.scene->npcConversation && o.scene->npcConversation->travelDestination)
                        o.session.npc_travel(*o.scene->npcConversation->travelDestination,o.context);
                    return;
                }
                else if constexpr (std::is_same_v<T, ClaimAkaraRespec>) {o.session.npc_travel(0,o.context);return;}
                else if constexpr (std::is_same_v<T, IdentifyWithCain>) request.action = OnlineItemAction::IdentifyAll;
                else if constexpr (std::is_same_v<T, BuyVendorItem>) {
                    request.action = OnlineItemAction::Buy; request.item = v.slot;
                    request.gamble = v.gamble;request.multibuy=v.multibuy;
                } else if constexpr (std::is_same_v<T, OpenGamble>) {
                    request.action = OnlineItemAction::TradeOpen; request.gamble = true;
                } else if constexpr (std::is_same_v<T, SellVendorItem>) {
                    request.action = OnlineItemAction::Sell; request.item = guid(v.item.id); request.itemRevision = v.item.revision;
                } else if constexpr (std::is_same_v<T, RepairVendorItem>) {
                    request.action = v.item.id ? OnlineItemAction::Repair : OnlineItemAction::RepairAll;
                    if (v.item.id) { request.item = guid(v.item.id); request.itemRevision = v.item.revision; }
                } else if constexpr (std::is_same_v<T, ImbueItem> || std::is_same_v<T, SocketQuestItem> || std::is_same_v<T, PersonalizeQuestItem>) {
                    request.action=OnlineItemAction::QuestService;request.item=guid(v.item.id);request.itemRevision=v.item.revision;
                    if(o.items.read().cursor!=request.item) {o.compositeTake(request);return;}
                } else { o.notice = "This native NPC service is unavailable."; return; }
                o.enqueue(request);
            }, c);
        }
std::string RemoteUiClients::Impl::npcIdentity() const {
        const auto &w=session.read().world; if(!w.npcConversation) return {};
        auto unit=w.units.find({1,w.npcConversation->source});
        if(unit==w.units.end() || !unit->second.classId) return {};
        const auto &mon=data.tables.at("monstats");
        for(size_t row=0;row<mon.rows().size();++row) if(mon.number(row,"hcIdx")==*unit->second.classId) return std::string(mon.value(row,"Id"));
        return {};
    }
void RemoteUiClients::Impl::projectMap() {
        const auto &online=session.read(); const auto &w=online.world;
        mapView={}; mapView.revision=revision; mapView.actor=characterView.actor;
        mapView.travelRequested = w.waypointRequested.has_value();
        mapView.act=online.load.act.value_or(0); mapView.region=RegionId(scene&&scene->area?*scene->area:1);
        mapView.palette=scene&&scene->palette?*scene->palette:mapView.act;
        if(scene && scene->origin && w.playerPosition) mapView.observer={float(int(w.playerPosition->x)-scene->origin->x),float(int(w.playerPosition->y)-scene->origin->y)};
        if(scene && scene->origin && displayPosition)
            mapView.observer = *displayPosition - Vec{float(scene->origin->x), float(scene->origin->y)};
        if(scene) for(const auto &d:scene->waypoints) if(d.unlocked) mapView.waypointActs[d.act]=true;
    }
void RemoteUiClients::Impl::projectInteractions() {
        const auto &online=session.read(); const auto &w=online.world;
        npcView={}; npcView.revision=revision; npcView.actor=characterView.actor;
        npcScene={}; npcScene.revision=revision;
        const auto &mon=data.tables.at("monstats"), &extra=data.tables.at("monstats2");
        for (const auto &key:w.questAlerts) {
            const auto unit=w.units.find(key);
            if (key.type!=1 || unit==w.units.end() || !unit->second.classId) continue;
            for (size_t row=0; row<mon.rows().size(); ++row) {
                if (mon.number(row,"hcIdx")!=*unit->second.classId || !mon.number(row,"interact").value_or(0)) continue;
                for (size_t sub=0; sub<extra.rows().size(); ++sub)
                    if (extra.value(sub,"Id")==mon.value(row,"MonStatsEx")) {
                        npcScene.npcs[npcId(key.id)]={true,extra.number(sub,"OverlayHeight").value_or(0)-1};
                        break;
                    }
                break;
            }
        }
        if(scene && scene->npcConversation) {
            const auto &d=*scene->npcConversation; npcView.npc=npcId(d.source); npcView.valid=true; npcView.speaker=d.speaker;
            if(d.type==2) {
                npcView.npc=EntityId{(uint64_t{2}<<32)+d.source+1};
                for(const auto &m:d.messages) if(!m.acknowledged && !m.text.empty()) {npcView.scrollMessage=m.stringId;npcView.introduction=m.text;break;}
            }
            if(scene->origin) npcView.position={float(int(d.position.x)-scene->origin->x),float(int(d.position.y)-scene->origin->y)};
            for(const auto &m:d.messages) if(!m.text.empty()) {
                if(m.menu==0 && !m.acknowledged && !npcView.introduction) npcView.introduction=m.text;
                else if(m.menu==1 || m.menu==2) {
                    npcView.textTopics.emplace(m.stringId,m.text);
                    auto title=m.text.substr(0,m.text.find('\n'));
                    if(title.size()>40) title=title.substr(0,40)+"...";
                    npcView.talkEntries.push_back({title,{NpcMenuAction::TextTopic,{},m.stringId}});
                }
            }
            auto add=[&](std::string label,NpcMenuAction action) { npcView.services.push_back({std::move(label),{action,{}}}); };
            auto menu=[&](std::string_view key,std::string fallback) {
                if(const auto found=data.itemStrings.find(key);found!=data.itemStrings.end()) return found->second;
                return fallback;
            };
            // Native servers send quest/intro text in 27; ordinary gossip is
            // local MPQ presentation and must not generate a quest ACK (31).
            if(d.type==1) for(size_t turn=0;;++turn) {
                const auto *speech=gossipSpeech(data.npcDialogues,d.speaker,turn,mapView.act);
                if(!speech || speech->text.empty()) break;
                std::string text=speech->text;
                if(const auto localized=data.itemStrings.find(speech->stringKey);localized!=data.itemStrings.end())
                    text=std::string(npcTblSpeech(localized->second));
                if(std::find(npcView.gossip.begin(),npcView.gossip.end(),text)!=npcView.gossip.end()) break;
                npcView.gossip.push_back(std::move(text));
            }
            if(!npcView.gossip.empty()) npcView.talkEntries.push_back({menu("NPCGossipMenu","Gossip"),{NpcMenuAction::Gossip,{}}});
            if(!npcView.textTopics.empty() || !npcView.gossip.empty()) {
                add(menu("TalkMenu","Talk"),NpcMenuAction::Talk);
                npcView.talkEntries.push_back({menu("Back","Back"),{NpcMenuAction::Back,{}}});
            }
            const auto identity=npcIdentity();
            if(identity=="kashya" || identity=="greiz" || identity=="asheara" || identity=="qual-kehk" || identity=="tyrael") {
                if(identity!="tyrael") add(menu("NPCMenuHire","Hire"),NpcMenuAction::Hire);
                if(w.deadHirelingName && w.hirelingReviveCost) add(menu("hireresurrect","Resurrect")+": " + std::to_string(*w.hirelingReviveCost),NpcMenuAction::Resurrect);
            }
            if(data.vendors.contains(identity) && identity!="nihlathak") add(menu("NPCMenuTrade","Trade"),NpcMenuAction::Trade);
            if(identity=="gheed" || identity=="elzix" || identity=="alkor" || identity=="jamella" || identity=="drehya" || identity=="nihlathak") add(menu("gamble","Gamble"),NpcMenuAction::Gamble);
            if(identity.starts_with("cain")) add(menu("NPCIdentify1","Identify Items"),NpcMenuAction::Identify);
            // Original S2C 0x28 personal quest records, never local reward
            // authority. SUnitNpc accepts the corresponding RewardPending bit.
            const auto rewardPending=[&](QuestId id) {
                if(!w.quests.playerFlags) return false;
                const auto flags=(*w.quests.playerFlags)[questDefinition(id).nativeSlot];
                return (flags&3u)==2u;
            };
            if(identity=="charsi" && characterView.level>=8 && rewardPending(QuestId::ToolsOfTheTrade))
                add(menu("Upgrade","Imbue"),NpcMenuAction::Imbue);
            if(identity=="larzuk" && rewardPending(QuestId::SiegeOnHarrogath)) {
                add(data.itemStrings.at("Addsocketsui"),NpcMenuAction::Socket);
                npcView.serviceHints.emplace(NpcMenuAction::Socket,data.itemStrings.at("Addsocketsui2"));
            }
            if(identity=="drehya" && rewardPending(QuestId::BetrayalOfHarrogath)) {
                add(data.itemStrings.at("Personalizeui"),NpcMenuAction::Personalize);
                npcView.serviceHints.emplace(NpcMenuAction::Personalize,data.itemStrings.at("Rename Instruct"));
            }
            if(!d.travelLabel.empty()) add(d.travelLabel,NpcMenuAction::GoEast);
            if(!d.respecLabel.empty()) add(d.respecLabel,NpcMenuAction::Respec);
            add(menu("strUiMenu1","Cancel"),NpcMenuAction::Cancel);
        }
        QuestProjectionInput questInput;
        questInput.revision=revision; questInput.actor=characterView.actor; questInput.currentAct=mapView.act;
        questInput.denRemaining=w.quests.denRemaining;
        questInput.staffTombOffset=w.quests.staffTombOffset;
        for (const auto &def:questDefinitions) {
            auto &entry=questInput.entries[questIndex(def.id)];
            entry.status=w.quests.statuses[def.nativeSlot];
            if (w.quests.playerFlags) entry.playerFlags=(*w.quests.playerFlags)[def.nativeSlot];
            if(w.quests.availability) {
                const auto found=std::find(nativeQuestInitializationSlots.begin(),nativeQuestInitializationSlots.end(),def.nativeSlot);
                if(found!=nativeQuestInitializationSlots.end()) entry.available=(*w.quests.availability)[size_t(found-nativeQuestInitializationSlots.begin())]!=0;
            }
        }
        questView=projectQuestDisplay(data,questInput);
        shopView={}; shopView.revision=revision; shopView.actor=characterView.actor; shopView.npc=npcView.npc;
        shopView.bankGold=inventoryView.bankGold; shopView.pricesKnown=false;
        shopView.tabLabels={data.itemStrings.at("strBSArmor"),data.itemStrings.at("strBSWeapons"),
            data.itemStrings.at("strBSWeapons"),data.itemStrings.at("strBSMisc")};
        const auto identity=npcIdentity();
        shopView.gamble=w.shopGamble;
        shopView.available=npcView.valid && (identity=="nihlathak" || data.vendors.contains(identity));
        shopView.repairAvailable=!w.shopGamble && (identity=="charsi" || identity=="fara" || identity=="hratli" || identity=="halbu" || identity=="larzuk");
        if(w.shopSource) for(const auto &[id,native]:w.items) {
            if(native.ownerType!=1 || native.owner!=w.shopSource || native.action!=11) continue;
            auto d=items.read().items.find(id); const auto *def=data.items.find(native.code);
            if(d==items.read().items.end() || !d->second.decoded || !def) continue;
            const auto &item=d->second;
            ShopOfferView offer; offer.slot=id; offer.width=item.width; offer.height=item.height; offer.definition=native.code;
            auto projection=projectItem(native,item,SocketLocation{{},0});
            offer.name=projection.name; offer.artKey=projection.artKey; offer.quality=projection.quality;
            const auto price = itemQuote(id, OnlineItemAction::Buy);
            offer.priceKnown = price.has_value(); offer.price = price.value_or(0);
            if(native.page<1 || native.page>4) continue;
            offer.storePage=int(native.page)-1;offer.cell=std::array{int(native.x),int(native.y)};
            offer.tooltip=projection.tooltip;
            if (item.gamble) offer.tooltip={{projection.name,ItemTextTone::Name}};
            shopView.offers.push_back(std::move(offer));
            // Include read-only shelf art in the shared inventory art cache, not owned inventory occupancy.
            inventoryView.items.emplace(projection.id,std::move(projection));
        }
        shopView.pricesKnown = !shopView.offers.empty() && std::all_of(shopView.offers.begin(), shopView.offers.end(), [](const auto &offer) { return offer.priceKnown; });
        shopView.repairAllPrice = repairAllQuote();
    }
void RemoteUiClients::Impl::projectHireling() {
        hirelingView={};hirelingView.revision=revision;hirelingView.actor=characterView.actor;
        const auto &world=session.read().world;
        if (session.read().load.playerUnitId)
            hirelingView.portraits=projectPetPortraits(data,world,*session.read().load.playerUnitId);
        if(!world.hireling) return;
        const auto &merc=*world.hireling;
        hirelingView.id=npcId(merc.id);hirelingView.classId=merc.monsterClass;
        for(const auto &[key,index]:data.hirelingNameIds) if(index==int(merc.name)) {
            const auto name=data.hirelingStrings.find(key);
            if(name!=data.hirelingStrings.end()) hirelingView.name=name->second;
            break;
        }
        const auto value=[&](std::string_view name)->std::optional<int64_t> {
            const auto &table=data.tables.at("itemstatcost");
            for(size_t row=0;row<table.rows().size();++row) if(table.value(row,"Stat")==name) {
                const auto id=table.number(row,"ID");if(!id || *id<0 || *id>255) return {};
                const auto stat=merc.attributes.find(uint8_t(*id));if(stat==merc.attributes.end()) return {};
                return table.number(row,"Signed").value_or(0)?int64_t(int32_t(stat->second)):int64_t(stat->second);
            }
            return {};
        };
        const auto life=value("hitpoints"),maximum=value("maxhp"),level=value("level");
        hirelingView.known=life && maximum && level && *maximum>0;
        for(const auto *name:{"strength","dexterity","armorclass","mindamage","maxdamage","experience","nextexp","fireresist","coldresist","lightresist","poisonresist"})
            hirelingView.known=hirelingView.known && value(name).has_value();
        if(!hirelingView.known) return;
        const auto projected=observedCombatEquipment({1,merc.id},{"",int(*value("strength")),int(*value("dexterity")),int(*level),0,0,true},
            {},int(*value("armorclass")),0,int(*value("mindamage")),int(*value("maxdamage")));
        hirelingView.known=projected.has_value();if(!projected) return;
        const auto &mods=projected->modifiers;
        const auto &attack=projected->equipment.weapons[0];
        hirelingView.attackTiming=KnownAttackTiming{attack.fasterAttack,attack.baseSpeed,mods.combat.attackRate};
        hirelingView.recoveryTiming=KnownRecoveryTiming{mods.combat.fasterHitRecovery,mods.combat.fasterBlock,false};
        hirelingView.fasterCast=mods.combat.fasterCast;
        hirelingView.level=int(*level);hirelingView.life=float(*life)/256.f;
        hirelingView.maximumLife=std::max(1,int((int64_t(*maximum/256)+mods.maxLife)*(100+mods.combat.lifePercent)/100));
        const auto pet=world.pets.find(merc.id);
        hirelingView.active=*life>0 && !world.deadHirelingName && pet!=world.pets.end();
        hirelingView.strength=projected->actor.strength;hirelingView.dexterity=projected->actor.dexterity;
        hirelingView.defense=projected->equipment.defense;
        for(const auto &[id,item]:world.items) {
            if(item.ownerType!=1 || item.owner!=merc.id || item.mode!=1) continue;
            const auto decoded=items.read().items.find(id);
            if(decoded!=items.read().items.end() && decoded->second.decoded && decoded->second.defense.value_or(0)>0 &&
                (!decoded->second.maxDurability.value_or(0) || decoded->second.durability.value_or(0)>0)) hirelingView.defenseImproved=true;
        }
        if(const auto states=combat.states().find({1,merc.id});states!=combat.states().end() && states->second.decoded) {
            const auto &costs=data.tables.at("itemstatcost");
            for(size_t row=0;row<costs.rows().size();++row) if(costs.value(row,"Stat")=="skill_armor_percent")
                for(const auto &[state,list]:states->second.states) {
                    (void)state;
                    for(const auto &entry:list.stats) if(costs.number(row,"ID")==entry.id && entry.value>0) hirelingView.defenseImproved=true;
                }
        }
        auto weapon=projected->equipment.weapons[0];
        const auto definition=std::find_if(data.hirelings.begin(),data.hirelings.end(),[&](const auto &entry){return entry.classId==merc.monsterClass;});
        if(!weapon.item && definition!=data.hirelings.end() && definition->act==1) {
            const int percent=std::max(-90,projected->actor.dexterity+mods.combat.damagePercent+std::max(mods.combat.minimumDamagePercent,mods.combat.maximumDamagePercent));
            weapon.minimum=int(int64_t(weapon.meleeBaseMinimum)*(100+percent)/100);
            weapon.maximum=int(int64_t(weapon.meleeBaseMaximum)*(100+percent)/100);
        }
        const auto elements=attackElementRanges(mods.combat,weapon.item);
        WeaponModifiers own;if(const auto found=mods.combat.weapons.find(weapon.item);found!=mods.combat.weapons.end()) own=found->second;
        const int64_t frames=(int64_t(mods.combat.poisonFrames)+own.poisonFrames)/std::max(1,mods.combat.poisonSources+own.poisonSources);
        hirelingView.damageMinimum=int(int64_t(weapon.minimum)/256+elements.fire.minimum+elements.lightning.minimum+elements.cold.minimum+elements.magic.minimum+
            (int64_t(mods.combat.poisonMinimum)+own.poisonMinimum)*frames/256);
        hirelingView.damageMaximum=int(int64_t(weapon.maximum)/256+elements.fire.maximum+elements.lightning.maximum+elements.cold.maximum+elements.magic.maximum+
            (int64_t(mods.combat.poisonMaximum)+own.poisonMaximum)*frames/256);
        hirelingView.experience=uint64_t(std::max<int64_t>(0,*value("experience")));
        hirelingView.nextExperience=uint64_t(std::max<int64_t>(0,*value("nextexp")));
        const int baseResist[]={int(*value("fireresist")),int(*value("coldresist")),int(*value("lightresist")),int(*value("poisonresist"))};
        const int bonus[]={mods.fireResist,mods.coldResist,mods.lightningResist,mods.poisonResist};
        const int caps[]={mods.combat.fireMaxResist,mods.combat.coldMaxResist,mods.combat.lightningMaxResist,mods.combat.poisonMaxResist};
        for(size_t index=0;index<4;++index) hirelingView.resistances[index]=std::clamp(baseResist[index]+bonus[index],-100,std::clamp(75+caps[index],-100,95));
        const auto unit=world.units.find({1,merc.id});
        if(unit!=world.units.end() && unit->second.position && scene && scene->origin)
            hirelingView.position={float(unit->second.position->x)-scene->origin->x,float(unit->second.position->y)-scene->origin->y};
    }
void RemoteUiClients::Impl::projectHirelingList() {
        hirelingList={};hirelingList.revision=revision;hirelingList.actor=characterView.actor;
        hirelingList.gold=inventoryView.gold;
        const auto cancel=data.itemStrings.find("Cancel");
        hirelingList.cancelLabel=cancel==data.itemStrings.end()?"Cancel":cancel->second;
        const auto &online=session.read();const auto &world=online.world;const auto &service=world.hirelingService;
        if(!service.source || !service.listReceived || !world.npcConversation || service.source!=world.npcConversation->source ||
            service.interaction!=world.interactionGeneration || !online.load.difficulty) return;
        hirelingList.npc=npcId(*service.source);
        const auto npc=world.units.find({1,*service.source});
        if(npc==world.units.end() || !npc->second.classId) return;
        for(const auto &[name,seed]:service.offers) {
            const auto definition=std::find_if(data.hirelings.begin(),data.hirelings.end(),[&](const auto &entry){return entry.seller==*npc->second.classId && entry.difficulty==*online.load.difficulty+1;});
            if(definition==data.hirelings.end()) continue;
            const auto first=data.hirelingNameIds.find(definition->nameFirst),last=data.hirelingNameIds.find(definition->nameLast);
            if(first==data.hirelingNameIds.end() || last==data.hirelingNameIds.end() || name<first->second || name>last->second) continue;
            const auto offer=resolveHirelingOffer(data.hirelings,*npc->second.classId,*online.load.difficulty,characterView.level,seed,uint32_t(name-first->second+1));
            if(!offer) continue;
            const auto text=data.hirelingStrings.find(offer->nameKey);
            const auto row=std::find_if(data.hirelings.begin(),data.hirelings.end(),[&](const auto &entry){return entry.sourceRow==offer->sourceRow;});
            std::string description;
            if(row!=data.hirelings.end()) if(const auto found=data.hirelingDescriptions.find(row->description);found!=data.hirelingDescriptions.end()) description=found->second;
            hirelingList.offers.push_back({name,text==data.hirelingStrings.end()?offer->nameKey:text->second,description,
                offer->level,offer->stats.life,offer->stats.defense,int(offer->stats.price)});
        }
    }
}
