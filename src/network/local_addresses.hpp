#pragma once
#include <string>
#include <vector>

namespace d2x::net {
// Enumerate active local IPv4 interfaces without DNS or external probes.
// Loopback is included; callers choose how to present the list.
std::vector<std::string> localIpv4Addresses();
}
