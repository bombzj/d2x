#pragma once
#include "network/protocol/client_profile.hpp"
#include <filesystem>
#include <string_view>

namespace d2x {
class Archives;
struct AppOptions;
namespace net::protocol { struct OriginalClientAuth; }
// Resolve before mounting resources. Files remain read-only; switching requires
// a fresh application/resource lifetime.
void resolveClientSetup(AppOptions &);
void mountClientResources(Archives &, const AppOptions &);
void verifyClientProfileDefinition(const std::filesystem::path &, std::string_view digest);
void preparePd2Authentication(net::protocol::OriginalClientAuth &, const std::filesystem::path &pd2,
                               const std::filesystem::path &original);
} // namespace d2x
