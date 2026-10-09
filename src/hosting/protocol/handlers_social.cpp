#include "gameplay_dispatch.hpp"

namespace d2x::hosting::handlers {
RequestResult PlayerRelation(GameplayContext &context, net::protocol::Reader &in) {
    const auto action=in.u8(),enabled=in.u8(); const auto target=in.u32(); in.finish();
    if(action==2 || action==3) {
        if(enabled>1) return {RequestStatus::Rejected};
        for (const auto id : context.host.participants(context.player.game)) {
            const auto view=context.host.read({context.player.game,id});
            if(view && view->actor.id==EntityId{target}) {
                server::social::Request request{action==2?server::social::Action::Ignore:server::social::Action::Squelch,id,{}};
                request.enabled=enabled!=0; return submitGameplay(context,std::move(request));
            }
        }
        return {RequestStatus::Rejected};
    }
    return {RequestStatus::NotImplemented, CommandStatus::NotImplemented, "PlayerRelation"};
}
RequestResult PartyAction(GameplayContext &, net::protocol::Reader &in) {
    in.u8(); in.u32(); in.finish();
    return {RequestStatus::NotImplemented, CommandStatus::NotImplemented, "PartyAction"};
}
RequestResult CancelTrade(GameplayContext &context, uint32_t amount) {
    if(amount) return {RequestStatus::Rejected};
    return submitGameplay(context,server::trade::Request{server::trade::Action::Cancel,{}});
}
RequestResult AcceptTrade(GameplayContext &context, uint32_t amount) {
    if(amount) return {RequestStatus::Rejected};
    return submitGameplay(context,server::trade::Request{server::trade::Action::Accept,{}});
}
RequestResult AgreeTrade(GameplayContext &context, uint32_t amount) {
    if(amount) return {RequestStatus::Rejected};
    return submitGameplay(context,server::trade::Request{server::trade::Action::Agree,{}});
}
RequestResult ResetTrade(GameplayContext &context, uint32_t amount) {
    if(amount) return {RequestStatus::Rejected};
    return submitGameplay(context,server::trade::Request{server::trade::Action::Revoke,{}});
}
RequestResult OfferTradeGold(GameplayContext &context, uint32_t amount) {
    if(amount>INT32_MAX) return {RequestStatus::Rejected};
    return submitGameplay(context,server::trade::Request{server::trade::Action::OfferGold,{},0,amount});
}
RequestResult Chat(GameplayContext &context, net::protocol::Reader &in) {
    const auto type = in.u8(), language = in.u8(); const auto text = in.string(255), receiver = in.string(15); const auto extension = in.u8(); in.take(extension); in.finish();
    if (type != 1 || language || extension) return {RequestStatus::Rejected};
    return submitGameplay(context, server::social::Request{server::social::Action::Chat, {}, text,receiver});
}
RequestResult OverheadChat(GameplayContext &context, net::protocol::Reader &in) {
    const auto type=in.u8(),language=in.u8(); const auto text=in.string(255),receiver=in.string(15);
    const auto extension=in.u8();in.take(extension);in.finish();
    if(type!=1 || language || !receiver.empty() || extension) return {RequestStatus::Rejected};
    return submitGameplay(context,server::social::Request{server::social::Action::Chat,{},text,{},true});
}
}
