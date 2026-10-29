#pragma once
namespace d2x {
class RemoteTown;
struct OnlineView;
// CN display policy, also selected by the PD2 local resource profile.
void updateAutomapRevealCn(RemoteTown &, const OnlineView &, bool enabled);
}
