// ============================================================================
// AllocationResult.cpp
// ============================================================================

#include "AllocationResult.h"
#include <sstream>
#include <algorithm>

namespace regalloc {

std::string AllocationResult::toString(int K) const {
    std::ostringstream os;
    os << "=== Final Register Allocation ===\n";
    os << "Physical registers: R0..R" << (K - 1) << "\n";
    os << "Stack slots used: " << totalStackSlots << "\n\n";
    os << "  Variable          Assignment\n";
    os << "  ----------------  ----------------\n";

    // Sort by variable name for deterministic output
    std::vector<std::string> vars;
    for (auto& [v, _] : assignments) vars.push_back(v);
    std::sort(vars.begin(), vars.end());

    for (auto& v : vars) {
        auto& a = assignments.at(v);
        os << "  ";
        // Left-pad variable name
        std::string vStr = v;
        if (vStr.size() < 18) vStr.resize(18, ' ');
        os << vStr;
        if (a.spilled) {
            os << "[stack_slot_" << a.stackSlot << "]";
        } else {
            os << "R" << a.physReg;
        }
        os << "\n";
    }
    return os.str();
}

} // namespace regalloc
