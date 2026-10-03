#include "session_impl.hpp"
#include "gameplay/npc/movement.hpp"
namespace d2x {
void GameSessionImpl::advanceNpcPaths(float dt) {
    d2x::advanceNpcPaths(world_.at(current_), pendingInteraction_, engagedNpc_, random_, dt);
}
} // namespace d2x
