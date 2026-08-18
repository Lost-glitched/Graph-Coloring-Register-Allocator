#pragma once
// ============================================================================
// Coalescer.h — Conservative register coalescing (Briggs' criterion)
// ============================================================================
// Two move-related nodes a and b can be safely coalesced if the merged node
// (a,b) would have fewer than K neighbors of significant degree (>= K).
//
// "Significant degree" means the neighbor's degree in the *merged* graph
// would be >= K.
// ============================================================================

#include "InterferenceGraph.h"
#include "MoveGraph.h"
#include "IRProgram.h"
#include <string>
#include <vector>
#include <utility>

namespace regalloc {

struct CoalesceResult {
    std::string a;
    std::string b;
    bool coalesced;
    std::string reason;      // empty if coalesced, explanation if rejected
};

class Coalescer {
public:
    /// Attempt to coalesce move-related pairs using Briggs' criterion.
    /// Returns the list of coalescing decisions made.
    ///
    /// Side effects: modifies `ig` (merges nodes), `program` (renames
    /// variables and removes redundant MOVs), and `moves` (clears consumed moves).
    std::vector<CoalesceResult> coalesce(
        InterferenceGraph& ig,
        MoveGraph& moves,
        IRProgram& program,
        int K);

private:
    /// Check Briggs' criterion for merging a and b.
    /// Returns true if the merged node would have < K significant-degree neighbors.
    bool briggsSafe(const InterferenceGraph& ig,
                    const std::string& a,
                    const std::string& b,
                    int K) const;
};

} // namespace regalloc
