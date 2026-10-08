#pragma once
#include "contracts/online.hpp"
#include "resources/archive.hpp"
#include "presentation/input.hpp"
#include <memory>
#include <optional>
#include <raylib.h>

namespace d2x {
enum class FrontendPage { Main, Login, Register, Realms, Characters, CreateCharacter, Lobby, Loading, TcpIp, JoinHost };
enum class FrontendCommand {
    None,
    Exit,
    SinglePlayer,
    TcpIp,
    OpenJoinHost,
    HostLan,
    JoinLan,
    Online,
    Login,
    OpenRegister,
    Register,
    ChangeRealm,
    SelectRealm,
    OpenCreateCharacter,
    CreateCharacter,
    DeleteCharacter,
    ListGames,
    QueryGame,
    CancelList,
    JoinGame,
    Back,
    SelectCharacter,
    CreateGame,
    LeaveGame,
    Dismiss
};
struct FrontendIntent {
    FrontendCommand command{};
    std::string name, password, description;
    uint8_t maximumPlayers{8}, levelDifference{99};
    uint8_t difficulty{}, characterClass{};
    bool hardcore{};
    std::optional<std::string> editedAccount;
};
// Original MPQ art and input only. The application owns authentication and sockets.
class RealmFrontend {
  public:
    explicit RealmFrontend(Archives &);
    ~RealmFrontend();
    RealmFrontend(const RealmFrontend &) = delete;
    RealmFrontend &operator=(const RealmFrontend &) = delete;
    FrontendIntent frame(FrontendPage, const OnlineView &, std::string_view gateway, std::string_view notice,
                         Vector2 mouse, const FrameInput &, bool allowRealmSelection, std::string_view worldNotice = {});
    void clearPassword();
    void clearTransientPasswords();
    void setLogin(std::string account, std::string password);
    void setLanAddresses(std::string addresses);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace d2x
