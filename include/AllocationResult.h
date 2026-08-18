#pragma once
// ============================================================================
// AllocationResult.h — Final allocation map and output formatting
// ============================================================================

#include <string>
#include <unordered_map>
#include <vector>
#include <set>

namespace regalloc {

/// Represents the final register assignment for one variable.
struct Assignment {
    std::string variable;
    bool spilled{false};
    int physReg{-1};           // physical register index (0..K-1), or -1 if spilled
    int stackSlot{-1};         // stack slot index, or -1 if not spilled
};

class AllocationResult {
public:
    /// Map: variable → Assignment
    std::unordered_map<std::string, Assignment> assignments;

    /// List of stack slots allocated (for display).
    int totalStackSlots{0};

    /// Pretty-print the final allocation table.
    std::string toString(int K) const;
};

} // namespace regalloc
