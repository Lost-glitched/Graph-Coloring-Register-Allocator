#pragma once
// ============================================================================
// Instruction.h — Three-Address Code Instruction Representation
// ============================================================================
// Each instruction in the IR carries an opcode, optional destination, sources,
// label, and stack-slot index.  Helper methods expose definitions, uses,
// move/control-flow classification, and pretty-printing.
// ============================================================================

#include <string>
#include <vector>
#include <sstream>
#include <optional>

namespace regalloc {

// ----------------------------------------------------------------------------
// Opcode — the set of operations understood by the allocator
// ----------------------------------------------------------------------------
enum class Opcode {
    MOV,        // dest = src1                      (copy / move)
    ADD,        // dest = src1 + src2
    SUB,        // dest = src1 - src2
    MUL,        // dest = src1 * src2
    DIV,        // dest = src1 / src2
    LOAD,       // dest = mem[slot]                  (load from stack)
    STORE,      // mem[slot] = src1                  (store to stack)
    GOTO,       // unconditional jump to label
    BRANCH,     // IF src1 GOTO label                (conditional branch)
    LABEL       // label marker (not a real instruction)
};

/// Human-readable opcode name.
std::string opcodeToString(Opcode op);

// ----------------------------------------------------------------------------
// Instruction
// ----------------------------------------------------------------------------
struct Instruction {
    Opcode      opcode;
    std::string dest;           // Destination register / variable (empty if none)
    std::string src1;           // First source operand  (empty if none)
    std::string src2;           // Second source operand (empty if none)
    std::string label;          // Target label for GOTO / BRANCH / LABEL
    int         slotIndex{-1};  // Stack-frame slot for LOAD / STORE (-1 = N/A)

    // -- Query helpers -------------------------------------------------------

    /// Returns the list of variables defined by this instruction.
    std::vector<std::string> getDefs() const;

    /// Returns the list of variables used by this instruction.
    std::vector<std::string> getUses() const;

    /// True if this is a register-to-register copy (MOV).
    bool isMove() const;

    /// True if this instruction transfers control flow (GOTO, BRANCH).
    bool isControlFlow() const;

    /// True if this is a LABEL pseudo-instruction.
    bool isLabel() const;

    /// Pretty-print.
    std::string toString() const;

    // -- Factory helpers -----------------------------------------------------

    static Instruction makeMov(const std::string& dest, const std::string& src);
    static Instruction makeArith(Opcode op, const std::string& dest,
                                 const std::string& src1, const std::string& src2);
    static Instruction makeLoad(const std::string& dest, int slot);
    static Instruction makeStore(const std::string& src, int slot);
    static Instruction makeGoto(const std::string& label);
    static Instruction makeBranch(const std::string& cond, const std::string& label);
    static Instruction makeLabel(const std::string& label);
};

} // namespace regalloc
