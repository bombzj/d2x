#include "frontend_commands.hpp"
#include "app/account_login.hpp"
#include "network/local_addresses.hpp"
#include <exception>
#include <limits>

namespace d2x {
void dispatchFrontendCommand(FrontendDispatchState &state, FrontendIntent &action) {
    // Remote profiles cannot construct or dispatch a local host, even through UI commands.
    if (!state.embedded && (action.command == FrontendCommand::TcpIp ||
        action.command == FrontendCommand::OpenJoinHost || action.command == FrontendCommand::JoinLan || action.command == FrontendCommand::HostLan ||
        action.command == FrontendCommand::SinglePlayer)) {
        state.notice = "This profile supports remote Battle.net only.";
        return;
    }
    switch (action.command) {
    case FrontendCommand::TcpIp:
        try {
            state.embedded->close();
            state.session.logout();
            state.localConnection = state.enterLocalGame = state.lanConnection = false;
            state.notice.clear(); state.page = FrontendPage::TcpIp;
            state.ui.setLanAddresses({});
            std::string addresses;
            if (state.lanListening && !state.options.hostLan.empty() && state.options.hostLan != "0.0.0.0") addresses = state.options.hostLan;
            else for (const auto &address : net::localIpv4Addresses()) {
                if (!addresses.empty()) addresses += ", ";
                addresses += address;
            }
            state.ui.setLanAddresses(std::move(addresses));
        } catch (const std::exception &e) { state.notice = e.what(); }
        break;
    case FrontendCommand::OpenJoinHost:
        state.notice.clear(); state.page = FrontendPage::JoinHost;
        break;
    case FrontendCommand::SinglePlayer:
    case FrontendCommand::HostLan:
        try {
            const bool hosting = action.command == FrontendCommand::HostLan;
            if (hosting && !state.lanListening) {
                state.embedded->listen("0.0.0.0", state.options.gamePort);
                state.lanListening = true;
            }
            state.session.logout();
            state.embedded->setTcpIpHost(hosting);
            state.lanJoinAddress.clear(); state.lanJoiningGame = false;
            auto streams = state.embedded->connect(true);
            state.session.connect_realm(std::move(streams.realm), std::move(streams.game),
                {"127.0.0.1", state.options.realmPort}, hosting ? "TCP/IP Game" : "Single Player", state.options.gamePort);
            state.localConnection = true; state.enterLocalGame = false; state.localDifficulty = 0; state.localDifficultyChosen = false;
            state.lanConnection = hosting;
            state.reloadCharacter.clear();
            state.debugInputs.clear(); state.notice.clear(); state.page = FrontendPage::Characters;
        } catch (const std::exception &e) { state.notice = e.what(); }
        break;
    case FrontendCommand::JoinLan:
        try {
            state.embedded->close();
            state.session.logout();
            state.localConnection = state.lanConnection = true; state.enterLocalGame = false; state.lanJoiningGame = false;
            state.embedded->setTcpIpHost(false); state.lanJoinAddress = std::move(action.name);
            state.reloadCharacter.clear(); state.debugInputs.clear(); state.notice.clear();
            auto streams = state.embedded->connect();
            state.session.connect_realm(std::move(streams.realm), std::move(streams.game),
                {"127.0.0.1", state.options.realmPort}, "TCP/IP Game", state.options.gamePort);
            state.page = FrontendPage::Characters;
        } catch (const std::exception &e) { state.notice = e.what(); }
        break;
    case FrontendCommand::Exit:
        state.quit = true;
        state.ui.clearPassword();
        break;
    case FrontendCommand::Online:
        try { if (state.embedded) state.embedded->close(); }
        catch (const std::exception &e) { state.notice = e.what(); break; }
        state.localConnection = state.enterLocalGame = state.lanConnection = false;
        state.page = FrontendPage::Login;
        break;
    case FrontendCommand::OpenRegister:
        state.ui.clearPassword();
        state.page = FrontendPage::Register;
        break;
    case FrontendCommand::OpenCreateCharacter:
        state.page = FrontendPage::CreateCharacter;
        break;
    case FrontendCommand::ChangeRealm:
        if (state.localConnection || state.lanConnection) {
            try { if (state.localConnection) state.embedded->close(); }
            catch (const std::exception &e) { state.notice = e.what(); break; }
            state.session.logout();
            state.page = state.lanConnection ? FrontendPage::TcpIp : FrontendPage::Main;
            state.localConnection = state.enterLocalGame = state.lanConnection = false;
            break;
        }
        state.manualRealm = true;
        state.session.return_to_realms();
        break;
    case FrontendCommand::SelectRealm:
        if (state.session.choose_realm(action.name)) {
            state.gateway = action.name;
            state.page = FrontendPage::Characters;
        }
        break;
    case FrontendCommand::CreateCharacter:
        state.session.create_character({std::move(action.name), action.characterClass, action.hardcore});
        break;
    case FrontendCommand::DeleteCharacter:
        state.session.delete_character(std::move(action.name));
        break;
    case FrontendCommand::ListGames:
        state.session.list_games();
        break;
    case FrontendCommand::CancelList:
        state.session.cancel_game_list();
        break;
    case FrontendCommand::QueryGame:
        state.session.query_game(std::move(action.name));
        break;
    case FrontendCommand::JoinGame:
        state.session.join_game(std::move(action.name), std::move(action.password));
        break;
    case FrontendCommand::Register:
    case FrontendCommand::Login:
        try {
            state.dismissedErrorSequence = std::numeric_limits<uint64_t>::max();
            state.manualRealm = false;
            submitAccountLogin(state.configPath, state.options.protocolProfile, state.lang,
                               state.session, state.ui, state.loginMemory, action, state.gateway, state.notice,
                               state.options.clientProfileDigest);
        } catch (const std::exception &e) {
            state.notice = e.what();
        }
        break;
    case FrontendCommand::SelectCharacter:
        state.localDifficulty = 0; state.localDifficultyChosen = false;
        state.enterLocalGame = state.session.select_character(std::move(action.name)) && state.localConnection;
        break;
    case FrontendCommand::SelectDifficulty:
        state.localDifficulty = action.difficulty; state.localDifficultyChosen = true; state.page = FrontendPage::Loading;
        break;
    case FrontendCommand::CreateGame:
        state.session.create_game({std::move(action.name), std::move(action.password),
                             std::move(action.description), action.difficulty, state.localConnection && !state.lanConnection ? uint8_t(1) : action.maximumPlayers,
                             action.levelDifference});
        break;
    case FrontendCommand::LeaveGame:
        if (!state.session.leave_game()) {
            state.session.cancel();
            state.page = state.lanConnection ? FrontendPage::TcpIp : state.localConnection ? FrontendPage::Main : FrontendPage::Login;
        }
        break;
    case FrontendCommand::Back:
        state.ui.clearTransientPasswords();
        state.notice.clear();
        if (state.page == FrontendPage::Difficulty) {
            state.enterLocalGame = false; state.localDifficultyChosen = false;
            state.session.return_to_characters(); state.page = FrontendPage::Characters;
        }
        else if (state.page == FrontendPage::JoinHost) {
            state.page = FrontendPage::TcpIp;
        }
        else if (state.page == FrontendPage::TcpIp) {
            state.page = FrontendPage::Main;
        }
        else if (state.page == FrontendPage::Register && state.session.read().stage == OnlineStage::Idle) {
            std::string account, password;
            state.loginMemory.read(account, password);
            state.ui.setLogin(std::move(account), std::move(password));
            state.page = FrontendPage::Login;
        }
        else if (state.page == FrontendPage::CreateCharacter &&
                 state.session.read().stage == OnlineStage::CharacterSelection)
            state.page = FrontendPage::Characters;
        else if (state.page == FrontendPage::Lobby && state.session.read().stage == OnlineStage::ListingGames) {
            state.session.cancel_game_list();
            state.session.return_to_characters();
        }
        else if (state.session.read().stage == OnlineStage::Lobby)
            state.session.return_to_characters();
        else {
            try { if (state.localConnection) state.embedded->close(); }
            catch (const std::exception &e) { state.notice = e.what(); break; }
            state.session.logout();
            state.page = state.lanConnection ? FrontendPage::TcpIp :
                state.embedded && (state.localConnection || state.page == FrontendPage::Login) ? FrontendPage::Main : FrontendPage::Login;
            state.localConnection = state.enterLocalGame = state.lanConnection = false;
        }
        break;
    case FrontendCommand::Dismiss:
        state.tcpIpSaveFailed = false; // Explicit retry of a retained returned save.
        state.notice.clear();
        state.dismissedErrorSequence = state.session.read().error ? state.session.read().error->sequence
            : std::numeric_limits<uint64_t>::max();
        if (state.session.read().stage == OnlineStage::Failed) {
            state.session.logout();
            if (state.page != FrontendPage::Register)
                state.page = state.lanConnection ? (state.localConnection ? FrontendPage::TcpIp : FrontendPage::JoinHost) :
                    state.localConnection ? FrontendPage::Main : FrontendPage::Login;
        }
        break;
    case FrontendCommand::None:
        break;
    }
    net::protocol::erase_secret(action.password);
}
} // namespace d2x
