#include "../remote_combat.hpp"

namespace d2x {
bool RemoteCombat::pd2ChargeTargetEligible(const OnlineUnit &source,const OnlineUnit &target) const {
    // Client 2B680 uses FindUnits flags A283: players/monsters, live,
    // CANBEATTACKED and source-relative attack permission. Range, room and
    // terrain mask4 are checked by the movement consumer.
    if(source.key==target.key || source.key.type>1 || target.key.type>1 ||
       !source.position || !target.position || onlineMonsterCorpse(target) ||
       (target.key.type==0 && (session_.read().world.corpseOwners.contains(target.key.id) ||
        (target.nativeMode?(target.mode==0 || target.mode==17):(target.mode==8 || target.mode==9))))) return false;
    if(target.key.type==1) {
        const auto monster=monsters_.find(monsterClass(target).value_or(UINT16_MAX));
        if(monster==monsters_.end()) return false;
        const auto name=tables_.at("monstats").value(monster->second,"MonStatsEx");
        const auto &extra=tables_.at("monstats2");bool attackable=false;
        for(size_t row=0;row<extra.rows().size();++row) if(extra.value(row,"Id")==name) {
            attackable=extra.number(row,"isAtt").value_or(0)!=0;break;
        }
        if(!attackable) return false;
    }
    const auto first=playerOwner(source),second=playerOwner(target);
    if(first && second) return *first!=*second && hostilePlayers(*first,*second);
    const auto firstAlignment=alignment(source),secondAlignment=alignment(target);
    return firstAlignment && secondAlignment && !sameAlignment(*firstAlignment,*secondAlignment);
}
}
