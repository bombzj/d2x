#pragma once
#include <cstdint>

namespace d2x::server::spatial {
// Opt-in construction work, separate from the periodic spatial index. Values
// accumulate per instance; one host step attempt publishes the difference.
struct FootprintMetrics {
    uint64_t builds{},navigationBuilds{},unitChainBuilds{},actorFreeBuilds{};
    uint64_t actorVisits{},cellTests{},cells{},mergedCells{},nanoseconds{};
    uint64_t nativeUpdateNs{},nativeWrites{},nativeReads{},nativeMoveProbes{},nativeRollbacks{},nativeQueryScopes{};
    FootprintMetrics since(const FootprintMetrics &before) const {
        return {builds-before.builds,navigationBuilds-before.navigationBuilds,
            unitChainBuilds-before.unitChainBuilds,actorFreeBuilds-before.actorFreeBuilds,
            actorVisits-before.actorVisits,cellTests-before.cellTests,cells-before.cells,
            mergedCells-before.mergedCells,nanoseconds-before.nanoseconds,
            nativeUpdateNs-before.nativeUpdateNs,nativeWrites-before.nativeWrites,nativeReads-before.nativeReads,
            nativeMoveProbes-before.nativeMoveProbes,nativeRollbacks-before.nativeRollbacks,nativeQueryScopes-before.nativeQueryScopes};
    }
};
}
