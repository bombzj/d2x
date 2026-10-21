#pragma once
#include "core/bytes.hpp"
#include <string>

namespace d2x::net::protocol {
// Native 1.13c Bnclient key/owner resource envelope. No DLL is loaded or called.
// Consumes and erases the caller's encrypted buffer, including on failure.
std::string decode_native_auth_resource(Bytes &);
}
