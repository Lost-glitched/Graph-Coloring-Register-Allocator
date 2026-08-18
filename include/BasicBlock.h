#pragma once
// ============================================================================
// BasicBlock.h — Basic block representation for the CFG
// ============================================================================

#include "Instruction.h"
#include <vector>
#include <string>
#include <sstream>

namespace regalloc {

struct BasicBlock {
    int id{-1};
    std::vector<Instruction> instructions;
    std::vector<BasicBlock*> predecessors;
    std::vector<BasicBlock*> successors;

    /// Pretty-print block header, instructions, and edges.
    std::string toString() const {
        std::ostringstream os;
        os << "Block B" << id << ":\n";
        for (auto& instr : instructions) {
            os << "    " << instr.toString() << "\n";
        }
        os << "  Successors:";
        for (auto* s : successors) os << " B" << s->id;
        if (successors.empty()) os << " (none)";
        os << "\n  Predecessors:";
        for (auto* p : predecessors) os << " B" << p->id;
        if (predecessors.empty()) os << " (none)";
        os << "\n";
        return os.str();
    }
};

} // namespace regalloc
