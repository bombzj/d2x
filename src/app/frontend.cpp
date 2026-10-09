#include "frontend.hpp"
#include "hosting/embedded_realm.hpp"
#include "network/tcp_stream.hpp"
#include "network/local_addresses.hpp"
#include "app/online_login_memory.hpp"
#include "app/client_preferences.hpp"
#include "app/debug/debug_pipe.hpp"
#include "app/debug/online_commands.hpp"
#include "app/debug/server_commands.hpp"
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
#include <utility>
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
void runFrontend(Archives &archives, RenderTexture2D target, const AppOptions &options) {
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
    EmbeddedRealm embedded(archives, options.hostSaves);
    bool localConnection = false, enterLocalGame = false;
    bool lanConnection = false, lanListening = false;
    std::string reloadCharacter;
    uint8_t localDifficulty{};
    auto administerLocal = [&](const hosting::AdminRequest &request) -> hosting::AdminResult {
        using namespace hosting;
        if (!localConnection && !lanListening) return {AdminStatus::Unavailable, "No embedded host connection"};
        if (request.operation == AdminOperation::Reload) {
            const auto primary = embedded.diagnostics().player;
            if (!localConnection || !primary || primary->game != request.target.game || primary->player != request.target.player ||
                session.read().stage != OnlineStage::ProtocolReady || !session.read().load.difficulty || session.read().selectedCharacter.empty())
                return {AdminStatus::Unavailable, "Reload requires the primary entered memory client"};
        }
        auto result = embedded.administer(request);
        if (result.applied() && request.operation == AdminOperation::Reload) {
            const auto &current = session.read();
            const auto name = current.selectedCharacter;
            const auto difficulty = *current.load.difficulty;
            if (!session.leave_game()) {
                embedded.administer({AdminOperation::CancelReload, request.target, {}});
                return {AdminStatus::Failed, "Native leave could not be queued; prepared reload cancelled"};
            }
            reloadCharacter = name; localDifficulty = difficulty;
        }
        return result;
    };
    auto hostFrame = std::chrono::steady_clock::now();
    RemoteTown town(archives);
    RemoteControl control(town, session);
    RemoteInventory inventory(archives);
    RemoteCombat combat(archives, town, session, inventory);
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
    uint32_t displayedWaypoint{};
    bool displayedWaypointKnown{};
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
            status.players = scene->players();
        }
        return status;
    };
    DebugPipe pipe(pipeName);
    bool quit = false, manualRealm = false, presentationPaused = false;
    int frames = 0;
    std::deque<FrameInput> debugInputs;
    auto lastInputTime = std::chrono::steady_clock::now();
    auto chatInput = [&](FrameInput &input) {
        sharedUi->updateChat(session.read().world.social);
        sharedUi->updatePlayerTrade(session.read().world.playerTrade);
        auto trade = sharedUi->handlePlayerTrade(input);
        bool consumed = trade.consumed;
        if (!trade.error.empty()) sharedUi->notice(std::move(trade.error), true);
        if (trade.accept) {
            const bool accepted = session.respond_player_trade(*trade.accept, trade.revision);
            if (!accepted && session.read().error) sharedUi->notice(session.read().error->message, true);
        }
        if (trade.action) {
            const bool accepted = !sharedClients->busy() && session.update_player_trade(*trade.action,trade.revision,trade.amount);
            if (!accepted) sharedUi->notice(sharedClients->busy() ? "Wait for the queued item operation." :
                session.read().error ? session.read().error->message : "Trade offer could not be submitted.",true);
        }
        if (!consumed && !session.read().world.playerTrade.active()) {
            auto intent = sharedUi->handleChat(input);
            if (!intent.error.empty()) sharedUi->notice(std::move(intent.error), true);
            if (intent.message) {
                const bool accepted = session.send_chat(std::move(*intent.message));
                sharedUi->chatSent(accepted);
                if (!accepted && session.read().error) sharedUi->notice(session.read().error->message, true);
            }
            consumed = intent.consumed;
        }
        if (!consumed) return;
        // The server trade modal or chat owns the whole opening/closing frame.
        // Retain held buttons so reopening gameplay requires a physical release.
        sharedController->resetInput();
        FrameInput quiet;
        quiet.mouse = input.mouse; quiet.insideViewport = input.insideViewport;
        quiet.focused = input.focused; quiet.leftHeld = input.leftHeld; quiet.rightHeld = input.rightHeld;
        input = std::move(quiet);
    };
    ClassicStrings strings(archives);
    FrontendPage page = FrontendPage::Main;
    std::string notice, gateway = "D2X-Local";
    uint64_t dismissedErrorSequence = std::numeric_limits<uint64_t>::max();
    std::optional<uint64_t> worldNoticeSequence;
    uint64_t worldNoticeGeneration{};
    constexpr float offsetX = (W - 800) / 2, offsetY = (H - 600) / 2;
    // Quick entry executes the real login/Realm/character/game protocol once.
    // No credentials in argv and no retry of non-idempotent room creation.
    bool quickCharacter = !options.onlineCharacter.empty();
    bool quickGame = quickCharacter && (!options.onlineCreateGame.empty() || !options.onlineJoinGame.empty());
    if (!options.hostLan.empty()) {
        try { embedded.listen(options.hostLan, options.realmPort, options.gamePort); lanListening = true; }
        catch (const std::exception &error) { throw std::runtime_error(std::string("LAN host could not start: ") + error.what()); }
    }
    if (!options.lan.empty()) {
        session.connect_realm(std::make_unique<net::TcpStream>(), std::make_unique<net::TcpStream>(),
            {options.lan, options.realmPort}, "LAN", options.gamePort);
        lanConnection = true;
        page = FrontendPage::Characters;
    }
    if (quickCharacter && options.lan.empty()) {
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
    if (!options.load.empty() || !options.save.empty() || !options.characterClass.empty()) {
        try {
            reloadCharacter = embedded.prepareStartup(options.load, options.save, options.characterClass);
            auto streams = embedded.connect();
            session.connect_realm(std::move(streams.realm), std::move(streams.game),
                {"127.0.0.1", options.realmPort}, "Single Player", options.gamePort);
            localConnection = true; page = FrontendPage::Characters;
        } catch (const std::exception &e) { reloadCharacter.clear(); notice = e.what(); }
    }
    while (true) {
        if (options.frameLimit > 0 && ++frames > options.frameLimit) quit = true;
        if (WindowShouldClose())
            quit = true;
        const auto hostNow = std::chrono::steady_clock::now();
        const bool hostFocused = debugInputs.empty() ? IsWindowFocused() : debugInputs.front().focused;
        embedded.pump(std::chrono::duration<double>(hostNow - hostFrame).count(),
            !hostFocused || presentationPaused || (sharedUi && sharedUi->ui().gameMenuOpen));
        hostFrame = hostNow;
        if (auto error = embedded.takeError(); !error.empty()) {
            notice = std::move(error); quit = false; frames = 0; enterLocalGame = false; reloadCharacter.clear();
        }
        const auto previousStage = session.read().stage;
        session.tick();
        if (session.read().world.playerTrade.active()) control.cancelMovement();
        inventory.update(session.read());
        combat.update();
        if (sceneGeneration != session.read().gameGeneration) {
            syncPreferences(true);
            debugInputs.clear();
            presentationPaused = false;
            sharedController.reset(); sharedUi.reset(); sharedClients.reset();
            displayedNpc.reset(); displayedWaypoint = 0; displayedWaypointKnown = false;
            scene.reset();
            sceneError.clear();
            sceneGeneration = session.read().gameGeneration;
        }
        if (inputAreaGeneration != session.read().world.areaGeneration) {
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
                }, [&](uint32_t item, OnlineItemAction action) -> std::optional<unsigned> {
                    if (!sharedClients) return {};
                    inventory.update(session.read());
                    return sharedClients->itemQuote(item, action);
                }, [&](const nlohmann::json &command) {
                    return serverDebugCommand(command, localConnection || lanListening ? &embedded : nullptr, administerLocal);
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
                    localConnection = enterLocalGame = lanConnection = false; reloadCharacter.clear();
                    manualRealm = false;
                    page = request.find("online-register") != std::string::npos ? FrontendPage::Register
                                                                                : FrontendPage::Login;
                }
                if (session.read().stage == OnlineStage::Cancelled) {
                    ui.clearTransientPasswords();
                    page = lanConnection ? FrontendPage::TcpIp : localConnection ? FrontendPage::Main : FrontendPage::Login;
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
            constexpr std::array pageNames{"Main", "Login", "Register", "Realms", "Characters", "CreateCharacter", "Lobby", "Loading", "TcpIp", "JoinHost"};
            reply["frontend"] = {{"page", pageNames.at(size_t(page))}, {"notice", notice}};
            if (sharedUi) {
                const auto &chat = sharedUi->chat();
                reply["chatUi"] = {{"ready", chat.ready()}, {"inputOpen", chat.inputOpen()}, {"logOpen", chat.logOpen()},
                    {"draft", chat.draft()}, {"scroll", chat.scroll()}, {"rows", chat.rows()},
                    {"unavailableMessages", chat.unavailable()}, {"reason", chat.reason()}};
                const auto &trade = sharedUi->tradeInvite();
                reply["tradeInviteUi"] = {{"ready", trade.ready()}, {"active", trade.active()}, {"reason", trade.reason()}};
            }
            return reply.dump();
        });
        if (quit) {
            if (session.read().stage == OnlineStage::ProtocolReady ||
                session.read().stage == OnlineStage::LoadingGame)
                session.leave_game();
            // Keep pumping the same bounded leave exchange when the window closes
            // or command quit is requested. Closing TCP immediately can skip saving.
            if (session.read().stage != OnlineStage::LeavingGame) {
                try { embedded.shutdown(); break; }
                catch (const std::exception &e) { notice = e.what(); quit = false; frames = 0; }
            }
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
            if (quit) continue;
        }
        auto view = session.read();
        if (localConnection && !reloadCharacter.empty() && view.stage == OnlineStage::CharacterSelection) {
            enterLocalGame = session.select_character(std::exchange(reloadCharacter, {}));
        }
        if (localConnection && enterLocalGame && view.stage == OnlineStage::Lobby) {
            enterLocalGame = false;
            session.create_game({lanConnection ? view.selectedCharacter : "SinglePlayer", {}, {}, localDifficulty,
                                 lanConnection ? uint8_t(8) : uint8_t(1), 99});
        } else if (localConnection && !enterLocalGame && view.stage == OnlineStage::Lobby) {
            // Rejected automatic admission returns to characters, never to a lobby.
            session.return_to_characters();
        }
        if (worldNoticeSequence && (view.stage != OnlineStage::ProtocolReady ||
                                   view.gameGeneration != worldNoticeGeneration)) {
            notice.clear();
            dismissedErrorSequence = *worldNoticeSequence;
            worldNoticeSequence.reset();
        }
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
        // Automatic commands above can change the stage in this same frame.
        view = session.read();
        if (enterLocalGame && view.error && view.stage == OnlineStage::CharacterSelection)
            enterLocalGame = false;
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
            page = localConnection ? FrontendPage::Loading : FrontendPage::Lobby;
        if (localConnection && view.stage == OnlineStage::SelectingCharacter)
            page = FrontendPage::Loading;
        if (gameStage(view.stage))
            page = FrontendPage::Loading;
        const bool listTimeout = view.error && view.error->kind == OnlineErrorKind::Timeout &&
            view.error->packetId == 0x05 && view.stage == OnlineStage::Lobby && !view.gameListComplete;
        if (listTimeout)
            dismissedErrorSequence = view.error->sequence; // Join page displays the incomplete-list state inline.
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
            if (!original.empty() && view.error->kind == OnlineErrorKind::Transport)
                notice += "\n" + view.error->message;
            if (view.stage == OnlineStage::ProtocolReady) {
                worldNoticeSequence = view.error->sequence;
                worldNoticeGeneration = view.gameGeneration;
            }
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
        Vector2 mouse{(raw.x - viewport.offset.x) / viewport.scale - offsetX,
                  (raw.y - viewport.offset.y) / viewport.scale - offsetY};
        bool captureRequested = false;
        FrontendIntent action;
        if (quit) debugInputs.clear();
        RemoteSceneFrame worldFrame;
        bool leaveGame = false;
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
                    if (native.storage.kind != OnlineStorageKind::Stash) panels.inventory.storage = {};
                    if (native.storage.kind != OnlineStorageKind::Cube) panels.inventory.cubeOpen = false;
                    if (native.storage.kind == OnlineStorageKind::Stash && !panels.inventory.storage) {
                        panels.inventory.storage = sharedUi->inventoryView().containers.stash; panels.inventory.open = true;
                        panels.characterOpen = panels.questOpen = panels.hirelingOpen = false;
                    }
                    if (native.storage.kind == OnlineStorageKind::Cube) { panels.inventory.cubeOpen = true; panels.inventory.open = true; }
                    if (native.waypointSource.has_value() != displayedWaypointKnown ||
                        (native.waypointSource && displayedWaypointKnown &&
                         *native.waypointSource != displayedWaypoint)) {
                        displayedWaypointKnown = native.waypointSource.has_value();
                        displayedWaypoint = native.waypointSource.value_or(0);
                        panels.travelMenu = displayedWaypointKnown;
                        panels.waypointSource = displayedWaypointKnown ? EntityId{(uint64_t{1} << 32) + displayedWaypoint + 1} : EntityId{};
                        panels.waypointAct = view.load.act.value_or(0);
                        if (panels.travelMenu) { panels.inventory.open = false; panels.skillTreeOpen = panels.characterOpen = panels.questOpen = false; }
                    }
                    if (town.read().npcConversation) {
                        const auto &dialog = *town.read().npcConversation;
                        if (displayedNpc != dialog.source) {
                            displayedNpc = dialog.source;
                            sharedUi->openNpcMenu(EntityId{(uint64_t{1} << 32) + dialog.source + 1},dialog.speaker,false);
                        }
                        {
                            // 1.13c selects type 0 for automatic speech; type 2 is a
                            // selectable topic and must leave the service menu open.
                            // Acknowledging a message changes the acknowledged set,
                            // not necessarily the native conversation revision.
                            if (panels.dialogue.empty() && !panels.shopOpen && !panels.hireListOpen) for (const auto &message : dialog.messages)
                                if (!message.acknowledged && message.menu == 0 && !message.text.empty()) {
                                    sharedUi->openNpcDialogue(EntityId{(uint64_t{1} << 32) + dialog.source + 1},dialog.speaker,message.text);
                                    panels.dialogueTextTopic = message.stringId;
                                    break;
                                }
                        }
                    } else if (displayedNpc && !native.npcRequested) {
                        displayedNpc.reset(); sharedUi->cancelNpcDialogue();
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
                        sharedController->discardBufferedInput(input.focused);
                    }
                    if (!debugInputs.empty()) {
                        input = std::move(debugInputs.front());
                        debugInputs.pop_front();
                    }
                    captureRequested = input.screenshot;
                    if (localConnection && input.focused && !panels.gameMenuOpen && (input.save || input.load)) {
                        try {
                            const auto state = embedded.diagnostics();
                            if (!state.player) throw std::runtime_error("No active host character");
                            const auto result = administerLocal({input.load ? hosting::AdminOperation::Reload : hosting::AdminOperation::Save,
                                *state.player, {}});
                            sharedUi->notice(result.message, !result.applied());
                        } catch (const std::exception &e) { sharedUi->notice(e.what(), true); }
                        input.save = input.load = false;
                    }
                    chatInput(input);
                    const bool keepGame = sharedController->handle(input,GetFrameTime());
                    if (!keepGame) leaveGame = true;
                    if (auto feedback = sharedClients->takeNotice(); !feedback.text.empty()) sharedUi->notice(std::move(feedback.text),feedback.error);
                    mapDisplay.visible = panels.automap; mapDisplay.large = panels.automapLarge;
                    mapDisplay.right = panels.minimapRight; mapDisplay.offset = panels.automapOffset; mapDisplay.running = sharedClients->running();
                    worldFrame = scene->frame(view,*town.map(),town.read(),*sharedUi,combat,
                        sharedController->uiConsumed(),input.mouse,input.rightHeld || input.rightPressed);
                    sharedController->handleWorld(input,worldFrame.input,GetFrameTime());
                    if (auto feedback = sharedClients->takeNotice(); !feedback.text.empty()) sharedUi->notice(std::move(feedback.text),feedback.error);
                    sharedUi->drawUi(input.mouse);
                } catch (const std::exception &e) {
                    sceneError = e.what();
                    showScene = false;
                }
            }
            if (!showScene) {
                auto input = pollInput(viewport);
                if (!debugInputs.empty()) { input = std::move(debugInputs.front()); debugInputs.pop_front(); }
                captureRequested = input.screenshot;
                mouse = {input.mouse.x - offsetX, input.mouse.y - offsetY};
                ClearBackground(BLACK);
                if (view.stage == OnlineStage::ProtocolReady && sharedClients && sharedUi && sharedController) {
                    sharedClients->update(town.read());
                    sharedUi->refreshUi(std::clamp(GetFrameTime(), 0.f, .1f));
                    chatInput(input);
                    if (!sharedController->handle(input, GetFrameTime())) leaveGame = true;
                    WorldInputView unavailable;
                    unavailable.gameGeneration = view.gameGeneration;
                    unavailable.areaGeneration = view.world.areaGeneration;
                    sharedController->handleWorld(input, unavailable, GetFrameTime());
                    sharedUi->drawUi(worldMouse);
                } else {
                    BeginScissorMode(int(offsetX), int(offsetY), 800, 600);
                    rlPushMatrix();
                    rlTranslatef(offsetX, offsetY, 0);
                    action = ui.frame(page, session.read(), gateway, notice, mouse, input,
                                      !localConnection && !lanConnection, sceneStatus().reason);
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
        town.revealVisibleTiles(session.read(), worldFrame.visibleMapTiles);
        if (leaveGame) session.leave_game();
        if (action.editedAccount && !loginMemory.write(*action.editedAccount, {}))
            notice = "Unable to remember the edited account name.";
        if (action.command != FrontendCommand::None) quickCharacter = quickGame = false;
        switch (action.command) {
        case FrontendCommand::TcpIp:
            try {
                embedded.close();
                session.logout();
                localConnection = enterLocalGame = lanConnection = false;
                notice.clear(); page = FrontendPage::TcpIp;
                ui.setLanAddresses({});
                std::string addresses;
                if (lanListening && !options.hostLan.empty() && options.hostLan != "0.0.0.0") addresses = options.hostLan;
                else for (const auto &address : net::localIpv4Addresses()) {
                    if (!addresses.empty()) addresses += ", ";
                    addresses += address;
                }
                ui.setLanAddresses(std::move(addresses));
            } catch (const std::exception &e) { notice = e.what(); }
            break;
        case FrontendCommand::OpenJoinHost:
            notice.clear(); page = FrontendPage::JoinHost;
            break;
        case FrontendCommand::SinglePlayer:
        case FrontendCommand::HostLan:
            try {
                const bool hosting = action.command == FrontendCommand::HostLan;
                if (hosting && !lanListening) {
                    embedded.listen("0.0.0.0", options.realmPort, options.gamePort);
                    lanListening = true;
                }
                session.logout();
                auto streams = embedded.connect(true);
                session.connect_realm(std::move(streams.realm), std::move(streams.game),
                    {"127.0.0.1", options.realmPort}, hosting ? "TCP/IP Game" : "Single Player", options.gamePort);
                localConnection = true; enterLocalGame = false; localDifficulty = 0;
                lanConnection = hosting;
                reloadCharacter.clear();
                debugInputs.clear(); notice.clear(); page = FrontendPage::Characters;
            } catch (const std::exception &e) { notice = e.what(); }
            break;
        case FrontendCommand::JoinLan:
            try {
                embedded.close();
                session.logout();
                localConnection = enterLocalGame = false; lanConnection = true;
                reloadCharacter.clear(); debugInputs.clear(); notice.clear();
                session.connect_realm(std::make_unique<net::TcpStream>(), std::make_unique<net::TcpStream>(),
                    {std::move(action.name), options.realmPort}, "TCP/IP Game", options.gamePort);
                page = FrontendPage::Characters;
            } catch (const std::exception &e) { notice = e.what(); }
            break;
        case FrontendCommand::Exit:
            quit = true;
            ui.clearPassword();
            break;
        case FrontendCommand::Online:
            try { embedded.close(); }
            catch (const std::exception &e) { notice = e.what(); break; }
            localConnection = enterLocalGame = lanConnection = false;
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
            if (localConnection || lanConnection) {
                try { if (localConnection) embedded.close(); }
                catch (const std::exception &e) { notice = e.what(); break; }
                session.logout();
                page = lanConnection ? FrontendPage::TcpIp : FrontendPage::Main;
                localConnection = enterLocalGame = lanConnection = false;
                break;
            }
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
        case FrontendCommand::QueryGame:
            session.query_game(std::move(action.name));
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
            enterLocalGame = session.select_character(std::move(action.name)) && localConnection;
            break;
        case FrontendCommand::CreateGame:
            session.create_game({std::move(action.name), std::move(action.password),
                                 std::move(action.description), action.difficulty, localConnection && !lanConnection ? uint8_t(1) : action.maximumPlayers,
                                 action.levelDifference});
            break;
        case FrontendCommand::LeaveGame:
            if (!session.leave_game()) {
                session.cancel();
                page = lanConnection ? FrontendPage::TcpIp : localConnection ? FrontendPage::Main : FrontendPage::Login;
            }
            break;
        case FrontendCommand::Back:
            ui.clearTransientPasswords();
            notice.clear();
            if (page == FrontendPage::JoinHost) {
                page = FrontendPage::TcpIp;
            }
            else if (page == FrontendPage::TcpIp) {
                page = FrontendPage::Main;
            }
            else if (page == FrontendPage::Register && session.read().stage == OnlineStage::Idle) {
                std::string account, password;
                loginMemory.read(account, password);
                ui.setLogin(std::move(account), std::move(password));
                page = FrontendPage::Login;
            }
            else if (page == FrontendPage::CreateCharacter &&
                     session.read().stage == OnlineStage::CharacterSelection)
                page = FrontendPage::Characters;
            else if (page == FrontendPage::Lobby && session.read().stage == OnlineStage::ListingGames) {
                session.cancel_game_list();
                session.return_to_characters();
            }
            else if (session.read().stage == OnlineStage::Lobby)
                session.return_to_characters();
            else {
                try { if (localConnection) embedded.close(); }
                catch (const std::exception &e) { notice = e.what(); break; }
                session.logout();
                page = lanConnection ? FrontendPage::TcpIp :
                    localConnection || page == FrontendPage::Login ? FrontendPage::Main : FrontendPage::Login;
                localConnection = enterLocalGame = lanConnection = false;
            }
            break;
        case FrontendCommand::Dismiss:
            notice.clear();
            dismissedErrorSequence = session.read().error ? session.read().error->sequence
                : std::numeric_limits<uint64_t>::max();
            if (session.read().stage == OnlineStage::Failed) {
                session.logout();
                if (page != FrontendPage::Register)
                    page = lanConnection ? (localConnection ? FrontendPage::TcpIp : FrontendPage::JoinHost) :
                        localConnection ? FrontendPage::Main : FrontendPage::Login;
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
