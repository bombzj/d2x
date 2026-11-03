#pragma once
#include "contracts.hpp"
#include "domain_fact.hpp"
#include <vector>

namespace d2x::server {
enum class AudienceKind { Player, Area, Game };
struct Audience { AudienceKind kind; PlayerId player; RegionId area; };
struct EventBatch {
    uint64_t sequence{}, tick{};
    TransactionId transaction;
    Audience audience;
    std::vector<DomainFact> facts;
};
}
