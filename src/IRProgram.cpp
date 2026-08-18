// ============================================================================
// IRProgram.cpp — IR program implementation
// ============================================================================

#include "IRProgram.h"
#include <sstream>

namespace regalloc {

void IRProgram::addInstruction(const Instruction& instr) {
    instructions.push_back(instr);
}

void IRProgram::insertAt(size_t pos, const Instruction& instr) {
    if (pos > instructions.size()) pos = instructions.size();
    instructions.insert(instructions.begin() + static_cast<long>(pos), instr);
}

void IRProgram::removeAt(size_t pos) {
    if (pos < instructions.size()) {
        instructions.erase(instructions.begin() + static_cast<long>(pos));
    }
}

std::set<std::string> IRProgram::allVariables() const {
    std::set<std::string> vars;
    for (auto& instr : instructions) {
        for (auto& d : instr.getDefs()) vars.insert(d);
        for (auto& u : instr.getUses()) vars.insert(u);
    }
    return vars;
}

void IRProgram::renameVariable(const std::string& oldName, const std::string& newName) {
    for (auto& instr : instructions) {
        if (instr.dest == oldName) instr.dest = newName;
        if (instr.src1 == oldName) instr.src1 = newName;
        if (instr.src2 == oldName) instr.src2 = newName;
    }
}

IRProgram IRProgram::clone() const {
    IRProgram copy;
    copy.instructions = instructions;
    return copy;
}

std::string IRProgram::toString() const {
    std::ostringstream os;
    for (size_t i = 0; i < instructions.size(); ++i) {
        os << "  " << i << ": " << instructions[i].toString() << "\n";
    }
    return os.str();
}

} // namespace regalloc
