#pragma once
#include <filesystem>

namespace d2x {
class Archives;
// Resource-only PD2 profile for the self-hosted engine. No native DLL or
// account-authentication dependency is required by this mount contract.
void mountPd2HostArchives(Archives &,const std::filesystem::path &original,
                          const std::filesystem::path &pd2);
}
