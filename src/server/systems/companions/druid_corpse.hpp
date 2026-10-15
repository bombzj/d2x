#pragma once
#include "server/runtime/contracts.hpp"
namespace d2x::server {class AreaStore;class PlayerStore;namespace monsters {class System;}namespace social {class System;}}
namespace d2x::server::companions {
EntityId findDruidCorpse(const AreaStore &,const PlayerStore &,const monsters::System &,const social::System &,EntityId,int radius,uint64_t tick);
}
