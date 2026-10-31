#include "remote_ui_clients_impl.hpp"

namespace d2x {
RemoteUiClients::Impl::Impl(Archives &a, net::RealmSession &s, RemoteInventory &i, RemoteCombat &c, RemoteControl &r)
        : session(s), items(i), combat(c), control(r), sharedData(sharedClassicData(a, s.read().protocolProfile != "lod113c" ? ContentPreparation::RemoteDisplay : ContentPreparation::OriginalRules)), data(*sharedData) {
    const auto &costs = data.tables.at("itemstatcost");
    for (size_t row = 0; row < costs.rows().size(); ++row) {
        std::optional<ObservedStat> definition;
        const auto id = costs.number(row, "ID");
        if (id && *id >= 0 && *id <= 255)
            definition = ObservedStat{uint8_t(*id), costs.number(row, "ValShift").value_or(0),
                costs.number(row, "Signed").value_or(0) != 0};
        observedStats.try_emplace(std::string(costs.value(row, "Stat")), definition);
    }
}
const QuestView &RemoteUiClients::Impl::Quests::read() const { return o.questView; }
bool RemoteUiClients::Impl::Quests::acknowledgeCompletion(QuestId quest) {
            return o.session.acknowledge_quest_log(uint16_t(questDefinition(quest).nativeSlot),o.context.gameGeneration);
        }
const MapSceneView &RemoteUiClients::Impl::MapClient::read() const { return o.mapView; }
const TravelMenuView &RemoteUiClients::Impl::MapClient::travel(EntityId source, int act) const {
            auto &v = o.travelView; v = {}; v.revision = o.revision; v.source = source; v.act = act;
            if (o.scene) for (const auto &d : o.scene->waypoints) if (d.act == act)
                v.entries.push_back({d.level,o.data.itemStrings.contains(d.name) ? o.data.itemStrings.at(d.name) : d.name,d.unlocked ? "" : "Not activated",
                    d.unlocked ? std::optional<RegionId>{RegionId(d.level)} : std::nullopt});
            return v;
        }
bool RemoteUiClients::Impl::MapClient::waypointSource(EntityId source) const {
            return o.session.read().world.waypointSource == npcGuid(source);
        }
void RemoteUiClients::Impl::MapClient::closeTravel() {
            if (onlineInteractionMatches(o.context, o.session.read()) && o.session.use_waypoint(0, 0, o.context)) {
                // Only our successful close advances this frame's interaction.
                // The following click/shortcut must use the resulting context.
                o.context = onlineIntentContext(o.session.read());
                o.mapView.travelRequested = false;
            }
        }
void RemoteUiClients::Impl::MapClient::submit(MapIntent intent) {
            if (!onlineInteractionMatches(o.context, o.session.read())) { o.notice = "Travel interaction changed; select it again."; return; }
            std::visit([&](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, WaypointTravel>)
                    if (o.scene) for (const auto &d : o.scene->waypoints)
                        if (d.level == uint16_t(v.destination) && d.unlocked) { o.session.use_waypoint(d.level,d.number,o.context); return; }
            }, intent);
        }
std::optional<double> RemoteUiClients::Impl::stat(std::string_view name) const {
        const auto found = observedStats.find(name);
        if (found == observedStats.end() || !found->second) return {};
        const auto &definition = *found->second;
        const auto &attributes = session.read().world.playerAttributes;
        const auto value = attributes.find(definition.id);
        if (value == attributes.end()) return {};
        const double observed = definition.signedValue ? double(int32_t(value->second)) : double(value->second);
        return observed / std::ldexp(1., definition.shift);
    }
void RemoteUiClients::Impl::queue(OnlineItemCommand c) {
        if (!c.context) c.context = context;
        transactions.push_back(std::move(c));
    }
void RemoteUiClients::Impl::enqueue(OnlineItemCommand c) {
        if (waiting || !transactions.empty()) { notice = "Waiting for the previous server item response."; return; }
        queue(std::move(c)); pump();
    }
