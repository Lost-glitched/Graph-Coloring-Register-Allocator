#pragma once
// ============================================================================
// ControlFlowGraph.h — CFG construction from IR
// ============================================================================
// Identifies leaders, partitions instructions into basic blocks, and creates
// directed edges for fall-through, unconditional jumps, and conditional
// branches.
// ============================================================================

#include "BasicBlock.h"
#include "IRProgram.h"
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <stdexcept>

namespace regalloc {

class ControlFlowGraph {
public:
    /// Build (or rebuild) the CFG from the given program.
    void build(const IRProgram& program);

    /// Access the ordered list of basic blocks.
    const std::vector<std::unique_ptr<BasicBlock>>& blocks() const { return blocks_; }

    /// Find the block that starts with the given label, or nullptr.
    BasicBlock* blockForLabel(const std::string& label) const;

    /// Pretty-print the entire CFG.
    std::string toString() const;

private:
    std::vector<std::unique_ptr<BasicBlock>> blocks_;
    std::unordered_map<std::string, BasicBlock*> labelToBlock_;
};

} // namespace regalloc
