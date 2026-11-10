#include "client/actor_client.hpp"
#include "contracts/online.hpp"
#include "content/classic_data.hpp"
#include "scene_view.hpp"
#include "presentation/world/lighting_view.hpp"
#include "presentation/world/weather_view.hpp"
#include "presentation/world/palette_blend_view.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace d2x {
namespace {
constexpr const char *highlightFragment = R"(
#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec2 highlightTransform;
void main() {
    vec4 pixel = texture(texture0, fragTexCoord) * fragColor;
    vec3 color = (pixel.rgb * highlightTransform.x - 0.5) * highlightTransform.y + 0.5;
    finalColor = vec4(clamp(color, vec3(0.0), vec3(1.0)), pixel.a);
}
)";
} // namespace
bool ViewState::capturesWorldInput(bool includePlayerTrade) const {
    return (includePlayerTrade && playerTradeBlocking) || messageLogOpen || gameMenuOpen || help || npcMenu || shopOpen || hireListOpen || !dialogue.empty();
}
bool ViewState::blocksInput() const { return capturesWorldInput() || travelMenu; }
void SceneView::updateAreaTitle(uint64_t game,int level,const std::optional<std::array<uint8_t,37>> &availability) {
    if(areaTitleGame_!=game) {
        areaTitleGame_=game;areaTitleLevel_=level;areaTitleFrames_=120;areaTitlePending_=false;assets_.areaTitle={};return;
    }
    if(areaTitleLevel_!=level) {
        areaTitleLevel_=level;areaTitleFrames_=0;areaTitlePending_=true;assets_.areaTitle={};
    }
    if(!areaTitlePending_ || ((level==8 || level==20) && !availability)) return;
    areaTitlePending_=false;
    assets_.loadAreaTitle(level,availability);
}
void SceneView::deferTooltip(std::vector<ItemTextLine> lines, ItemQuality quality, Vec anchor,
                            std::optional<unsigned> price, std::string_view priceLabel) const {
    deferredTooltip_.active = true;
    deferredTooltip_.lines = std::move(lines);
    deferredTooltip_.quality = quality;
    deferredTooltip_.price = price;
    deferredTooltip_.priceLabel = std::string(priceLabel);
    deferredTooltip_.anchor = anchor;
}
void SceneView::updateWorldAudio(float clock, std::vector<SoundActorView> &actors,
                                std::vector<PresentationSoundEvent> &events, Vec listener, Vec terrainOrigin) {
    for(auto &effect:clientMissiles_) {
        if(effect.age<0 || !effect.releaseSound) continue;
        auto &release=*effect.releaseSound;
        if(!release.sound.empty()) {
            const auto target=std::ranges::find(actors,release.target,&SoundActorView::id);
            const bool attachToMissile=target==actors.end();
            const auto source=attachToMissile?effect.soundEmitter:target->id;
            events.push_back({.kind=PresentationSoundEvent::Kind::Original,.source=source,
                .age=effect.age,.sound=std::move(release.sound)});
            if(!attachToMissile) {
                effect.releaseSound.reset();
                continue;
            }
            release.sound.clear();
        }
        actors.push_back({.id=effect.soundEmitter,.position=effect.pos,.alive=false,
            .audible=worldContains(screen(effect.pos-terrainOrigin))});
    }
    assets_.sceneAudio.update(clock,actors,events,listener);
}
void SceneView::playUiSound(std::string_view sound) { assets_.sceneAudio.play(sound, uint64_t(GetTime() * 25)); }
int SceneView::soundPeriod() const { return lighting_->period(std::clamp(mapView().palette,0,4)); }
void SceneView::greetNpc(EntityId npc) {
    const auto found=npcSoundIdentities_.find(npc);
    if(found!=npcSoundIdentities_.end()) assets_.sceneAudio.npcGreeting(found->second,0,soundPeriod());
}
void SceneView::observeWorldSounds(const OnlineView &online) {
    if(narrativeGameGeneration_!=online.gameGeneration) {
        narrativeGameGeneration_=online.gameGeneration;
        narrativeSequence_=0;
        npcSoundIdentities_.clear();
        beginWorldGame(online.gameGeneration);
    }
    npcSoundIdentities_.clear();
    for(const auto &[key,unit]:online.world.units)
        if(key.type==1 && unit.classId) npcSoundIdentities_.insert_or_assign(EntityId{(uint64_t{1}<<32)+key.id+1},int(*unit.classId));
    for(const auto &event:online.world.combatEvents) {
        if(event.sequence<=narrativeSequence_) continue;
        if(event.kind!=OnlineCombatEvent::Kind::Sound || event.directSound) continue;
        const auto source=online.world.units.find(event.source);
        if(source==online.world.units.end() || !source->second.classId) continue;
        const auto identity=*source->second.classId;
        if(event.source.type==1 && event.auxiliary==18)
            assets_.sceneAudio.npcGreeting(identity,1,soundPeriod());
        else if(event.source.type==1 && event.auxiliary==10)
            playUiSound("shrine_refill");
        else if(event.source.type==0 && event.source.id==online.load.playerUnitId && event.auxiliary==8)
            playUiSound("player_townportal_enter");
        else if(online.protocolProfile=="lod113c" && event.source.type==0 && event.auxiliary>=33 && event.auxiliary<=83)
            assets_.sceneAudio.playerNarrative(identity,int(event.auxiliary));
    }
    narrativeSequence_=online.world.combatSequence;
    if(online.protocolProfile=="lod113c" && online.load.playerUnitId && !characterView_.dead && mapView().hasObserverRoom) {
        const auto self=online.world.units.find({0,*online.load.playerUnitId});
        if(self!=online.world.units.end() && self->second.classId)
            assets_.sceneAudio.enterLevel(online.gameGeneration,*self->second.classId,int(mapView().region),online.world.quests);
    }
}
void SceneView::playItemUse(const InventoryItemView &item) {
    assets_.sceneAudio.itemUse(item.definition,item.quality,item.specialRow);
}
void SceneView::playPlayerFootstep(int identity, bool running, int material, int level, uint64_t frame) {
    assets_.sceneAudio.playerFootstep(identity,running,material,level,frame);
}
void SceneView::questMusic(std::string_view music,int endFrames,int resumeBlock) {
    assets_.streams.questCue(music,0,endFrames,{},0,resumeBlock);
}
void SceneView::beginWorldGame(uint64_t generation) {
    if(questAudioGameGeneration_!=generation) {
        questAudioGameGeneration_=generation;
        assets_.streams.resetQuestCue();
        akaraGossipChoice_.reset();
    }
}
const std::set<std::string, std::less<>> &SceneView::soundLimitations() const { return assets_.sceneAudio.limitations(); }
void SceneView::updateChat(const OnlineSocialView &social,const OnlineSocialView &account) { chat_.update(social,account); }
bool SceneView::partyReady() const { return party_.ready(); }
bool SceneView::storageNavigationReady() const { return assets_.storageNavigation.frames.size() == 12; }
bool SceneView::storageMaterialUpgradeReady() const { return assets_.storageMaterialUpgrade.frames.size() >= 2; }
bool SceneView::storageMaterialsReady() const { return assets_.storagePanel.frames.size() >= 8 && assets_.vendorButtons.frames.size() >= 32; }
const std::string &SceneView::partyReason() const { return party_.reason(); }
void SceneView::debugChatMessage(std::string_view text, bool error) { chat_.diagnostic(text,error); }
const ChatView &SceneView::chat() const { return chat_; }
const TradeInviteView &SceneView::tradeInvite() const { return tradeInvite_; }
void SceneView::drawNativeIceShatter(Vec position, int size) { createIceShatter(position,size); }
const SceneAssets::ProjectileVisual *SceneView::projectileVisual(int id) { return assets_.ensureProjectile(id); }
const SkillOverlayVisual *SceneView::overlayVisual(int id) { return assets_.ensureOverlay(id); }
void SceneView::clearClientMissiles() {
    clientMissiles_.clear();
    clientMissileHiddenOwners_.clear();
    clientMissileOverlays_.clear();
    assets_.sceneAudio.reset();
}
const std::vector<SceneView::ClientMissile> &SceneView::clientMissiles() const { return clientMissiles_; }
std::vector<EntityId> SceneView::takeClientMissileHiddenOwners() {
    std::vector<EntityId> result;
    result.swap(clientMissileHiddenOwners_);
    return result;
}
std::vector<ClientMissileOverlay> SceneView::takeClientMissileOverlays() {
    std::vector<ClientMissileOverlay> result;
    result.swap(clientMissileOverlays_);
    return result;
}
void SceneView::retireClientMissileRoom(const std::array<int,3> &room) {
    std::erase_if(clientMissiles_,[&](const auto &value){return value.roomAttachment && *value.roomAttachment==room;});
}
std::optional<int> SceneView::weaponMissile(std::string_view code) const {
    const auto found = assets_.weaponMissiles.find(code);
    return found == assets_.weaponMissiles.end() ? std::nullopt : std::optional{found->second};
}
void SceneView::cancelPendingClientMissiles(EntityId owner) {
    std::erase_if(clientMissiles_, [&](const auto &value) { return value.flight && value.age < 0 && value.owner == owner; });
}
void SceneView::drawGroundLabels(Vec mouse) const { drawLootLabels(mouse); }
void SceneView::drawHighlightedActor(const Sprite *image, Vec position) const { drawSelectableSprite(image, position, true); }
void SceneView::drawNpcQuestAlert(EntityId npc, Vec position, bool back) const { drawNpcAlert(npc, position, back); }
const ActorAnimation *SceneView::actorAnimation(const ActorAnimationRequest &request, int palette) const {
    return assets_.actorAnimation(request, palette);
}
const ActorAnimation *SceneView::objectAnimation(int identity, int mode, int palette) const {
    return assets_.objectAnimation(identity, mode, palette);
}
std::array<int, 2> SceneView::objectShrineOverlays(int identity, int code) const {
    return assets_.objectShrineOverlays(identity, code);
}
ObjectPresentation SceneView::objectPresentation(int identity, int serverMode, float elapsed) const {
    return assets_.objectPresentation(identity, serverMode, elapsed);
}
std::string SceneView::actorSequenceMode(std::string_view name) const { return assets_.actorSequenceMode(name); }
ViewState &SceneView::ui() { return view_; }
const ViewState &SceneView::ui() const { return view_; }
const InventoryView &SceneView::inventoryView() const { return inventoryView_; }
const CharacterView &SceneView::characterView() const { return characterView_; }
const QuestView &SceneView::questView() const { return questView_; }
bool SceneView::refreshQuests() { return questClient_.refresh(); }
const NpcConversationView &SceneView::npcView() const { return npcView_; }
void SceneView::pauseWorldPresentation(bool paused) { assets_.audio.pauseEmitters(paused); assets_.streams.pauseWeather(paused); }
WeatherDebugState SceneView::debugWeather(std::optional<WeatherDebugMode> mode, int density) {
    const auto state=weather_->debugState();
    if(state.level<0) throw std::invalid_argument("Weather requires a rendered client world");
    if(mode) weather_->debugWeather(*mode,density,assets_.worldLevel(state.level));
    return weather_->debugState();
}
void SceneView::triggerWeatherLightning(float delay) { weather_->lightning(delay); }
void SceneView::updatePlayerTrade(const OnlinePlayerTrade &trade) {
    tradeInvite_.update(trade);
    view_.playerTradeBlocking = trade.active();
    if(trade.active() && chat_.inputOpen()) {
        chat_.sent(true);view_.chatInputOpen=false;
    }
    view_.playerTradeEditable = !trade.ownAgreed && trade.response == OnlinePlayerTrade::Response::None;
    const bool open = trade.phase == OnlinePlayerTrade::Phase::Open;
    if (open && !view_.inventory.playerTradeOpen) {
        view_.inventory.cancelGesture(); view_.inventory.storage = {}; view_.inventory.cubeOpen = false;
        view_.inventory.open = true; view_.miniPanelOpen = false;
        view_.partyOpen = view_.characterOpen = view_.hirelingOpen = view_.questOpen = view_.skillTreeOpen = false;
        view_.travelMenu = view_.shopOpen = view_.npcMenu = view_.hireListOpen = false;
        view_.help = view_.gameMenuOpen = false; view_.skillPicker.reset();
        cancelNpcDialogue();
    } else if (!open && view_.inventory.playerTradeOpen) {
        view_.inventory.open = false; view_.inventory.cancelGesture();
        view_.inventory.selected = {};
    }
    view_.inventory.playerTradeOpen = open;
}
PlayerTradeIntent SceneView::handlePlayerTrade(const FrameInput &input) {
    auto intent = tradeInvite_.handle(input,worldViewport());
    if (intent.consumed || !view_.inventory.playerTradeOpen) return intent;
    const bool busy = bool(view_.inventory.pending) || inventoryView_.cursorItem();
    return tradeInvite_.handleOpen(input,inventoryView_.gold,inventoryView_.goldKnown,busy);
}
void SceneView::drawPlayerTrade(Vec mouse) const {
    if (!view_.inventory.playerTradeOpen) return;
    drawPanelFrame(false); tradeInvite_.drawOpen(characterView_.name);
    if (!tradeInvite_.ready()) return;
    for (bool own : {false,true}) {
        const auto grid = playerTradeGrid(inventoryView_,own);
        if (!grid.container) continue;
        drawContainerGrid(grid,mouse);
        if (!view_.inventory.drag)
            if (auto cell = grid.cellAt(mouse))
                if (const auto *item = inventoryView_.item(inventoryView_.itemAt(grid.container,*cell)); item && !view_.inventory.hidesItem(*item))
                    drawItemTooltip(*item,{mouse.x+170,mouse.y});
    }
}
SceneView::SceneView(Archives &archives, const ClassicData &content, WeatherCycleState &weatherCycle, const IActorClient &actor,
    IInventoryClient &inventory, ICharacterClient &character, IQuestClient &quests, INpcClient &npc, IMapClient &map,
    const AudioSettings &audio)
    : archives_(archives), actorClient_(actor), inventoryClient_(inventory), characterClient_(character), questClient_(quests),
    npcClient_(npc), mapClient_(map), assets_(archives, content, int(map.read().region), audio), lighting_(std::make_unique<LightingView>()),
    weather_(std::make_unique<WeatherView>(weatherCycle)), paletteBlend_(std::make_unique<PaletteBlendView>(archives)),
      painter_(assets_.font), chat_(archives),
      originalMenu_(archives, assets_.font), tradeInvite_(archives, assets_.font, originalMenu_), party_(archives) {
    applyAudioSettings(audio);
    highlightShader_=LoadShaderFromMemory(nullptr,highlightFragment);
    highlightTransform_=GetShaderLocation(highlightShader_,"highlightTransform");
    refreshUi(0);
    view_.skillClass = characterView_.classCode;
    resetQuestAnimations();
}
void SceneView::refreshUi(float dt) {
    refreshInventory(); refreshCharacterView(); refreshInteractions();
    assets_.streams.update(int(mapView().region), dt);
    const int palette=mapView().palette;
    assets_.loadInventoryArt(inventoryView_,palette);
    const auto &p = characterView_;
    view_.skillClass = p.classCode; view_.displayedWeaponSet = p.weaponSet;
    const int left = p.selectedSkills[p.weaponSet * 2], right = p.selectedSkills[p.weaponSet * 2 + 1];
    view_.leftSkill = left < 0 ? std::nullopt : std::optional<int>{left};
    view_.rightSkill = right < 0 ? std::nullopt : std::optional<int>{right};
    if(view_.orificeObject!=inventoryView_.staffSource) {
        view_.orificeObject=inventoryView_.staffSource;view_.orificeItem.reset();
        if(view_.orificeObject) {view_.inventory.open=true;view_.inventory.cancelGesture();view_.npcMenu=false;}
    }
    view_.inventory.syncCursor(inventoryView_,view_.orificeItem?view_.orificeItem->id:EntityId{});
    advanceUi(dt);
}
void SceneView::advanceWorldPresentation(float dt) {
    // World effects share the replica renderer's clock; menu UI keeps its own.
    dt = std::clamp(dt, 0.f, .1f);
    advanceGroundAnimations(dt);
    view_.animationTime += dt;
    if(const auto voice=assets_.streams.advanceQuestCue(dt,!characterView().dead))
        assets_.sceneAudio.play(*voice,uint64_t(view_.animationTime*25.f));
    if(screenShake_) {
        const auto &shake=*screenShake_;
        const double now=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const int64_t elapsed=int64_t(now-shake.startMilliseconds);
        const int64_t end=int64_t(shake.buildMilliseconds)+shake.holdMilliseconds+shake.fadeMilliseconds;
        if(elapsed>end) {screenShake_.reset();screenShakeOffset_={};}
        else if(elapsed>=0) {
            const int magnitude=elapsed<shake.buildMilliseconds?int(int64_t(shake.magnitude)*elapsed/shake.buildMilliseconds):
                elapsed<int64_t(shake.buildMilliseconds)+shake.holdMilliseconds?shake.magnitude:
                shake.fadeMilliseconds?int(int64_t(shake.magnitude)*(end-elapsed)/shake.fadeMilliseconds):0;
            if(magnitude>0) screenShakeOffset_={float(int(limitedRandom(projectileVisualRandom_,uint32_t(2*magnitude)))-magnitude),
                float(int(limitedRandom(projectileVisualRandom_,uint32_t(2*magnitude)))-magnitude)};
        }
    }
}
void SceneView::shakeScreen(int magnitude,int buildFrames,int holdFrames,int fadeFrames,float delay) {
    if(magnitude<0 || buildFrames<0 || holdFrames<=0 || fadeFrames<0) return;
    const double now=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    screenShake_=ScreenShake{now+double(delay)*1000,magnitude,buildFrames*40,holdFrames*40,fadeFrames*40};
}
SceneView::~SceneView() {
    if (highlightShader_.id)
        UnloadShader(highlightShader_);
}
ChatIntent SceneView::handleChat(const FrameInput &input) {
    chat_.setLogOpen(view_.messageLogOpen);
    const bool canOpen = !view_.playerTradeBlocking && !view_.gameMenuOpen && !view_.help && !view_.npcMenu && !view_.shopOpen &&
        !view_.hireListOpen && view_.dialogue.empty() && !view_.travelMenu && !view_.inventory.drag &&
        !view_.inventory.goldDialog && !view_.inventory.identify && !view_.inventory.storage && !view_.inventory.cubeOpen;
    if(chat_.inputOpen() && !canOpen) {
        chat_.sent(true);view_.chatInputOpen=false;
        return {true,{}};
    }
    auto intent = chat_.handle(input, canOpen);
    view_.chatInputOpen = chat_.inputOpen(); view_.messageLogOpen = chat_.logOpen();
    return intent;
}
void SceneView::chatSent(bool accepted) {
    chat_.sent(accepted);
    view_.chatInputOpen = chat_.inputOpen();
}
void SceneView::updateParty(const OnlineSocialView &social, uint32_t self, uint64_t game, bool town) {
    if (party_.update(social, self, game, town, actorClient_.controlledActor().hardcore.value_or(false))) view_.partyOpen = false;
    // D2Client 1.13c RVA 0x9DB90: another roster player's directed flag 8 toward self.
    constexpr uint16_t hostileFlag = 8;
    incomingHostility_ = self != UINT32_MAX && std::any_of(social.relationships.begin(), social.relationships.end(),
        [&](const auto &entry) {
            const auto source = social.players.find(entry.first.first);
            return entry.first.first != self && entry.first.second == self && (entry.second & hostileFlag) &&
                source != social.players.end() && source->second.listed;
        });
}
PartyIntent SceneView::handleParty(const FrameInput &input) {
    if (view_.blocksInput() || view_.inventory.drag ||
        view_.inventory.goldDialog || view_.inventory.identify) {
        bool hidden = false;
        party_.handle(input, hidden);
        return {};
    }
    return party_.handle(input, view_.partyOpen);
}
void SceneView::drawSelectableSprite(const Sprite *image, Vec position, bool highlighted, Color tint,
                                     Vector2 highlight, const PaletteBlendView *lighting, Color light) const {
    if (!image || !image->texture.id)
        return;
    if (lighting) {
        lighting->drawLit(image, position, {&light, 1}, tint, highlighted ? highlight : Vector2{1, 1});
        return;
    }
    if (highlighted && highlightShader_.id) {
        SetShaderValue(highlightShader_, highlightTransform_, &highlight, SHADER_UNIFORM_VEC2);
        BeginShaderMode(highlightShader_);
    }
    sprite(image, position, tint);
    if (highlighted && highlightShader_.id)
        EndShaderMode();
}
Rectangle SceneView::worldViewport() const {
    const bool left = view_.partyOpen || view_.questOpen || view_.characterOpen || view_.hirelingOpen || view_.travelMenu || view_.shopOpen ||
                      view_.inventory.storage || view_.inventory.cubeOpen || view_.inventory.playerTradeOpen;
    const bool right = view_.inventory.open || view_.skillTreeOpen;
    const float begin = left ? classicSideBounds(false).width : 0;
    const float end = right ? classicSideBounds(true).x : W;
    return {begin, 0, end - begin, float(H)};
}
Rectangle SceneView::worldRenderViewport() const {
    return {0, 0, float(W), float(H)};
}
bool SceneView::worldContains(Vec mouse) const {
    const auto viewport = worldViewport();
    return CheckCollisionPointRec(rv(mouse), worldRenderViewport()) &&
        !(viewport.x > 0 && CheckCollisionPointRec(rv(mouse), classicSideBounds(false))) &&
        !(viewport.x + viewport.width < W && CheckCollisionPointRec(rv(mouse), classicSideBounds(true)));
}
Vec SceneView::worldObservationExtent() const {
    const auto viewport = worldViewport();
    const float centerX = viewport.x + viewport.width * .5f;
    return {std::max(centerX, W - centerX)/(16*view_.zoom), H/(16*view_.zoom)};
}
Vec SceneView::screen(Vec p) const {
    const auto viewport = worldViewport();
    const float centerX = viewport.x + viewport.width * .5f;
    return (project(p) - view_.camera) * view_.zoom + Vec{centerX, (H - HUD) * .5f}+screenShakeOffset_;
}
Vec SceneView::world(Vec p) const {
    const auto viewport = worldViewport();
    const float centerX = viewport.x + viewport.width * .5f;
    return unproject((p - Vec{centerX, (H - HUD) * .5f}-screenShakeOffset_) * (1 / view_.zoom) + view_.camera);
}
void SceneView::notice(std::string text, bool error) {
    view_.lootNotice = std::move(text);
    view_.noticeError = error;
    view_.noticeTime = 4;
}
void SceneView::advanceUi(float dt) {
    view_.noticeTime = std::max(0.f, view_.noticeTime - dt);
    if (view_.gameMenuOpen) view_.gameMenuTime += dt;
    advanceNpcDialogue(dt);
    advanceQuestAnimations(dt);
}
void SceneView::refreshNpcView(EntityId npc) {
    const auto &snapshot = npcClient_.read(npc);
    if (snapshot.revision != npcView_.revision || snapshot.npc != npcView_.npc) npcView_ = snapshot;
}
void SceneView::refreshInteractions() {
    const auto &quests = questClient_.read();
    if (quests.revision != questView_.revision) {
        for (size_t index=0; index<questAnimations_.size(); ++index) {
            const auto id = QuestId(index);
            const auto &previous = questView_.entry(id), &current = quests.entry(id);
            if (quests.actor != questView_.actor) questAnimations_[index] = {};
            queueQuestAnimation(id, current.completionPending);
            if (!previous.known || !current.known || quests.actor != questView_.actor) {
                // Only the owner's unviewed native record queues completion,
                // including completion saved before the log was viewed.
                continue;
            }
            if (previous.active != current.active || previous.completed != current.completed ||
                (id != QuestId::DenOfEvil && previous.description != current.description)) {
                view_.questUpdated = int(index);
                view_.questNotice = !view_.questOpen;
            }
        }
        if (quests.showDenRemaining && quests.denRemaining && *quests.denRemaining > 0 &&
            *quests.denRemaining <= 5 && questView_.showDenRemaining &&
            questView_.denRemaining && quests.denRemaining != questView_.denRemaining) {
            view_.questUpdated = int(QuestId::DenOfEvil);
            view_.questNotice = !view_.questOpen;
        }
        questView_ = quests;
    }
    const auto &scene = npcClient_.scene();
    if (scene.revision != npcScene_.revision) npcScene_ = scene;
    refreshNpcView(view_.dialogueObject);
    if(npcView_.scrollMessage && npcView_.introduction && view_.dialogue.empty()) {
        openNpcDialogue(npcView_.npc,npcView_.speaker,*npcView_.introduction,npcView_.scrollMessage);view_.dialogueTextTopic=npcView_.scrollMessage;
    }
}
void SceneView::refreshCharacterView() {
    const auto &snapshot = characterClient_.read();
    if (snapshot.revision != characterView_.revision) characterView_ = snapshot;
}
void SceneView::refreshInventory() {
    const auto &snapshot = inventoryClient_.read();
    if (snapshot.revision == inventoryView_.revision) return;
    if (inventoryView_.revision && snapshot.gameGeneration == inventoryView_.gameGeneration &&
        snapshot.areaGeneration == inventoryView_.areaGeneration && !snapshot.dead) {
        for (const auto &[id, item] : snapshot.items) {
            const auto *before = inventoryView_.item(id);
            const auto *location = std::get_if<ContainerLocation>(&item.location);
            if (!before || !location || before->location == item.location) continue;
            const auto *previous = std::get_if<ContainerLocation>(&before->location);
            if (location->container == snapshot.containers.cursor) {
                if (previous) playUiSound("item_pickup");
                else if (std::holds_alternative<GroundLocation>(before->location))
                    assets_.sceneAudio.itemPlacement(item.definition,item.quality,item.specialRow);
            } else if ((previous && previous->container == inventoryView_.containers.cursor) ||
                       std::holds_alternative<GroundLocation>(before->location)) {
                assets_.sceneAudio.itemPlacement(item.definition,item.quality,item.specialRow);
            }
        }
    }
    inventoryView_ = snapshot;
}
} // namespace d2x
