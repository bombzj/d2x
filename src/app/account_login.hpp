#pragma once
#include "network/protocol/client_profile.hpp"
#include <filesystem>
#include <string>
#include <string_view>

namespace d2x {
struct FrontendIntent;
struct Language;
class RealmFrontend;
class OnlineLoginMemory;
namespace net { class RealmSession; }
void submitAccountLogin(const std::filesystem::path &, net::protocol::ClientProfile,
                        const Language &, net::RealmSession &, RealmFrontend &,
                        OnlineLoginMemory &, FrontendIntent &, std::string &gateway,
                        std::string &notice, std::string_view profileDigest = {});
}
