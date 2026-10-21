#pragma once
#include "contracts/online.hpp"
#include "content/string_table.hpp"
#include "content/character/character_attributes.hpp"
#include "resources/data_table.hpp"
#include "network/realm_session.hpp"
#include <map>

namespace d2x {
struct EquipmentDefinition;
class RemoteInventory {
    std::map<std::string, DataTable, std::less<>> tables_;
    std::vector<CharacterDefinition> characters_;
    struct Base { std::string table; size_t row{}; };
    std::map<std::string, Base, std::less<>> bases_;
    std::map<std::string, size_t, std::less<>> typeRows_;
    std::map<unsigned, size_t> statRows_;
    const net::protocol::ClientProfile profile_;
    ClassicStrings strings_;
    OnlineInventoryView view_;
    OnlineStashLayout stashLayout_;
    uint64_t sourceRevision_{~uint64_t{}}, generation_{~uint64_t{}};
    std::optional<uint32_t> player_;
    std::optional<uint16_t> playerClass_;
    std::string reason_;
    OnlineDecodedItem decode(const OnlineItem &) const;
    bool isType(const OnlineItem &, std::string_view) const;
    int baseNumber(const OnlineItem &, std::string_view) const;
    EquipmentDefinition equipmentDefinition(const OnlineItem &) const;
    bool reject(std::string);
  public:
    explicit RemoteInventory(Archives &, net::protocol::ClientProfile = net::protocol::ClientProfile::Lod113c);
    void update(const OnlineView &);
    const OnlineInventoryView &read() const { return view_; }
    OnlineStashLayout stashLayout() const { return stashLayout_; }
    const std::string &reason() const { return reason_; }
    bool submit(net::RealmSession &, OnlineItemCommand);
};
} // namespace d2x