void RemoteUiClients::Impl::pump() {
        const auto &online = session.read();
        const auto &w = online.world;
        if ((waitingContext && !onlineInteractionMatches(*waitingContext, online)) ||
            (!transactions.empty() && (!transactions.front().context ||
                !onlineInteractionMatches(*transactions.front().context, online)))) {
            notice = "Queued item operation cancelled after the game, area or interaction changed.";
            transactions.clear(); waiting.reset(); waitingContext.reset(); return;
        }
        if (waiting) {
            if (!w.itemRequest || w.itemRequest->sequence != *waiting) { transactions.clear(); waiting.reset(); waitingContext.reset(); return; }
            const auto status = w.itemRequest->state;
            if (status == OnlineItemRequest::State::Pending) return;
            if (status == OnlineItemRequest::State::Rejected || status == OnlineItemRequest::State::TimedOut || status == OnlineItemRequest::State::Interrupted) {
                notice = "Server item request rejected, timed out or interrupted."; transactions.clear();
            }
            // Cursor combinations require assignment before their next step.
            // Bulk materials instead await each deposit's counter and removal.
            if (!transactions.empty() && transactions.front().action != OnlineItemAction::MaterialDeposit &&
                (!items.read().cursor || *items.read().cursor != transactions.front().item)) {
                if (status == OnlineItemRequest::State::Updated && std::chrono::steady_clock::now() < waitingUntil) return;
                notice = "Server did not assign the requested cursor item."; transactions.clear();
            }
            waiting.reset(); waitingContext.reset();
        }
        if (transactions.empty()) return;
        if (!session.item_request_ready()) return;
        auto c = transactions.front(); transactions.pop_front();
        if (c.action == OnlineItemAction::Buy || c.action == OnlineItemAction::Sell) {
            const auto found = session.read().world.items.find(c.item);
            if (found == session.read().world.items.end() || (c.itemRevision && c.itemRevision != found->second.revision)) {
                notice = "The quoted item changed before submission."; transactions.clear(); return;
            }
            c.itemRevision = found->second.revision;
            const auto price = itemQuote(c.item, c.action);
            if (!price) { notice = "The current server item has no complete transaction quote."; transactions.clear(); return; }
            c.amount = *price;
        }
        if (!items.submit(session,c)) { notice = items.reason(); transactions.clear(); return; }
        const auto &sent = session.read();
        if (c.action == OnlineItemAction::TradeOpen) {
            shopView.gamble = sent.world.shopGamble;
            shopView.offers.clear();
            shopView.pricesKnown = false;
            ++shopView.revision;
        }
        if (sent.world.itemRequest) {
            waiting = sent.world.itemRequest->sequence;
            waitingContext = onlineIntentContext(sent);
            waitingUntil = std::chrono::steady_clock::now() + session.request_timeout();
        }
    }
void RemoteUiClients::Impl::update(const OnlineSceneView &binding, std::optional<Vec> position) {
        scene=&binding;
        displayPosition = position;
        context = onlineIntentContext(session.read());
        if(generation!=session.read().gameGeneration) {
            generation=session.read().gameGeneration; transactions.clear(); waiting.reset(); waitingContext.reset(); hotkeys={};
            combatEquipment.clear();equipmentStats.clear();
        }
        std::erase_if(combatEquipment,[&](const auto &entry){return !session.read().world.units.contains(entry.first);});
        std::erase_if(equipmentStats,[&](const auto &entry){return !session.read().world.units.contains(entry.first.first);});
        if (areaGeneration!=session.read().world.areaGeneration) {
            areaGeneration=session.read().world.areaGeneration; transactions.clear(); waiting.reset(); waitingContext.reset();
        }
        const auto &online=session.read();
        const ProjectionKey sources{online.revision,items.read().revision,generation,areaGeneration,
            binding.area,binding.palette,binding.origin,binding.town,run,
            binding.npcConversation?std::optional{std::pair{binding.npcConversation->source,binding.npcConversation->revision}}:std::nullopt};
        if(!projectedSources || *projectedSources!=sources) {
            ++revision;
            projectCharacter(); projectMap(); projectInventory(); projectInteractions();
            projectHirelingList(); projectHireling();
            projectedSources=sources;
        } else {
            // Prediction and timeouts still advance every presentation frame.
            // Heavy character/item/NPC projections depend on replica versions,
            // never on the camera's interpolated position or wall-clock cadence.
            ++revision; projectMap();
            const auto &w=online.world;
            const std::optional<GroundLocation> drop=w.playerPosition?
                std::optional{GroundLocation{mapView.region,mapView.observer}}:std::nullopt;
            if(inventoryView.dropLocation!=drop) {
                inventoryView.dropLocation=drop;inventoryView.revision=revision;
            }
            projectItemFeedback(inventoryView);
        }
        if (characterView.dead) { transactions.clear(); waiting.reset(); waitingContext.reset(); }
        else pump();
        const auto &request = session.read().world.itemRequest;
        const bool itemPending = waiting.has_value() || !transactions.empty() ||
            (request && request->state == OnlineItemRequest::State::Pending);
        if (inventoryView.itemOperationPending != itemPending) {
            inventoryView.itemOperationPending = itemPending;
            inventoryView.revision = revision;
        }
    }
