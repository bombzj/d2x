#pragma once
#include "native_realm_host.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace d2x::hosting {
struct RealmCharacter {
    uint64_t connection{};
    uint32_t game{}, token{};
    uint8_t characterClass{};
    std::string name, account, realm;
    Bytes save, portrait, logon;
};
struct RealmGameOutput {
    enum class Kind { Packet, Save, Entered, Retired, RoomClosed, Failure };
    Kind kind{};
    uint64_t connection{}, request{};
    uint32_t game{}, level{}, characterClass{};
    Bytes bytes, portrait;
    std::string error;
};
class PvpgnGameThread {
    struct Impl;
    std::unique_ptr<Impl> impl_;
  public:
    PvpgnGameThread(const Archives &, std::filesystem::path, ContentPreparation);
    ~PvpgnGameThread();
    void reserve(HostedGame);
    void admit(RealmCharacter);
    void packet(uint64_t connection, Bytes);
    void disconnect(uint64_t connection);
    void saved(uint64_t connection, uint64_t request, std::string error);
    void retireRoom(uint32_t game);
    void drain();
    std::vector<RealmGameOutput> take();
    std::vector<std::string> recover();
    void stop();
};
}