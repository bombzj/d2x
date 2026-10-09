#include "client/actor_client.hpp"
#include "content/classic_data.hpp"
#include "scene_view.hpp"
#include <algorithm>

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
void SceneView::updatePlayerTrade(const OnlinePlayerTrade &trade) {
    tradeInvite_.update(trade);
    view_.playerTradeBlocking = trade.active();
    view_.playerTradeEditable = !trade.ownAgreed && trade.response == OnlinePlayerTrade::Response::None;
    const bool open = trade.phase == OnlinePlayerTrade::Phase::Open;
    if (open && !view_.inventory.playerTradeOpen) {
        view_.inventory.cancelGesture(); view_.inventory.storage = {}; view_.inventory.cubeOpen = false;
        view_.inventory.open = true; view_.miniPanelOpen = false;
        view_.characterOpen = view_.hirelingOpen = view_.questOpen = view_.skillTreeOpen = false;
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
                if (const auto *item = inventoryView_.item(inventoryView_.itemAt(grid.container,*cell)))
                    drawItemTooltip(*item,{mouse.x+170,mouse.y});
    }
}
SceneView::SceneView(Archives &archives, const ClassicData &content, const IActorClient &actor,
    IInventoryClient &inventory, ICharacterClient &character, IQuestClient &quests, INpcClient &npc, IMapClient &map)
    : archives_(archives), actorClient_(actor), inventoryClient_(inventory), characterClient_(character), questClient_(quests),
      npcClient_(npc), mapClient_(map), assets_(archives, content), paletteBlend_(archives),
      painter_(assets_.font), speechPainter_(assets_.speechFont), chat_(archives),
      originalMenu_(archives, assets_.font), tradeInvite_(archives, assets_.font, originalMenu_) {
    highlightShader_=LoadShaderFromMemory(nullptr,highlightFragment);
    highlightTransform_=GetShaderLocation(highlightShader_,"highlightTransform");
    refreshUi(0);
    view_.skillClass = characterView_.classCode;
    resetQuestAnimations();
}
void SceneView::refreshUi(float dt) {
    refreshInventory(); refreshCharacterView(); refreshInteractions();
    const int palette=mapView().palette;
    if (itemGroundPalette_!=palette) { assets_.itemGround.clear(); itemGroundPalette_=palette; }
    assets_.loadInventoryArt(inventoryView_,palette);
    advanceGroundAnimations(dt);
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
    view_.animationTime += dt;
    advanceUi(dt);
}
SceneView::~SceneView() {
    if (highlightShader_.id)
        UnloadShader(highlightShader_);
}
ChatIntent SceneView::handleChat(const FrameInput &input) {
    chat_.setLogOpen(view_.messageLogOpen);
    const bool canOpen = !view_.playerTradeBlocking && !view_.gameMenuOpen && !view_.help && !view_.npcMenu && !view_.shopOpen &&
        !view_.hireListOpen && view_.dialogue.empty() && !view_.travelMenu && !view_.inventory.drag &&
        !view_.inventory.split && !view_.inventory.goldDialog && !view_.inventory.identify;
    auto intent = chat_.handle(input, canOpen);
    view_.chatInputOpen = chat_.inputOpen(); view_.messageLogOpen = chat_.logOpen();
    return intent;
}
void SceneView::chatSent(bool accepted) {
    chat_.sent(accepted);
    view_.chatInputOpen = chat_.inputOpen();
}
void SceneView::drawSelectableSprite(const Sprite *image, Vec position, bool highlighted, Color tint,
                                     Vector2 highlight) const {
    if (!image || !image->texture.id)
        return;
    if (highlighted && highlightShader_.id) {
        SetShaderValue(highlightShader_, highlightTransform_, &highlight, SHADER_UNIFORM_VEC2);
        BeginShaderMode(highlightShader_);
    }
    sprite(image, position, tint);
    if (highlighted && highlightShader_.id)
        EndShaderMode();
}
Rectangle SceneView::worldViewport() const {
    const bool left = view_.questOpen || view_.characterOpen || view_.hirelingOpen || view_.travelMenu || view_.shopOpen ||
                      view_.inventory.storage || view_.inventory.cubeOpen || view_.inventory.playerTradeOpen;
    const bool right = view_.inventory.open || view_.skillTreeOpen;
    const float begin = left ? classicSideBounds(false).width : 0;
    const float end = right ? classicSideBounds(true).x : W;
    return {begin, 0, end - begin, float(H - HUD)};
}
Vec SceneView::screen(Vec p) const {
    const auto viewport = worldViewport();
    const float centerX = viewport.x + viewport.width * .5f;
    return (project(p) - view_.camera) * view_.zoom + Vec{centerX, (H - HUD) * .5f};
}
Vec SceneView::world(Vec p) const {
    const auto viewport = worldViewport();
    const float centerX = viewport.x + viewport.width * .5f;
    return unproject((p - Vec{centerX, (H - HUD) * .5f}) * (1 / view_.zoom) + view_.camera);
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
            if (!previous.known || !current.known || quests.actor != questView_.actor) {
                // Initial owner snapshots establish the baseline, not a new reward.
                questAnimations_[index] = {};
                questAnimations_[index].completed = current.completed;
                continue;
            }
            queueQuestAnimation(id, current.completed);
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
        openNpcDialogue(npcView_.npc,npcView_.speaker,*npcView_.introduction);view_.dialogueTextTopic=npcView_.scrollMessage;
    }
}
void SceneView::refreshCharacterView() {
    const auto &snapshot = characterClient_.read();
    if (snapshot.revision != characterView_.revision) characterView_ = snapshot;
}
void SceneView::refreshInventory() {
    const auto &snapshot = inventoryClient_.read();
    if (snapshot.revision != inventoryView_.revision) inventoryView_ = snapshot;
}
} // namespace d2x
