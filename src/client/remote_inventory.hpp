#pragma once
#include "contracts/online.hpp"
#include "content/string_table.hpp"
#include "resources/data_table.hpp"
#include "network/realm_session.hpp"
#include <map>

namespace d2x {
class RemoteInventory {
    std::map<std::string, DataTable, std::less<>> tables_;
    struct Base { std::string table; size_t row{}; };
    std::map<std::string, Base, std::less<>> bases_;
    std::map<std::string, size_t, std::less<>> typeRows_;
    std::map<unsigned, size_t> statRows_;
    ClassicStrings strings_;
    OnlineInventoryView view_;
    uint64_t sourceRevision_{~uint64_t{}}, generation_{~uint64_t{}};
    std::optional<uint32_t> player_;
    std::optional<uint16_t> playerClass_;
    std::string reason_;
    OnlineDecodedItem decode(const OnlineItem &) const;
    bool isType(const OnlineItem &, std::string_view) const;
    int baseNumber(const OnlineItem &, std::string_view) const;
    bool reject(std::string);
  public:
    explicit RemoteInventory(Archives &);
    void update(const OnlineView &);
    const OnlineInventoryView &read() const { return view_; }
    const std::string &reason() const { return reason_; }
    bool submit(net::RealmSession &, OnlineItemCommand);
};
} // namespace d2x
