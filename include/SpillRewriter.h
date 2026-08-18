#pragma once
// ============================================================================
// SpillRewriter.h — IR rewriting for spilled variables
// ============================================================================
// For every actually-spilled variable:
//   1. Assign a unique stack-frame slot.
//   2. After every definition, insert: STORE temp, [slot]
//   3. Before every use, insert:       LOAD temp, [slot]
//   4. Replace the spilled variable with a fresh, short-lived temporary.
//
// Each inserted temporary has a unique name (e.g., "x.spill.0") so it has
// a very short live range and is likely to be colorable.
// ============================================================================

#include "IRProgram.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace regalloc {

class SpillRewriter {
public:
    /// Rewrite the IR program, inserting LOAD/STORE for every spilled variable.
    /// Returns the set of newly-created temporaries.
    std::unordered_set<std::string> rewrite(
        IRProgram& program,
        const std::unordered_set<std::string>& spilledVars,
        int& nextSlot,
        int& tempCounter,
        std::unordered_map<std::string, int>& varToSlot);

private:
    /// Generate a fresh temporary name derived from `base`.
    std::string freshTemp(const std::string& base, int& tempCounter);
};

} // namespace regalloc
