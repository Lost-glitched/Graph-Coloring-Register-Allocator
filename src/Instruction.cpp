// ============================================================================
// Instruction.cpp — IR instruction implementation
// ============================================================================

#include "Instruction.h"
#include <stdexcept>

namespace regalloc {

// ----------------------------------------------------------------------------
// opcodeToString
// ----------------------------------------------------------------------------
std::string opcodeToString(Opcode op) {
    switch (op) {
        case Opcode::MOV:    return "MOV";
        case Opcode::ADD:    return "ADD";
        case Opcode::SUB:    return "SUB";
        case Opcode::MUL:    return "MUL";
        case Opcode::DIV:    return "DIV";
        case Opcode::LOAD:   return "LOAD";
        case Opcode::STORE:  return "STORE";
        case Opcode::GOTO:   return "GOTO";
        case Opcode::BRANCH: return "BRANCH";
        case Opcode::LABEL:  return "LABEL";
    }
    return "UNKNOWN";
}

// ----------------------------------------------------------------------------
// getDefs — variables written by this instruction
// ----------------------------------------------------------------------------
std::vector<std::string> Instruction::getDefs() const {
    switch (opcode) {
        case Opcode::MOV:
        case Opcode::ADD:
        case Opcode::SUB:
        case Opcode::MUL:
        case Opcode::DIV:
        case Opcode::LOAD:
            if (!dest.empty()) return {dest};
            break;
        default:
            break;
    }
    return {};
}

// ----------------------------------------------------------------------------
// getUses — variables read by this instruction
// ----------------------------------------------------------------------------
std::vector<std::string> Instruction::getUses() const {
    std::vector<std::string> uses;
    switch (opcode) {
        case Opcode::MOV:
            if (!src1.empty()) uses.push_back(src1);
            break;
        case Opcode::ADD:
        case Opcode::SUB:
        case Opcode::MUL:
        case Opcode::DIV:
            if (!src1.empty()) uses.push_back(src1);
            if (!src2.empty()) uses.push_back(src2);
            break;
        case Opcode::STORE:
            if (!src1.empty()) uses.push_back(src1);
            break;
        case Opcode::BRANCH:
            if (!src1.empty()) uses.push_back(src1);
            break;
        default:
            break;
    }
    return uses;
}

bool Instruction::isMove()        const { return opcode == Opcode::MOV; }
bool Instruction::isControlFlow() const { return opcode == Opcode::GOTO || opcode == Opcode::BRANCH; }
bool Instruction::isLabel()       const { return opcode == Opcode::LABEL; }

// ----------------------------------------------------------------------------
// toString — human-readable instruction
// ----------------------------------------------------------------------------
std::string Instruction::toString() const {
    std::ostringstream os;
    switch (opcode) {
        case Opcode::MOV:
            os << dest << " = " << src1;
            break;
        case Opcode::ADD:
            os << dest << " = " << src1 << " + " << src2;
            break;
        case Opcode::SUB:
            os << dest << " = " << src1 << " - " << src2;
            break;
        case Opcode::MUL:
            os << dest << " = " << src1 << " * " << src2;
            break;
        case Opcode::DIV:
            os << dest << " = " << src1 << " / " << src2;
            break;
        case Opcode::LOAD:
            os << "LOAD " << dest << ", [stack_slot_" << slotIndex << "]";
            break;
        case Opcode::STORE:
            os << "STORE " << src1 << ", [stack_slot_" << slotIndex << "]";
            break;
        case Opcode::GOTO:
            os << "GOTO " << label;
            break;
        case Opcode::BRANCH:
            os << "IF " << src1 << " GOTO " << label;
            break;
        case Opcode::LABEL:
            os << "LABEL " << label;
            break;
    }
    return os.str();
}

// ----------------------------------------------------------------------------
// Factory helpers
// ----------------------------------------------------------------------------
Instruction Instruction::makeMov(const std::string& d, const std::string& s) {
    return {Opcode::MOV, d, s, "", "", -1};
}

Instruction Instruction::makeArith(Opcode op, const std::string& d,
                                   const std::string& s1, const std::string& s2) {
    return {op, d, s1, s2, "", -1};
}

Instruction Instruction::makeLoad(const std::string& d, int slot) {
    return {Opcode::LOAD, d, "", "", "", slot};
}

Instruction Instruction::makeStore(const std::string& s, int slot) {
    return {Opcode::STORE, "", s, "", "", slot};
}

Instruction Instruction::makeGoto(const std::string& lbl) {
    return {Opcode::GOTO, "", "", "", lbl, -1};
}

Instruction Instruction::makeBranch(const std::string& cond, const std::string& lbl) {
    return {Opcode::BRANCH, "", cond, "", lbl, -1};
}

Instruction Instruction::makeLabel(const std::string& lbl) {
    return {Opcode::LABEL, "", "", "", lbl, -1};
}

} // namespace regalloc
