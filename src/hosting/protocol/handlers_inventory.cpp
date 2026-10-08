#include "gameplay_dispatch.hpp"

namespace d2x::hosting::handlers {
RequestResult CloseStash(GameplayContext &, uint32_t) {
    return {RequestStatus::NotImplemented, CommandStatus::Stale, "CloseStash"};
}
RequestResult WithdrawGold(GameplayContext &, uint32_t) {
    return {RequestStatus::NotImplemented, CommandStatus::Stale, "WithdrawGold"};
}
RequestResult DepositGold(GameplayContext &, uint32_t) {
    return {RequestStatus::NotImplemented, CommandStatus::Stale, "DepositGold"};
}
RequestResult CloseCube(GameplayContext &, uint32_t) {
    return {RequestStatus::NotImplemented, CommandStatus::Stale, "CloseCube"};
}
RequestResult Transmute(GameplayContext &, uint32_t) {
    return {RequestStatus::NotImplemented, CommandStatus::Stale, "Transmute"};
}
RequestResult PickUpItem(GameplayContext &, net::protocol::Reader &) {
    // TODO: Inventory authority validation, transaction and native replication.
    return {RequestStatus::NotImplemented};
}
RequestResult DropItem(GameplayContext &, net::protocol::Reader &) {
    // TODO: Inventory authority validation, transaction and native replication.
    return {RequestStatus::NotImplemented};
}
RequestResult UseItem(GameplayContext &, net::protocol::Reader &) {
    // TODO: Inventory authority validation, transaction and native replication.
    return {RequestStatus::NotImplemented};
}

RequestResult UseBeltItem(GameplayContext &, net::protocol::Reader &) {
    // TODO: Inventory authority validation, transaction and native replication.
    return {RequestStatus::NotImplemented};
}
RequestResult IdentifyItem(GameplayContext &, net::protocol::Reader &) {
    // TODO: Inventory authority validation, transaction and native replication.
    return {RequestStatus::NotImplemented};
}
RequestResult SocketItem(GameplayContext &, net::protocol::Reader &) {
    // TODO: Inventory authority validation, transaction and native replication.
    return {RequestStatus::NotImplemented};
}

RequestResult DropGold(GameplayContext &, net::protocol::Reader &) {
    // TODO: Inventory authority validation, transaction and native replication.
    return {RequestStatus::NotImplemented};
}
}
