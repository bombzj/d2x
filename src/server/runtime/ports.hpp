#pragma once
namespace d2x::server {
class AreaStore;
class PlayerStore;
class EventOutbox;
class MovementSystem;
#define D2X_SYSTEM(id, member, phase, scope) namespace member { class System; }
#include "subsystems.inc"
#undef D2X_SYSTEM
}
