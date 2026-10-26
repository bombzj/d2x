#pragma once
#include <filesystem>

namespace d2x {
class Archives;
// Shared PD2 resource mount for local play and CN frontend assets. No native
// DLL or account-authentication dependency is required by this mount contract.
void mountPd2HostArchives(Archives &,const std::filesystem::path &original,
                          const std::filesystem::path &pd2);
}
