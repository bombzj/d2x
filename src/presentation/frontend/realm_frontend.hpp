#pragma once
#include "contracts/online.hpp"
#include "resources/archive.hpp"
#include <memory>
#include <raylib.h>

namespace d2x {
enum class FrontendPage { Main, Login, Register, Realms, Characters, CreateCharacter, Lobby, Loading };
enum class FrontendCommand {
    None,
    Exit,
    Offline,
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
    uint8_t maximumPlayers{4}, levelDifference{4};
    uint8_t difficulty{}, characterClass{};
    bool hardcore{};
};
// Original MPQ art and input only. The application owns authentication and sockets.
class RealmFrontend {
  public:
    explicit RealmFrontend(Archives &);
    ~RealmFrontend();
    RealmFrontend(const RealmFrontend &) = delete;
    RealmFrontend &operator=(const RealmFrontend &) = delete;
    FrontendIntent frame(FrontendPage, const OnlineView &, std::string_view gateway, std::string_view notice,
                         Vector2 mouse);
    void clearPassword();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace d2x
