#pragma once
// ============================================================================
// IRProgram.h — Container for a complete IR program
// ============================================================================

#include "Instruction.h"
#include <vector>
#include <string>
#include <set>

namespace regalloc {

class IRProgram {
public:
    std::vector<Instruction> instructions;

    /// Add an instruction to the end.
    void addInstruction(const Instruction& instr);

    /// Insert an instruction at position `pos`.
    void insertAt(size_t pos, const Instruction& instr);

    /// Remove the instruction at position `pos`.
    void removeAt(size_t pos);

    /// Collect every variable name that appears as a def or use.
    std::set<std::string> allVariables() const;

    /// Replace every occurrence of variable `oldName` with `newName`.
    void renameVariable(const std::string& oldName, const std::string& newName);

    /// Deep copy.
    IRProgram clone() const;

    /// Pretty-print every instruction.
    std::string toString() const;
};

} // namespace regalloc
