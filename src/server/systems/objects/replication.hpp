#pragma once

namespace d2x::server { struct ObjectRule; }
namespace d2x::server::objects {
// Shared by initialization, reliable publication and the host's snapshot path.
// These existing units update through ObjectFact/0E, never a repeated 51.
bool usesReliableModeFacts(const ObjectRule &);
}
