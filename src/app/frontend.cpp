#include "frontend.hpp"
#include "app/debug/debug_pipe.hpp"
#include "app/debug/online_commands.hpp"
#include "content/string_table.hpp"
#include "input.hpp"
#include "network/realm_session.hpp"
#include "presentation/frontend/realm_frontend.hpp"
#include "presentation/graphics/primitives.hpp"
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
    DebugPipe pipe(pipeName);
    bool quit = false, manualRealm = false;
    ClassicStrings strings(archives);
    FrontendPage page = FrontendPage::Main;
    std::string notice, gateway = "D2X-Local";
    uint64_t dismissedRevision = std::numeric_limits<uint64_t>::max();
    constexpr float scale = float(H) / 600, offsetX = (W - 800 * scale) / 2;
    while (!WindowShouldClose()) {
        const auto previousStage = session.read().stage;
        session.tick();
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
                    ui.clearPassword();
                    page = FrontendPage::Login;
                }
                if (session.read().stage == OnlineStage::Idle) {
                    ui.clearPassword();
                    page = FrontendPage::Main;
                }
            }
            return response;
        });
        if (quit)
            break;
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
        // There is no world consumer yet. Explicitly drain opaque packets while showing
        // the handoff boundary, rather than growing the queue or simulating a local game.
        session.take_game_packets();
        const auto viewport = currentViewport();
        const auto raw = GetMousePosition();
        Vector2 mouse{((raw.x - viewport.offset.x) / viewport.scale - offsetX) / scale,
                      (raw.y - viewport.offset.y) / viewport.scale / scale};
        BeginTextureMode(target);
        ClearBackground(BLACK);
        BeginScissorMode(int(offsetX), 0, int(800 * scale), H);
        rlPushMatrix();
        rlTranslatef(offsetX, 0, 0);
        rlScalef(scale, scale, 1);
        auto action = ui.frame(page, session.read(), gateway, notice, mouse);
        rlPopMatrix();
        EndScissorMode();
        EndTextureMode();
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(target.texture, {0, 0, float(W), -float(H)},
                       {viewport.offset.x, viewport.offset.y, W * viewport.scale, H * viewport.scale}, {0, 0},
                       0, WHITE);
        EndDrawing();
        switch (action.command) {
        case FrontendCommand::Exit:
            session.logout();
            ui.clearPassword();
            return std::nullopt;
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
                config.login.account = std::move(action.name);
                config.login.password = std::move(action.password);
                notice.clear();
                dismissedRevision = std::numeric_limits<uint64_t>::max();
                manualRealm = false;
                if (action.command == FrontendCommand::Register)
                    session.register_account(std::move(config.login));
                else
                    session.login(std::move(config.login));
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
            ui.clearPassword();
            notice.clear();
            if (page == FrontendPage::Register && session.read().stage == OnlineStage::Idle)
                page = FrontendPage::Login;
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
