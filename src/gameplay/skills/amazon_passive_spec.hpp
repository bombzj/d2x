#pragma once
namespace d2x {
enum class AmazonPassiveStat { Critical, Dodge, Avoid, Rating, Evade, Pierce };
struct AmazonPassiveSpec {
    AmazonPassiveStat stat = AmazonPassiveStat::Critical;
    int minimum = 0, maximum = 0, perLevel = 0;
    bool diminishing = true;
};
int amazonPassiveValue(const AmazonPassiveSpec &, int rank);
} // namespace d2x
