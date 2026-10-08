#include "gameplay_dispatch.hpp"

namespace d2x::hosting::handlers {
RequestResult CloseStash(GameplayContext &context, uint32_t amount) {
    if(amount) return {RequestStatus::Rejected};
    return submitGameplay(context,server::inventory::Request{CloseStorage{}});
}
RequestResult WithdrawGold(GameplayContext &context, uint32_t amount) {
    return submitGameplay(context,server::inventory::Request{GoldTransaction{GoldAction::Withdraw,amount}});
}
RequestResult DepositGold(GameplayContext &context, uint32_t amount) {
    return submitGameplay(context,server::inventory::Request{GoldTransaction{GoldAction::Deposit,amount}});
}
RequestResult CloseCube(GameplayContext &context, uint32_t amount) {
    if(amount) return {RequestStatus::Rejected};
    return submitGameplay(context,server::inventory::Request{server::inventory::CloseCube{}});
}
RequestResult Transmute(GameplayContext &context, uint32_t amount) {
    if(amount) return {RequestStatus::Rejected};
    return submitGameplay(context, server::crafting::Request{TransmuteCube{}});
}
RequestResult PickUpItem(GameplayContext &context, net::protocol::Reader &reader) {
    const auto type = reader.u32(); const EntityId id{reader.u32()}; const auto cursor = reader.u32();
    if (type != 4 || cursor > 1) return {RequestStatus::Rejected};
    for (const auto &item : context.host.groundItems(context.player))
        if (item.id == id) return submitGameplay(context, server::inventory::Request{server::inventory::GroundTransfer{item.handle(), false, cursor != 0}});
    return {RequestStatus::Rejected};
}
RequestResult DropItem(GameplayContext &context, net::protocol::Reader &reader) {
    const EntityId id{reader.u32()}; const auto input = context.host.inventoryInput(context.player);
    if (!input || !input->items.contains(id)) return {RequestStatus::Rejected};
    return submitGameplay(context, server::inventory::Request{server::inventory::GroundTransfer{input->items.at(id).handle, true, false}});
}
namespace {
RequestResult consume(GameplayContext &context, net::protocol::Reader &reader, bool belt) {
    const EntityId id{reader.u32()}; const auto first = reader.u32(), second = reader.u32();
    if (belt && (first || second)) return {RequestStatus::NotImplemented};
    if (!belt && (first > UINT16_MAX || second > UINT16_MAX)) return {RequestStatus::Rejected};
    const auto input = context.host.inventoryInput(context.player);
    if (!input || !input->items.contains(id)) return {RequestStatus::Rejected};
    return submitGameplay(context, server::inventory::Request{d2x::UseItem{input->items.at(id).handle},
        belt ? server::inventory::Source::Belt : server::inventory::Source::Stored});
}
}
RequestResult UseItem(GameplayContext &context, net::protocol::Reader &reader) { return consume(context, reader, false); }
RequestResult UseBeltItem(GameplayContext &context, net::protocol::Reader &reader) { return consume(context, reader, true); }
RequestResult IdentifyItem(GameplayContext &context, net::protocol::Reader &in) {
    const EntityId target{in.u32()}, source{in.u32()}; in.finish();
    const auto inventory = context.host.inventoryInput(context.player);
    if (!inventory || !inventory->items.contains(source) || !inventory->items.contains(target)) return {RequestStatus::Rejected};
    return submitGameplay(context, server::inventory::Request{d2x::IdentifyItem{inventory->items.at(source).handle, inventory->items.at(target).handle}});
}
RequestResult SocketItem(GameplayContext &context, net::protocol::Reader &in) {
    const EntityId filler{in.u32()}, host{in.u32()}; in.finish();
    const auto input=context.host.inventoryInput(context.player);
    if(!input || !input->items.contains(filler) || !input->items.contains(host)) return {RequestStatus::Rejected};
    return submitGameplay(context,server::crafting::Request{d2x::SocketItem{input->items.at(filler).handle,input->items.at(host).handle}});
}

RequestResult DropGold(GameplayContext &context, net::protocol::Reader &in) {
    const EntityId owner{in.u32()}; const auto amount = in.u32(); in.finish();
    const auto player = context.host.read(context.player);
    if (!player || player->actor.id != owner || !amount || amount > INT32_MAX) return {RequestStatus::Rejected};
    return submitGameplay(context, server::inventory::Request{GoldTransaction{GoldAction::Drop, amount}});
}
}
