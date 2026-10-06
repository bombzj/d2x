#include "frontend.hpp"
#include "app/online_login_memory.hpp"
#include "app/client_preferences.hpp"
#include "app/debug/debug_pipe.hpp"
#include "app/debug/online_commands.hpp"
#include "client/remote_town.hpp"
#include "client/remote_control.hpp"
#include "client/remote_inventory.hpp"
#include "client/remote_combat.hpp"
#include "client/remote_ui_clients.hpp"
#include "presentation/scene_view.hpp"
#include "presentation/controller.hpp"
#include "content/string_table.hpp"
#include "input.hpp"
#include "network/realm_session.hpp"
#include "presentation/frontend/realm_frontend.hpp"
#include "presentation/graphics/primitives.hpp"
#include "presentation/remote/remote_scene.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <deque>
#include <chrono>
#include <limits>
#include <nlohmann/json.hpp>
#include <rlgl.h>

namespace d2x {
namespace {
struct OnlineConfiguration {
    net::LoginOptions login;
    std::string gateway;
};
OnlineConfiguration configuration(const std::filesystem::path &path) {
    // Keep keys out of argv, logs, views, saves and committed configuration.
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Online login configuration is unavailable.");
    nlohmann::json json;
    OnlineConfiguration result;
    try {
        file >> json;
        const auto port = json.value("accountPort", 6112), gamePort = json.value("gamePort", 4000);
        if (port < 1 || port > 65535 || gamePort < 1 || gamePort > 65535)
            throw std::runtime_error("Invalid port");
        result.login.accountServer = {json.at("accountHost").get<std::string>(), uint16_t(port)};
        result.login.gamePort = uint16_t(gamePort);
        result.gateway = json.at("gateway").get<std::string>();
        auto directory = std::filesystem::path(json.at("originalClientDirectory").get<std::string>());
        if (directory.is_relative())
            directory = path.parent_path() / directory;
        constexpr std::array names{"Game.exe", "Bnclient.dll", "D2Client.dll"};
        for (size_t i = 0; i < names.size(); ++i) {
            result.login.originalClient.files[i] = directory / names[i];
            if (!std::filesystem::is_regular_file(result.login.originalClient.files[i]))
                throw std::runtime_error("Missing original file");
        }
        const auto authentication = json.value("authentication", std::string("pvpgn"));
        if (authentication != "pvpgn" && authentication != "keys")
            throw std::runtime_error("Unsupported authentication mode");
        result.login.originalClient.submitKeys = authentication == "keys";
        if (result.login.originalClient.submitKeys) {
            result.login.originalClient.classicKey = json.at("classicKey").get<std::string>();
            result.login.originalClient.expansionKey = json.at("expansionKey").get<std::string>();
        }
        result.login.originalClient.owner = json.value("keyOwner", std::string("D2X"));
        if ((result.login.originalClient.submitKeys && (result.login.originalClient.classicKey.empty() ||
                                                        result.login.originalClient.expansionKey.empty())) ||
            result.gateway.empty() || result.login.accountServer.host.empty())
            throw std::runtime_error("Missing configuration");
        for (auto key : {"classicKey", "expansionKey"})
            if (json.contains(key) && json[key].is_string())
                net::protocol::erase_secret(json[key].get_ref<std::string &>());
        return result;
    } catch (...) {
        for (auto key : {"classicKey", "expansionKey"})
            if (json.contains(key) && json[key].is_string())
                net::protocol::erase_secret(json[key].get_ref<std::string &>());
        net::protocol::erase_secret(result.login.originalClient.classicKey);
        net::protocol::erase_secret(result.login.originalClient.expansionKey);
        throw std::runtime_error("Online login configuration is incomplete or invalid.");
    }
}
void saveFrontendScreenshot(RenderTexture2D target, const std::string &path) {
    const auto destination = std::filesystem::path(path);
    if (destination.has_parent_path()) std::filesystem::create_directories(destination.parent_path());
    Image capture = LoadImageFromTexture(target.texture);
    ImageFlipVertical(&capture);
    const bool saved = ExportImage(capture, path.c_str());
    UnloadImage(capture);
    if (!saved) throw std::runtime_error("Screenshot could not be written");
}
bool gameStage(OnlineStage s) {
    return s == OnlineStage::ConnectingGame || s == OnlineStage::GameHandshake ||
           s == OnlineStage::LoadingGame || s == OnlineStage::ProtocolReady || s == OnlineStage::LeavingGame;
}
} // namespace
void runOnlineFrontend(Archives &archives, RenderTexture2D target, const AppOptions &options) {
    const auto configPath = std::filesystem::path(options.onlineConfig);
    const auto &pipeName = options.debugPipe;
    if (options.hidden && !pipeName.empty()) SetTargetFPS(60);
    RealmFrontend ui(archives);
    OnlineLoginMemory loginMemory(configPath);
    std::string rememberedAccount, rememberedPassword;
    loginMemory.read(rememberedAccount, rememberedPassword);
    ui.setLogin(std::move(rememberedAccount), std::move(rememberedPassword));
    HideCursor();
    net::RealmSession session;
    RemoteTown town(archives);
    RemoteControl control(town, session);
    RemoteInventory inventory(archives);
    RemoteCombat combat(archives, town, session);
    ClientPreferences preferences = loadClientPreferences();
    bool preferencesDirty = false;
    double preferencesRetryAt = 0;
    RemoteMapDisplayState mapDisplay;
    mapDisplay.running = preferences.running;
    mapDisplay.large = preferences.automapLarge;
    std::unique_ptr<RemoteUiClients> sharedClients;
    std::unique_ptr<SceneView> sharedUi;
    std::unique_ptr<SceneController> sharedController;
    auto syncPreferences = [&](bool force = false) {
        if (sharedUi && sharedClients) {
            const auto &panels = sharedUi->ui();
            const ClientPreferences current{sharedClients->running(), panels.miniPanelOpen,
                panels.automapLarge, panels.automapCenterWhenCleared,
                panels.automapParty, panels.automapNames, panels.automapFade};
            if (current != preferences) { preferences = current; preferencesDirty = true; }
        }
        if (!preferencesDirty || (!force && GetTime() < preferencesRetryAt)) return;
        preferencesDirty = !saveClientPreferences(preferences);
        preferencesRetryAt = GetTime() + 2;
    };
    std::optional<uint32_t> displayedNpc;
    std::optional<uint16_t> displayedSpeech{};
    std::optional<uint32_t> displayedWaypoint{};
    std::unique_ptr<RemoteScene> scene;
    std::optional<uint8_t> renderedAct;
    std::optional<uint16_t> renderedArea;
    std::string sceneError;
    uint64_t sceneGeneration = ~uint64_t{};
    uint64_t inputAreaGeneration = ~uint64_t{};
    auto sceneStatus = [&] {
        town.update(session.read());
        auto status = town.read();
        status.automapVisible = mapDisplay.visible;
        status.automapLarge = mapDisplay.large;
        if (sceneGeneration == session.read().gameGeneration && !sceneError.empty()) {
            status.available = status.movementAvailable = false;
            status.reason = sceneError;
        }
        if (status.available && scene && sceneGeneration == session.read().gameGeneration) {
            status.renderedUnits = scene->renderedUnits();
            status.unavailableUnits = scene->unavailableUnits();
            status.effectLimitations = scene->effectLimitations();
            status.playerDisplayed = scene->playerDisplayed();
            status.playerDisplayPosition = scene->playerDisplayPosition();
        }
        return status;
    };
    auto move = [&](OnlinePoint targetPoint, bool run, std::optional<OnlineIntentContext> context = {}) {
        town.update(session.read());
        return sceneError.empty() && control.move(targetPoint, run, context);
    };
    DebugPipe pipe(pipeName);
    bool quit = false, manualRealm = false, presentationPaused = false;
    int frames = 0;
    std::deque<FrameInput> debugInputs;
    auto lastInputTime = std::chrono::steady_clock::now();
    ClassicStrings strings(archives);
    FrontendPage page = FrontendPage::Main;
    std::string notice, gateway = "D2X-Local";
    uint64_t dismissedErrorSequence = std::numeric_limits<uint64_t>::max();
    constexpr float scale = float(H) / 600, offsetX = (W - 800 * scale) / 2;
    // Quick entry executes the real login/Realm/character/game protocol once.
    // No credentials in argv and no retry of non-idempotent room creation.
    bool quickCharacter = !options.onlineCharacter.empty();
    bool quickGame = quickCharacter && (!options.onlineCreateGame.empty() || !options.onlineJoinGame.empty());
    if (quickCharacter) {
        try {
            auto config = configuration(configPath);
            std::string account, password;
            loginMemory.read(account, password);
            if (account.empty() || password.empty()) {
                net::protocol::erase_secret(password);
                throw std::runtime_error("Quick entry needs a remembered login; log in once through the UI.");
            }
            gateway = std::move(config.gateway);
            config.login.account = std::move(account);
            config.login.password = std::move(password);
            session.login(std::move(config.login));
            page = FrontendPage::Login;
        } catch (const std::exception &e) {
            quickCharacter = quickGame = false;
            notice = e.what();
            page = FrontendPage::Login;
        }
    }
    while (true) {
        if (options.frameLimit > 0 && ++frames > options.frameLimit) quit = true;
        if (WindowShouldClose())
            quit = true;
        const auto previousStage = session.read().stage;
        session.tick();
        inventory.update(session.read());
        combat.update();
        if (sceneGeneration != session.read().gameGeneration) {
            mapDisplay.movementHeld = false;
            syncPreferences(true);
            debugInputs.clear();
            presentationPaused = false;
            sharedController.reset(); sharedUi.reset(); sharedClients.reset();
            displayedNpc.reset(); displayedSpeech.reset(); displayedWaypoint.reset();
            scene.reset();
            sceneError.clear();
            sceneGeneration = session.read().gameGeneration;
        }
        if (inputAreaGeneration != session.read().world.areaGeneration) {
            mapDisplay.movementHeld = false;
            inputAreaGeneration = session.read().world.areaGeneration;
            debugInputs.clear();
            control.cancelMovement();
            if (sharedController) sharedController->resetInput();
            scene.reset();
            sceneError.clear();
        }
        if (renderedAct != session.read().load.act) {
            scene.reset();
            sceneError.clear();
            renderedAct = session.read().load.act;
        }
        town.update(session.read());
        const bool focused = debugInputs.empty() ? IsWindowFocused() : debugInputs.front().focused;
        const bool escape = debugInputs.empty() ? IsKeyPressed(KEY_ESCAPE) : debugInputs.front().escape;
        if (focused && escape) control.cancelMovement();
        if (!focused || presentationPaused || (sharedUi && sharedUi->ui().blocksInput()))
            control.cancelMovement();
        control.tick();
        if (renderedArea != town.read().area) {
            scene.reset();
            sceneError.clear();
            renderedArea = town.read().area;
        }
        if (previousStage == OnlineStage::ListingCharacters &&
            session.read().stage == OnlineStage::CharacterSelection)
            page = FrontendPage::Characters;
        pipe.poll([&](const std::string &request) {
            const auto before = session.read().revision;
            const auto beforeStage = session.read().stage;
            const bool wasPaused = presentationPaused;
            auto response = onlineDebugCommand(
                request, session,
                [&] {
                    auto config = configuration(configPath);
                    gateway = std::move(config.gateway);
                    return std::move(config.login);
                },
                quit,
                [&](const std::string &path) { saveFrontendScreenshot(target, path); },
                sceneStatus, control, inventory, combat, [&](bool visible, bool large) {
                    mapDisplay.visible = visible; mapDisplay.large = large;
                    if (sharedUi) { sharedUi->ui().automap = visible; sharedUi->ui().automapLarge = large; }
                }, presentationPaused, [&](std::vector<FrameInput> queued) {
                    if (debugInputs.size() + queued.size() > 32)
                        throw std::invalid_argument("The UI input queue exceeds 32 frames");
                    for (auto &frame : queued) debugInputs.push_back(std::move(frame));
                });
            if (presentationPaused != wasPaused) {
                debugInputs.clear();
                if (sharedUi) sharedUi->pauseDebugPresentation(presentationPaused);
                if (sharedController) sharedController->resetInput();
                if (!presentationPaused) {
                    scene.reset();
                    if (sharedUi) sharedUi->clearClientMissiles();
                }
            }
            if (session.read().revision != before) {
                quickCharacter = quickGame = false;
                notice.clear();
                dismissedErrorSequence = std::numeric_limits<uint64_t>::max();
                if (beforeStage == OnlineStage::CharacterSelection &&
                    session.read().stage == OnlineStage::ListingRealms)
                    manualRealm = true;
                if (session.read().stage == OnlineStage::ConnectingAccount) {
                    manualRealm = false;
                    page = request.find("online-register") != std::string::npos ? FrontendPage::Register
                                                                                : FrontendPage::Login;
                }
                if (session.read().stage == OnlineStage::Cancelled) {
                    ui.clearTransientPasswords();
                    page = FrontendPage::Login;
                }
                if (session.read().stage == OnlineStage::Idle) {
                    ui.clearTransientPasswords();
                    page = FrontendPage::Main;
                }
            }
            auto reply = nlohmann::json::parse(response);
            const auto waiting = sharedClients ? sharedClients->waitingItemRequest() : std::nullopt;
            reply["uiQueue"] = {{"inputFrames", debugInputs.size()},
                {"itemCommands", sharedClients ? sharedClients->queuedItemCommands() : 0},
                {"waitingItemRequest", waiting ? nlohmann::json(*waiting) : nlohmann::json(nullptr)}};
            constexpr std::array pageNames{"Main", "Login", "Register", "Realms", "Characters", "CreateCharacter", "Lobby", "Loading"};
            reply["frontend"] = {{"page", pageNames.at(size_t(page))}, {"notice", notice}};
            return reply.dump();
        });
        if (quit) {
            if (session.read().stage == OnlineStage::ProtocolReady ||
                session.read().stage == OnlineStage::LoadingGame)
                session.leave_game();
            // Keep pumping the same bounded leave exchange when the window closes
            // or command quit is requested. Closing TCP immediately can skip saving.
            if (session.read().stage != OnlineStage::LeavingGame)
                break;
            debugInputs.clear();
            // Keep the final scene/screenshot while the independent service completes
            // the bounded save/leave exchange; no further world input is accepted.
            const auto closingViewport = currentViewport();
            BeginDrawing();
            ClearBackground(BLACK);
            DrawTexturePro(target.texture, {0, 0, float(W), -float(H)},
                {closingViewport.offset.x, closingViewport.offset.y,
                 W * closingViewport.scale, H * closingViewport.scale}, {0, 0}, 0, WHITE);
            EndDrawing();
            continue;
        }
        const auto view = session.read();
        if (view.stage == OnlineStage::RealmSelection) {
            const auto match = std::find_if(view.realms.begin(), view.realms.end(),
                                            [&](const auto &r) { return r.name == gateway; });
            if (!manualRealm && match != view.realms.end())
                session.choose_realm(match->name);
            else {
                manualRealm = true;
                page = FrontendPage::Realms;
            }
        }
        if (view.error) quickCharacter = quickGame = false;
        if (quickCharacter && view.stage == OnlineStage::CharacterSelection) {
            quickCharacter = false;
            const auto found = std::find_if(view.characters.begin(), view.characters.end(),
                [&](const auto &c) { return c.name == options.onlineCharacter; });
            if (found == view.characters.end() || !session.select_character(found->name)) {
                quickGame = false;
                notice = found == view.characters.end() ? "Quick-entry character is absent from the server list."
                    : "Quick-entry character cannot be selected; inspect the server response.";
            }
        }
        if (quickGame && view.stage == OnlineStage::Lobby) {
            quickGame = false;
            const bool accepted = !options.onlineCreateGame.empty()
                ? session.create_game({options.onlineCreateGame, {}, {}, 0, 8, 99})
                : session.join_game(options.onlineJoinGame);
            if (!accepted) notice = "Quick-entry game request was rejected; use the lobby to inspect or retry.";
        }
        if (view.stage != OnlineStage::ProtocolReady && presentationPaused) {
            presentationPaused = false;
            if (sharedUi) sharedUi->pauseDebugPresentation(false);
        }
        if (view.stage == OnlineStage::CharacterSelection && page != FrontendPage::CreateCharacter)
            page = FrontendPage::Characters;
        if (view.stage == OnlineStage::CreatingCharacter)
            page = FrontendPage::CreateCharacter;
        if (view.stage == OnlineStage::Lobby || view.stage == OnlineStage::ListingGames ||
            view.stage == OnlineStage::CreatingGame || view.stage == OnlineStage::JoiningGame)
            page = FrontendPage::Lobby;
        if (gameStage(view.stage))
            page = FrontendPage::Loading;
        if (view.error && dismissedErrorSequence != view.error->sequence && notice.empty()) {
            int id = 0;
            if (view.error->packetId == 0x3D) {
                constexpr std::array<int, 9> ids{0, 5231, 5232, 5233, 5239, 5234, 5235, 5236, 5237};
                id = view.error->serverCode < ids.size() ? ids[view.error->serverCode] : 5249;
            }
            if (view.error->kind == OnlineErrorKind::Server && view.error->packetId == 0x02)
                id = view.error->serverCode == 0x14 ? 5165 : 5140;
            if (view.error->packetId == 0x3A)
                id = view.error->serverCode == 1 ? 5208 : view.error->serverCode == 2 ? 5207 : 5244;
            if (view.error->kind == OnlineErrorKind::Server && view.error->packetId == 0x03)
                id = view.error->serverCode == 0x1E || view.error->serverCode == 0x1F ? 5138
                     : view.error->serverCode == 0x20                                 ? 5139
                                                                                      : 5140;
            if (view.error->kind == OnlineErrorKind::Server && view.error->packetId == 0x04) {
                switch (view.error->serverCode) {
                case 0x29:
                    id = 5160;
                    break;
                case 0x2A:
                    id = 5159;
                    break;
                case 0x2B:
                    id = 5161;
                    break;
                case 0x2C:
                    id = 5162;
                    break;
                default:
                    id = 5141;
                    break;
                }
            }
            if (view.error->kind == OnlineErrorKind::Transport ||
                view.error->kind == OnlineErrorKind::Timeout)
                id = 5247;
            auto original = id ? strings.find(id) : std::string_view{};
            notice = original.empty() || view.error->kind == OnlineErrorKind::Timeout
                ? view.error->message : std::string(original);
        }
        // RealmSession already reduced these packets into its remote-only view.
        // No opaque queue is retained by this value-only client.
        session.take_game_packets();
        town.update(session.read());
        bool showScene =
            view.stage == OnlineStage::ProtocolReady && town.read().available && sceneError.empty();
        if (showScene && !scene) {
            try {
                scene = std::make_unique<RemoteScene>(archives, town.read().palette.value_or(0), mapDisplay);
            } catch (const std::exception &e) {
                sceneError = e.what();
                showScene = false;
            }
        }
        const auto viewport = currentViewport();
        const auto raw = GetMousePosition();
        Vector2 mouse{((raw.x - viewport.offset.x) / viewport.scale - offsetX) / scale,
                      (raw.y - viewport.offset.y) / viewport.scale / scale};
        bool captureRequested = false;
        FrontendIntent action;
        if (quit) debugInputs.clear();
        RemoteSceneIntent worldAction;
        const Vec worldMouse{(raw.x - viewport.offset.x) / viewport.scale,
                             (raw.y - viewport.offset.y) / viewport.scale};
        if (!presentationPaused) {
            BeginTextureMode(target);
            ClearBackground(BLACK);
            if (showScene) {
                try {
                    if (!sharedClients) {
                        // Network worker remains active throughout synchronous resource loading.
                        sharedClients = std::make_unique<RemoteUiClients>(archives,session,inventory,combat,control,preferences.running);
                        sharedClients->update(town.read());
                        sharedUi = std::make_unique<SceneView>(archives,sharedClients->content(),sharedClients->actor(),
                            sharedClients->inventory(),sharedClients->character(),sharedClients->quests(),sharedClients->npc(),sharedClients->map());
                        sharedController = std::make_unique<SceneController>(sharedClients->actor(),sharedClients->inventory(),
                            sharedClients->character(),sharedClients->npc(),sharedClients->map(),*sharedUi);
                        auto &initial = sharedUi->ui();
                        initial.automap = mapDisplay.visible;
                        initial.miniPanelOpen = preferences.miniPanelOpen;
                        initial.automapLarge = preferences.automapLarge;
                        initial.automapCenterWhenCleared = preferences.automapCenterWhenCleared;
                        initial.automapParty = preferences.automapParty;
                        initial.automapNames = preferences.automapNames;
                        initial.automapFade = preferences.automapFade;
                        sharedController->resetInput();
                    }
                    sharedClients->update(town.read());
                    sharedUi->refreshUi(std::clamp(GetFrameTime(),0.f,.1f));
                    auto &panels = sharedUi->ui();
                    const auto &native = view.world;
                    if (!sharedClients->busy()) { panels.inventory.pending = {}; panels.shopSalePending.reset(); }
                    if (native.storage.kind == OnlineStorageKind::Stash && !panels.inventory.storage) {
                        panels.inventory.storage = sharedUi->inventoryView().containers.stash; panels.inventory.open = true;
                        panels.characterOpen = panels.questOpen = panels.hirelingOpen = false;
                    }
                    if (native.storage.kind == OnlineStorageKind::Cube) { panels.inventory.cubeOpen = true; panels.inventory.open = true; }
                    if (native.waypointSource != displayedWaypoint) {
                        displayedWaypoint = native.waypointSource;
                        panels.travelMenu = displayedWaypoint.has_value();
                        panels.waypointSource = displayedWaypoint ? EntityId{(uint64_t{1} << 32) + *displayedWaypoint + 1} : EntityId{};
                        panels.waypointAct = view.load.act.value_or(0);
                        if (panels.travelMenu) { panels.inventory.open = false; panels.skillTreeOpen = panels.characterOpen = panels.questOpen = false; }
                    }
                    if (town.read().npcConversation) {
                        const auto &dialog = *town.read().npcConversation;
                        if (displayedNpc != dialog.source) {
                            displayedNpc = dialog.source;
                            displayedSpeech.reset();
                            sharedUi->openNpcMenu(EntityId{(uint64_t{1} << 32) + dialog.source + 1},dialog.speaker,false);
                        }
                        {
                            // Acknowledging a message changes the acknowledged set,
                            // not necessarily the native conversation revision.
                            if (panels.dialogue.empty() && !panels.shopOpen && !panels.hireListOpen) for (const auto &message : dialog.messages)
                                if (!message.acknowledged && (message.menu == 0 || message.menu == 2) && !message.text.empty()) {
                                    displayedSpeech = message.stringId;
                                    sharedUi->openNpcDialogue(EntityId{(uint64_t{1} << 32) + dialog.source + 1},dialog.speaker,message.text);
                                    break;
                                }
                        }
                    } else if (displayedNpc && !native.npcRequested) {
                        displayedNpc.reset(); displayedSpeech.reset(); sharedUi->cancelNpcDialogue();
                        panels.shopOpen = panels.hireListOpen = false;
                    }
                    auto input = pollInput(viewport);
                    const auto inputNow = std::chrono::steady_clock::now();
                    const bool inputWait = inputNow - lastInputTime > std::chrono::milliseconds(250);
                    lastInputTime = inputNow;
                    if (inputWait) {
                        // Discard buffered physical clicks/keys after blocking window or
                        // resource work; held buttons still require release before reuse.
                        FrameInput quiet;
                        quiet.mouse = input.mouse; quiet.insideViewport = input.insideViewport;
                        quiet.focused = input.focused;
                        quiet.leftHeld = input.leftHeld; quiet.rightHeld = input.rightHeld;
                        input = std::move(quiet);
                        sharedController->resetInput();
                    }
                    if (!debugInputs.empty()) {
                        input = std::move(debugInputs.front());
                        debugInputs.pop_front();
                    }
                    captureRequested = input.screenshot;
                    const bool wasShop = panels.shopOpen, wasCube = panels.inventory.cubeOpen;
                    const auto speech = displayedSpeech ? std::optional<uint32_t>{*displayedSpeech} : panels.dialogueTextTopic;
                    const bool wasSpeech = !panels.dialogue.empty() && speech.has_value();
                    if (input.focused && input.escape && native.waypointRequested && !native.waypointSource) {
                        session.use_waypoint(0, 0, onlineIntentContext(view));
                    }
                    // Acknowledge native automatic speech without ending its server conversation.
                    if (wasSpeech && input.focused && (input.escape || input.leftPressed)) {
                        // Native menu topics use the same original 0x31 as
                        // automatic speech. A topic can grant a quest reward;
                        // merely closing its local text must not lose that ACK.
                        const auto &conversation = native.npcConversation;
                        if (conversation && !conversation->acknowledged.contains(uint16_t(*speech)))
                            session.acknowledge_npc_message(uint16_t(*speech), onlineIntentContext(view));
                        displayedSpeech.reset();
                        sharedUi->cancelNpcDialogue();
                        if (town.read().npcConversation) sharedUi->openNpcMenu(panels.dialogueObject,town.read().npcConversation->speaker,false);
                        input.escape = input.leftPressed = input.leftHeld = false;
                    }
                    const bool keepGame = sharedController->handle(input,GetFrameTime());
                    if (!keepGame) worldAction.leave = true;
                    if (!wasShop && panels.shopOpen) sharedClients->openShop();
                    if (!wasCube && panels.inventory.cubeOpen && native.storage.kind != OnlineStorageKind::Cube)
                        for (const auto &[id,item] : native.items) if (item.code == sharedUi->inventoryView().cubeCode &&
                            item.ownerType == 0 && item.owner == view.load.playerUnitId && item.mode == 0 && item.page == 1) {
                            OnlineItemCommand request; request.action = OnlineItemAction::CubeOpen; request.item = id; request.itemRevision = item.revision;
                            request.context = onlineIntentContext(view);
                            if (!inventory.submit(session,request)) { panels.inventory.cubeOpen = false; sharedUi->notice(inventory.reason(),true); }
                            break;
                        }
                    if (auto feedback = sharedClients->takeNotice(); !feedback.empty()) sharedUi->notice(std::move(feedback),true);
                    mapDisplay.visible = panels.automap; mapDisplay.large = panels.automapLarge;
                    mapDisplay.right = panels.minimapRight; mapDisplay.offset = panels.automapOffset; mapDisplay.running = sharedClients->running();
                    auto rendered = scene->frame(view,*town.map(),town.read(),*sharedUi,combat,sharedController->uiConsumed() || wasSpeech,input);
                    rendered.leave = rendered.leave || worldAction.leave;
                    worldAction = std::move(rendered);
                } catch (const std::exception &e) {
                    sceneError = e.what();
                    showScene = false;
                }
            }
            if (!showScene) {
                auto input = pollInput(viewport);
                if (!debugInputs.empty()) { input = std::move(debugInputs.front()); debugInputs.pop_front(); }
                captureRequested = input.screenshot;
                mouse = {(input.mouse.x - offsetX) / scale, input.mouse.y / scale};
                ClearBackground(BLACK);
                if (view.stage == OnlineStage::ProtocolReady && onlinePlayerDead(view.world) && sharedClients && sharedUi && sharedController) {
                    sharedClients->update(town.read());
                    sharedUi->refreshUi(std::clamp(GetFrameTime(), 0.f, .1f));
                    sharedController->handle(input, GetFrameTime());
                    sharedUi->drawUi(worldMouse);
                } else {
                    BeginScissorMode(int(offsetX), 0, int(800 * scale), H);
                    rlPushMatrix();
                    rlTranslatef(offsetX, 0, 0);
                    rlScalef(scale, scale, 1);
                    action = ui.frame(page, session.read(), gateway, notice, mouse, input, sceneStatus().reason);
                    rlPopMatrix();
                    EndScissorMode();
                }
            }
            EndTextureMode();
        }
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(target.texture, {0, 0, float(W), -float(H)},
                       {viewport.offset.x, viewport.offset.y, W * viewport.scale, H * viewport.scale}, {0, 0},
                       0, WHITE);
        EndDrawing();
        syncPreferences();
        if (captureRequested) saveFrontendScreenshot(target, "artifacts/d2x-capture.png");
        if (quit || presentationPaused)
            continue;
        const auto frameContext = onlineIntentContext(view);
        town.revealVisibleTiles(session.read(), worldAction.visibleMapTiles);
        if (worldAction.stopCombat) {
            OnlineCombatCommand stop; stop.action = OnlineCombatCommand::Action::Stop;
            stop.context = frameContext;
            combat.submit(stop);
        }
        if (worldAction.leave)
            session.leave_game();
        else if (worldAction.pickup) {
            control.cancelMovement();
            OnlineItemCommand request;
            request.action = OnlineItemAction::Pickup;
            request.item = uint32_t(worldAction.pickup->id.value - 1);
            request.itemRevision = worldAction.pickup->revision;
            request.context = frameContext;
            request.toCursor = sharedUi && sharedUi->ui().inventory.open;
            if (!inventory.submit(session, request) && sharedUi) sharedUi->notice(inventory.reason(), true);
        }
        else if (worldAction.combat) {
            worldAction.combat->context = frameContext;
            control.cancelMovement();
            const bool accepted = combat.submit(*worldAction.combat);
            if (scene) scene->combatSubmitted(accepted);
            if (!accepted && sharedUi) sharedUi->notice(combat.reason(),true);
        }
        else if (worldAction.interact && town.permitsInteraction(session.read(), *worldAction.interact)) {
            if (!control.interact(*worldAction.interact, worldAction.run, frameContext) && sharedUi)
                sharedUi->notice(control.reason(), true);
        }
        else if (worldAction.move) {
            const bool accepted = control.move(*worldAction.move, worldAction.run, frameContext, worldAction.moveOrigin);
            if (scene) scene->movementSubmitted(accepted);
        }
        if (action.editedAccount && !loginMemory.write(*action.editedAccount, {}))
            notice = "Unable to remember the edited account name.";
        if (action.command != FrontendCommand::None) quickCharacter = quickGame = false;
        switch (action.command) {
        case FrontendCommand::Exit:
            quit = true;
            ui.clearPassword();
            break;
        case FrontendCommand::Online:
            page = FrontendPage::Login;
            break;
        case FrontendCommand::OpenRegister:
            ui.clearPassword();
            page = FrontendPage::Register;
            break;
        case FrontendCommand::OpenCreateCharacter:
            page = FrontendPage::CreateCharacter;
            break;
        case FrontendCommand::ChangeRealm:
            manualRealm = true;
            session.return_to_realms();
            break;
        case FrontendCommand::SelectRealm:
            if (session.choose_realm(action.name)) {
                gateway = action.name;
                page = FrontendPage::Characters;
            }
            break;
        case FrontendCommand::CreateCharacter:
            session.create_character({std::move(action.name), action.characterClass, action.hardcore});
            break;
        case FrontendCommand::DeleteCharacter:
            session.delete_character(std::move(action.name));
            break;
        case FrontendCommand::ListGames:
            session.list_games();
            break;
        case FrontendCommand::CancelList:
            session.cancel_game_list();
            break;
        case FrontendCommand::JoinGame:
            session.join_game(std::move(action.name), std::move(action.password));
            break;
        case FrontendCommand::Register:
        case FrontendCommand::Login:
            try {
                auto config = configuration(configPath);
                gateway = std::move(config.gateway);
                const bool remembered = loginMemory.write(action.name, action.password);
                ui.setLogin(action.name, action.password);
                config.login.account = std::move(action.name);
                config.login.password = std::move(action.password);
                notice.clear();
                dismissedErrorSequence = std::numeric_limits<uint64_t>::max();
                manualRealm = false;
                if (action.command == FrontendCommand::Register)
                    session.register_account(std::move(config.login));
                else
                    session.login(std::move(config.login));
                if (!remembered) notice = "Login submitted, but login information could not be remembered.";
            } catch (const std::exception &e) {
                notice = e.what();
            }
            break;
        case FrontendCommand::SelectCharacter:
            session.select_character(std::move(action.name));
            break;
        case FrontendCommand::CreateGame:
            session.create_game({std::move(action.name), std::move(action.password),
                                 std::move(action.description), action.difficulty, action.maximumPlayers,
                                 action.levelDifference});
            break;
        case FrontendCommand::LeaveGame:
            if (!session.leave_game()) {
                session.cancel();
                page = FrontendPage::Login;
            }
            break;
        case FrontendCommand::Back:
            ui.clearTransientPasswords();
            notice.clear();
            if (page == FrontendPage::Register && session.read().stage == OnlineStage::Idle) {
                std::string account, password;
                loginMemory.read(account, password);
                ui.setLogin(std::move(account), std::move(password));
                page = FrontendPage::Login;
            }
            else if (page == FrontendPage::CreateCharacter &&
                     session.read().stage == OnlineStage::CharacterSelection)
                page = FrontendPage::Characters;
            else if (session.read().stage == OnlineStage::Lobby)
                session.return_to_characters();
            else {
                session.logout();
                page = page == FrontendPage::Login ? FrontendPage::Main : FrontendPage::Login;
            }
            break;
        case FrontendCommand::Dismiss:
            notice.clear();
            dismissedErrorSequence = session.read().error ? session.read().error->sequence
                : std::numeric_limits<uint64_t>::max();
            if (session.read().stage == OnlineStage::Failed) {
                session.logout();
                if (page != FrontendPage::Register)
                    page = FrontendPage::Login;
            }
            break;
        case FrontendCommand::None:
            break;
        }
        net::protocol::erase_secret(action.password);
    }
    syncPreferences(true);
    session.logout();

}
} // namespace d2x
