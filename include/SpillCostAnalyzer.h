#pragma once
// ============================================================================
// SpillCostAnalyzer.h — Spill cost heuristic
// ============================================================================
// Heuristic:
//   spill_cost(v) = Σ (10^loop_depth for each use/def of v) / max(degree, 1)
//
// Lower cost → better spill candidate (less expensive to spill).
// Dividing by degree prefers nodes with many edges, since spilling a
// high-degree node removes more interference.
// ============================================================================

#include "IRProgram.h"
#include "ControlFlowGraph.h"
#include "InterferenceGraph.h"
#include <string>
#include <unordered_map>

namespace regalloc {

class SpillCostAnalyzer {
public:
    /// Compute spill costs for all variables in the program.
    /// Uses the CFG to estimate loop depth via back-edge detection.
    void compute(const IRProgram& program,
                 const ControlFlowGraph& cfg,
                 const InterferenceGraph& ig);

    /// Get the spill cost for a variable. Lower = better candidate.
    double cost(const std::string& var) const;

    /// Get all computed costs.
    const std::unordered_map<std::string, double>& allCosts() const { return costs_; }

private:
    /// Estimate loop depth for each instruction based on back-edges in the CFG.
    std::unordered_map<int, int> computeBlockDepths(const ControlFlowGraph& cfg) const;

    std::unordered_map<std::string, double> costs_;
};

} // namespace regalloc
