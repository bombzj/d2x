#include "frontend.hpp"
#include "app/online_login_memory.hpp"
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
bool gameStage(OnlineStage s) {
    return s == OnlineStage::ConnectingGame || s == OnlineStage::GameHandshake ||
           s == OnlineStage::LoadingGame || s == OnlineStage::ProtocolReady || s == OnlineStage::LeavingGame;
}
} // namespace
std::optional<CharacterChoice> chooseFrontend(Archives &archives, RenderTexture2D target,
                                              const std::filesystem::path &configPath,
                                              const std::string &pipeName, bool returnToLocalCharacters) {
    RealmFrontend ui(archives);
    OnlineLoginMemory loginMemory(configPath);
    std::string rememberedAccount, rememberedPassword;
    loginMemory.read(rememberedAccount, rememberedPassword);
    ui.setLogin(std::move(rememberedAccount), std::move(rememberedPassword));
    HideCursor();
    if (returnToLocalCharacters) {
        if (auto chosen = chooseCharacter(archives, target))
            return chosen;
        while (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !WindowShouldClose()) {
            BeginDrawing();
            EndDrawing();
        }
    }
    net::RealmSession session;
    RemoteTown town(archives);
    RemoteControl control(town, session);
    RemoteInventory inventory(archives);
    RemoteCombat combat(archives, town, session);
    RemoteMapDisplayState mapDisplay;
    std::unique_ptr<RemoteUiClients> sharedClients;
    std::unique_ptr<SceneView> sharedUi;
    std::unique_ptr<SceneController> sharedController;
    std::optional<uint32_t> displayedNpc;
    std::optional<uint16_t> displayedSpeech{};
    std::optional<uint32_t> displayedWaypoint{};
    uint64_t displayedNpcRevision{};
    std::unique_ptr<RemoteScene> scene;
    std::optional<uint8_t> renderedAct;
    std::optional<uint16_t> renderedArea;
    std::string sceneError;
    uint64_t sceneGeneration = ~uint64_t{};
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
        }
        return status;
    };
    auto move = [&](OnlinePoint targetPoint, bool run) {
        town.update(session.read());
        return sceneError.empty() && control.move(targetPoint, run);
    };
    DebugPipe pipe(pipeName);
    bool quit = false, manualRealm = false;
    ClassicStrings strings(archives);
    FrontendPage page = FrontendPage::Main;
    std::string notice, gateway = "D2X-Local";
    uint64_t dismissedRevision = std::numeric_limits<uint64_t>::max();
    constexpr float scale = float(H) / 600, offsetX = (W - 800 * scale) / 2;
    while (true) {
        if (WindowShouldClose())
            quit = true;
        const auto previousStage = session.read().stage;
        session.tick();
        inventory.update(session.read());
        combat.update();
        if (sceneGeneration != session.read().gameGeneration) {
            sharedController.reset(); sharedUi.reset(); sharedClients.reset();
            displayedNpc.reset(); displayedSpeech.reset(); displayedWaypoint.reset();
            scene.reset();
            sceneError.clear();
            sceneGeneration = session.read().gameGeneration;
        }
        if (renderedAct != session.read().load.act) {
            scene.reset();
            sceneError.clear();
            renderedAct = session.read().load.act;
        }
        town.update(session.read());
        if (IsKeyPressed(KEY_ESCAPE)) control.cancelMovement();
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
            auto response = onlineDebugCommand(
                request, session,
                [&] {
                    auto config = configuration(configPath);
                    gateway = std::move(config.gateway);
                    return std::move(config.login);
                },
                quit,
                [&](const std::string &path) {
                    auto destination = std::filesystem::path(path);
                    if (destination.has_parent_path())
                        std::filesystem::create_directories(destination.parent_path());
                    Image capture = LoadImageFromTexture(target.texture);
                    ImageFlipVertical(&capture);
                    const bool saved = ExportImage(capture, path.c_str());
                    UnloadImage(capture);
                    if (!saved)
                        throw std::runtime_error("Screenshot could not be written");
                },
                sceneStatus, control, inventory, combat, [&](bool visible, bool large) {
                    mapDisplay.visible = visible; mapDisplay.large = large;
                    if (sharedUi) { sharedUi->ui().automap = visible; sharedUi->ui().automapLarge = large; }
                });
            if (session.read().revision != before) {
                notice.clear();
                dismissedRevision = std::numeric_limits<uint64_t>::max();
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
            return response;
        });
        if (quit) {
            if (session.read().stage == OnlineStage::ProtocolReady ||
                session.read().stage == OnlineStage::LoadingGame)
                session.leave_game();
            // Keep pumping the same bounded leave exchange when the window closes
            // or command quit is requested. Closing TCP immediately can skip saving.
            if (session.read().stage != OnlineStage::LeavingGame)
                break;
        }
        const auto &view = session.read();
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
        if (view.stage == OnlineStage::CharacterSelection && page != FrontendPage::CreateCharacter)
            page = FrontendPage::Characters;
        if (view.stage == OnlineStage::CreatingCharacter)
            page = FrontendPage::CreateCharacter;
        if (view.stage == OnlineStage::Lobby || view.stage == OnlineStage::ListingGames ||
            view.stage == OnlineStage::CreatingGame || view.stage == OnlineStage::JoiningGame)
            page = FrontendPage::Lobby;
        if (gameStage(view.stage))
            page = FrontendPage::Loading;
        if (view.error && dismissedRevision != view.revision && notice.empty()) {
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
            notice = original.empty() ? view.error->message : std::string(original);
        }
        // RealmSession already reduced these packets into its remote-only view.
        // Drain the retained opaque stream; unsupported messages have no local fallback.
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
        BeginTextureMode(target);
        ClearBackground(BLACK);
        FrontendIntent action;
        RemoteSceneIntent worldAction;
        const Vec worldMouse{(raw.x - viewport.offset.x) / viewport.scale,
                             (raw.y - viewport.offset.y) / viewport.scale};
        if (showScene) {
            try {
                if (!sharedClients) {
                    // Full UI resource loading can be long enough to miss native keepalives.
                    // Pump only during construction, outside live-world rendering iterations.
                    struct UiLoadingKeepalive {
                        Archives &archives;
                        UiLoadingKeepalive(Archives &a, net::RealmSession &s) : archives(a) {
                            archives.setLoadingPulse([&s] { s.tick(); });
                        }
                        ~UiLoadingKeepalive() { archives.setLoadingPulse({}); }
                    } keepalive(archives,session);
                    sharedClients = std::make_unique<RemoteUiClients>(archives,session,inventory,combat,control);
                    sharedClients->update(town.read());
                    sharedUi = std::make_unique<SceneView>(archives,sharedClients->content(),sharedClients->actor(),
                        sharedClients->inventory(),sharedClients->character(),sharedClients->quests(),sharedClients->npc(),sharedClients->map());
                    sharedController = std::make_unique<SceneController>(sharedClients->actor(),sharedClients->inventory(),
                        sharedClients->character(),sharedClients->npc(),sharedClients->map(),*sharedUi);
                    sharedUi->ui().automap = mapDisplay.visible;
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
                        displayedNpcRevision = 0; displayedSpeech.reset();
                        sharedUi->openNpcMenu(EntityId{(uint64_t{1} << 32) + dialog.source + 1},dialog.speaker,false);
                    }
                    if (displayedNpcRevision != dialog.revision) {
                        displayedNpcRevision = dialog.revision;
                        if (panels.dialogue.empty()) for (const auto &message : dialog.messages)
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
                const bool wasShop = panels.shopOpen, wasCube = panels.inventory.cubeOpen;
                const bool wasSpeech = !panels.dialogue.empty() && displayedSpeech.has_value();
                // Acknowledge native automatic speech without ending its server conversation.
                if (wasSpeech && (input.escape || input.leftPressed)) {
                    session.acknowledge_npc_message(displayedSpeech.value_or(0)); displayedSpeech.reset();
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
            ClearBackground(BLACK);
            if (view.stage == OnlineStage::ProtocolReady && onlinePlayerDead(view.world) && sharedClients && sharedUi && sharedController) {
                sharedClients->update(town.read());
                sharedUi->refreshUi(std::clamp(GetFrameTime(), 0.f, .1f));
                sharedController->handle(pollInput(viewport), GetFrameTime());
                sharedUi->drawUi(worldMouse);
            } else {
                BeginScissorMode(int(offsetX), 0, int(800 * scale), H);
                rlPushMatrix();
                rlTranslatef(offsetX, 0, 0);
                rlScalef(scale, scale, 1);
                action = ui.frame(page, session.read(), gateway, notice, mouse, sceneStatus().reason);
                rlPopMatrix();
                EndScissorMode();
            }
        }
        EndTextureMode();
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(target.texture, {0, 0, float(W), -float(H)},
                       {viewport.offset.x, viewport.offset.y, W * viewport.scale, H * viewport.scale}, {0, 0},
                       0, WHITE);
        EndDrawing();
        if (quit)
            continue;
        town.revealVisibleTiles(session.read(), worldAction.visibleMapTiles);
        if (worldAction.stopCombat) {
            OnlineCombatCommand stop; stop.action = OnlineCombatCommand::Action::Stop;
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
            request.toCursor = sharedUi && sharedUi->ui().inventory.open;
            if (!inventory.submit(session, request) && sharedUi) sharedUi->notice(inventory.reason(), true);
        }
        else if (worldAction.combat) {
            control.cancelMovement();
            const bool accepted = combat.submit(*worldAction.combat);
            if (scene) scene->combatSubmitted(accepted);
            if (!accepted && sharedUi) sharedUi->notice(combat.reason(),true);
        }
        else if (worldAction.interact && town.permitsInteraction(session.read(), *worldAction.interact)) {
            if (!control.interact(*worldAction.interact, worldAction.run) && sharedUi)
                sharedUi->notice(control.reason(), true);
        }
        else if (worldAction.move)
            move(*worldAction.move, worldAction.run);
        if (action.editedAccount && !loginMemory.write(*action.editedAccount, {}))
            notice = "Unable to remember the edited account name.";
        switch (action.command) {
        case FrontendCommand::Exit:
            quit = true;
            ui.clearPassword();
            break;
        case FrontendCommand::Offline:
            if (auto chosen = chooseCharacter(archives, target))
                return chosen;
            while (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !WindowShouldClose()) {
                BeginDrawing();
                EndDrawing();
            }
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
                dismissedRevision = std::numeric_limits<uint64_t>::max();
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
            dismissedRevision = session.read().revision;
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
    session.logout();
    return std::nullopt;
}
} // namespace d2x
