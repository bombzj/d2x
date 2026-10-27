#pragma once
#include "network/byte_transport.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace d2x::hosting {
struct RealmStorageRequest {
    enum class Kind { Load, Save, Unlock };
    Kind kind{};
    uint64_t connection{}, request{};
    std::string account, name, realm;
    Bytes save, portrait;
};
struct RealmStorageResult {
    RealmStorageRequest::Kind kind{};
    uint64_t connection{}, request{};
    bool locked{}, fatal{};
    Bytes save, portrait;
    std::string error;
};
class PvpgnStorage {
    struct Impl;
    std::unique_ptr<Impl> impl_;
  public:
    PvpgnStorage(net::Endpoint, std::filesystem::path);
    ~PvpgnStorage();
    void submit(RealmStorageRequest);
    std::vector<RealmStorageResult> take();
    bool ready() const;
    void stop();
};
}