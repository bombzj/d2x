#include "gameplay_dispatch.hpp"

namespace d2x::hosting::handlers {
RequestResult CancelTrade(GameplayContext &, uint32_t) {
    return {RequestStatus::NotImplemented, CommandStatus::Stale, "CancelTrade"};
}
RequestResult AcceptTrade(GameplayContext &, uint32_t) {
    return {RequestStatus::NotImplemented, CommandStatus::Stale, "AcceptTrade"};
}
RequestResult AgreeTrade(GameplayContext &, uint32_t) {
    return {RequestStatus::NotImplemented, CommandStatus::Stale, "AgreeTrade"};
}
RequestResult ResetTrade(GameplayContext &, uint32_t) {
    return {RequestStatus::NotImplemented, CommandStatus::Stale, "ResetTrade"};
}
RequestResult OfferTradeGold(GameplayContext &, uint32_t) {
    return {RequestStatus::NotImplemented, CommandStatus::Stale, "OfferTradeGold"};
}
RequestResult Chat(GameplayContext &, net::protocol::Reader &) {
    // TODO: Social authority validation, transaction and native replication.
    return {RequestStatus::NotImplemented};
}
}
