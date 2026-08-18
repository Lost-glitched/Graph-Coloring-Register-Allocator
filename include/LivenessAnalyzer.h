#pragma once
// ============================================================================
// LivenessAnalyzer.h — Backward data-flow liveness analysis
// ============================================================================
// Computes block-level USE/DEF/LIVE_IN/LIVE_OUT via iterative fixed-point,
// then derives instruction-level live sets by backward scanning within each
// block.
// ============================================================================

#include "ControlFlowGraph.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

namespace regalloc {

/// Per-instruction liveness information.
struct InstrLiveness {
    std::unordered_set<std::string> liveIn;
    std::unordered_set<std::string> liveOut;
};

/// Per-block summary sets.
struct BlockSummary {
    std::unordered_set<std::string> use;     // upward-exposed uses
    std::unordered_set<std::string> def;     // definitions (kills)
    std::unordered_set<std::string> liveIn;
    std::unordered_set<std::string> liveOut;
};

class LivenessAnalyzer {
public:
    /// Run liveness analysis over the given CFG.
    void analyze(const ControlFlowGraph& cfg);

    /// Get block-level summary for block with id `blockId`.
    const BlockSummary& getBlockSummary(int blockId) const;

    /// Get instruction-level liveness. The key is the global instruction
    /// index (flattened across all blocks, in program order).
    const InstrLiveness& getInstrLiveness(size_t globalIndex) const;

    /// Total number of instruction-level entries.
    size_t instrCount() const { return instrLiveness_.size(); }

    /// Pretty-print all liveness information.
    std::string toString() const;

private:
    /// Compute USE and DEF sets for each block.
    void computeBlockSets(const ControlFlowGraph& cfg);

    /// Iterate fixed-point equations for LIVE_IN / LIVE_OUT.
    void computeFixedPoint(const ControlFlowGraph& cfg);

    /// Derive instruction-level live sets.
    void computeInstrLevel(const ControlFlowGraph& cfg);

    std::unordered_map<int, BlockSummary> blockSummaries_;
    std::vector<InstrLiveness> instrLiveness_;
};

} // namespace regalloc
