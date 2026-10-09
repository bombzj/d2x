#pragma once
#include "message_catalog.hpp"
#include "hosting/game_host.hpp"

namespace d2x::hosting {
enum class RequestStatus { Queued, NotImplemented, Rejected };
struct RequestResult {
    RequestStatus status = RequestStatus::NotImplemented;
    CommandStatus command = CommandStatus::Stale;
    std::string_view operation{};
};
// Bound by the authenticated connection, never decoded from an untrusted GUID.
// No archives, storage, frontend or native transport are available to handlers.
struct GameplayContext {
    GameHost &host;
    PlayerBinding player;
    Vec origin;
    uint64_t &sequence;
};
RequestResult dispatchGameplay(std::span<const uint8_t>, GameplayContext &);
RequestResult submitGameplay(GameplayContext &, server::CommandPayload);
namespace handlers {
// Multiplexed native messages are decoded once, then routed to their domain.
RequestResult CancelTrade(GameplayContext &, uint32_t amount);
RequestResult AcceptTrade(GameplayContext &, uint32_t amount);
RequestResult AgreeTrade(GameplayContext &, uint32_t amount);
RequestResult ResetTrade(GameplayContext &, uint32_t amount);
RequestResult OfferTradeGold(GameplayContext &, uint32_t amount);
RequestResult CloseStash(GameplayContext &, uint32_t amount);
RequestResult WithdrawGold(GameplayContext &, uint32_t amount);
RequestResult DepositGold(GameplayContext &, uint32_t amount);
RequestResult CloseCube(GameplayContext &, uint32_t amount);
RequestResult Transmute(GameplayContext &, uint32_t amount);
RequestResult NpcTravel(GameplayContext &, uint32_t npc, uint32_t destination);
RequestResult OpenShop(GameplayContext &, uint32_t npc);
RequestResult OpenGambleShop(GameplayContext &, uint32_t npc);
RequestResult WalkPoint(GameplayContext &, net::protocol::Reader &);
RequestResult WalkUnit(GameplayContext &, net::protocol::Reader &);
RequestResult RunPoint(GameplayContext &, net::protocol::Reader &);
RequestResult RunUnit(GameplayContext &, net::protocol::Reader &);
RequestResult LeftPoint(GameplayContext &, net::protocol::Reader &);
RequestResult LeftUnit(GameplayContext &, net::protocol::Reader &);
RequestResult LeftUnitStill(GameplayContext &, net::protocol::Reader &);
RequestResult LeftPointRepeat(GameplayContext &, net::protocol::Reader &);
RequestResult LeftUnitRepeat(GameplayContext &, net::protocol::Reader &);
RequestResult LeftUnitRepeatStill(GameplayContext &, net::protocol::Reader &);
RequestResult RightPoint(GameplayContext &, net::protocol::Reader &);
RequestResult RightUnit(GameplayContext &, net::protocol::Reader &);
RequestResult RightUnitStill(GameplayContext &, net::protocol::Reader &);
RequestResult RightPointRepeat(GameplayContext &, net::protocol::Reader &);
RequestResult RightUnitRepeat(GameplayContext &, net::protocol::Reader &);
RequestResult RightUnitRepeatStill(GameplayContext &, net::protocol::Reader &);
RequestResult StopSkill(GameplayContext &, net::protocol::Reader &);
RequestResult InteractUnit(GameplayContext &, net::protocol::Reader &);
RequestResult Chat(GameplayContext &, net::protocol::Reader &);
RequestResult PickUpItem(GameplayContext &, net::protocol::Reader &);
RequestResult DropItem(GameplayContext &, net::protocol::Reader &);
RequestResult PlaceItem(GameplayContext &, net::protocol::Reader &);
RequestResult TakeItem(GameplayContext &, net::protocol::Reader &);
RequestResult EquipItem(GameplayContext &, net::protocol::Reader &);
RequestResult EquipItemIndirect(GameplayContext &, net::protocol::Reader &);
RequestResult UnequipItem(GameplayContext &, net::protocol::Reader &);
RequestResult SwapEquipment(GameplayContext &, net::protocol::Reader &);
RequestResult EquipTwoHanded(GameplayContext &, net::protocol::Reader &);
RequestResult SwapItem(GameplayContext &, net::protocol::Reader &);
RequestResult UseItem(GameplayContext &, net::protocol::Reader &);
RequestResult StackItem(GameplayContext &, net::protocol::Reader &);
RequestResult PlaceBeltItem(GameplayContext &, net::protocol::Reader &);
RequestResult TakeBeltItem(GameplayContext &, net::protocol::Reader &);
RequestResult SwapBeltItem(GameplayContext &, net::protocol::Reader &);
RequestResult UseBeltItem(GameplayContext &, net::protocol::Reader &);
RequestResult IdentifyItem(GameplayContext &, net::protocol::Reader &);
RequestResult SocketItem(GameplayContext &, net::protocol::Reader &);
RequestResult LoadBook(GameplayContext &, net::protocol::Reader &);
RequestResult InitializeNpc(GameplayContext &, net::protocol::Reader &);
RequestResult CloseNpc(GameplayContext &, net::protocol::Reader &);
RequestResult NpcMessage(GameplayContext &, net::protocol::Reader &);
RequestResult StaffUpdate(GameplayContext &, net::protocol::Reader &);
RequestResult BuyItem(GameplayContext &, net::protocol::Reader &);
RequestResult SellItem(GameplayContext &, net::protocol::Reader &);
RequestResult IdentifyAll(GameplayContext &, net::protocol::Reader &);
RequestResult RepairItems(GameplayContext &, net::protocol::Reader &);
RequestResult NpcService(GameplayContext &, net::protocol::Reader &);
RequestResult SpendAttribute(GameplayContext &, net::protocol::Reader &);
RequestResult LearnSkill(GameplayContext &, net::protocol::Reader &);
RequestResult SelectSkill(GameplayContext &, net::protocol::Reader &);
RequestResult RequestQuests(GameplayContext &, net::protocol::Reader &);
RequestResult Resurrect(GameplayContext &, net::protocol::Reader &);
RequestResult Waypoint(GameplayContext &, net::protocol::Reader &);
RequestResult UiAction(GameplayContext &, net::protocol::Reader &);
RequestResult DropGold(GameplayContext &, net::protocol::Reader &);
RequestResult BindHotkey(GameplayContext &, net::protocol::Reader &);
RequestResult SwitchWeapons(GameplayContext &, net::protocol::Reader &);
}
}
