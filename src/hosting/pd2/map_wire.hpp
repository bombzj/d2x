#pragma once
#include "network/protocol/wire.hpp"
namespace d2x::server {struct Pd2MapAttributesFact;}
namespace d2x::hosting {
Bytes pd2MapAttributes(const server::Pd2MapAttributesFact &);
}