RemoteUiClients::RemoteUiClients(Archives &a,net::RealmSession &s,RemoteInventory &i,RemoteCombat &c,RemoteControl &r,bool running):impl_(std::make_unique<Impl>(a,s,i,c,r)){
    impl_->run=running;c.setSkillRankResolver([state=impl_.get()]{return state->observedSkillRanks();});
}
RemoteUiClients::~RemoteUiClients(){impl_->combat.setSkillRankResolver({});}
size_t RemoteUiClients::queuedItemCommands() const { return impl_->transactions.size(); }
std::optional<uint64_t> RemoteUiClients::waitingItemRequest() const { return impl_->waiting; }
std::optional<unsigned> RemoteUiClients::itemQuote(uint32_t item, OnlineItemAction action) const {
    return action == OnlineItemAction::RepairAll ? impl_->repairAllQuote() : impl_->itemQuote(item, action);
}
void RemoteUiClients::update(const OnlineSceneView &s, std::optional<Vec> position){impl_->update(s, position);}
std::optional<UnitLightAttributes> RemoteUiClients::light(OnlineUnitKey key) const { return impl_->light(key); }
std::optional<MovementTiming> RemoteUiClients::movement(OnlineUnitKey key,bool running) const { return impl_->movement(key,running); }
const ClassicData &RemoteUiClients::content()const{return impl_->data;}
const std::map<std::string,double,std::less<>> &RemoteUiClients::skillFormulaStats() const {return impl_->skillFormulaStats;}
std::optional<KnownAttackTiming> RemoteUiClients::attackTiming() const {return impl_->characterView.attackTiming;}
IActorClient &RemoteUiClients::actor(){return impl_->actor;}
IInventoryClient &RemoteUiClients::inventory(){return impl_->inventory;}
ICharacterClient &RemoteUiClients::character(){return impl_->character;}
const ICharacterClient &RemoteUiClients::character() const{return impl_->character;}
INpcClient &RemoteUiClients::npc(){return impl_->npc;}
IQuestClient &RemoteUiClients::quests(){return impl_->quests;}
IMapClient &RemoteUiClients::map(){return impl_->map;}
bool RemoteUiClients::running()const{return impl_->run;}
bool RemoteUiClients::busy()const{
    const auto &request=impl_->session.read().world.itemRequest;
    return impl_->waiting.has_value()||!impl_->transactions.empty()||(request&&request->state==OnlineItemRequest::State::Pending);
}
RemoteUiNotice RemoteUiClients::takeNotice(){
    if (!impl_->notice.empty()) return {std::exchange(impl_->notice,{}),true};
    if (impl_->itemNotices.empty()) return {};
    auto notice = std::move(impl_->itemNotices.front()); impl_->itemNotices.pop_front();
    return {std::move(notice),false};
}
}
