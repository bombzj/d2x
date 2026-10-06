#pragma once
#include "client/remote_town.hpp"
#include "network/realm_session.hpp"
#include "resources/data_table.hpp"
#include <map>

namespace d2x {
struct OnlineCombatSkillView {
    uint16_t id{}, base{}, bonus{}, level{};
    std::string name;
    bool left{}, passive{}, inTown{}, classSkill{}, innate{};
};
struct OnlineCombatStateView {
    uint8_t id{};
    std::string name;
    std::vector<OnlineItemStat> stats;
};
struct OnlineCombatUnitStates {
    uint64_t sequence{};
    bool decoded{};
    std::string reason;
    std::map<uint8_t, OnlineCombatStateView> states;
};
// MPQ eligibility and read-only projection of native states. No local combat authority.
class RemoteCombat {
    RemoteTown &scene_;
    net::RealmSession &session_;
    std::map<std::string, DataTable, std::less<>> tables_;
    std::map<uint16_t, size_t> skills_, stats_, monsters_;
    std::map<uint8_t, size_t> states_;
    std::vector<OnlineCombatSkillView> catalog_;
    std::map<OnlineUnitKey, OnlineCombatUnitStates> unitStates_;
    uint64_t game_{~uint64_t{}}, area_{~uint64_t{}};
    std::string reason_;
    bool reject(std::string);
    std::string_view classCode() const;
  public:
    RemoteCombat(Archives &, RemoteTown &, net::RealmSession &);
    bool submit(OnlineCombatCommand);
    void update();
    const auto &skills() const { return catalog_; }
    const auto &states() const { return unitStates_; }
    const auto &reason() const { return reason_; }
};
}
