// ============================================================================
// ControlFlowGraph.cpp — CFG construction
// ============================================================================
// Leader identification:
//   1. The first instruction is a leader.
//   2. Any instruction that is a LABEL is a leader.
//   3. Any instruction immediately following a GOTO, BRANCH, or LABEL is a
//      leader (unless it is the same LABEL that started the block).
//
// Edge creation:
//   - GOTO L        → edge to the block starting with LABEL L
//   - IF x GOTO L   → edge to LABEL L (taken) + fall-through (not taken)
//   - Otherwise     → fall-through to next block
// ============================================================================

#include "ControlFlowGraph.h"
#include <set>
#include <sstream>
#include <algorithm>

namespace regalloc {

void ControlFlowGraph::build(const IRProgram& program) {
    blocks_.clear();
    labelToBlock_.clear();

    const auto& instrs = program.instructions;
    if (instrs.empty()) return;

    // ---- Step 1: Identify leader indices ----
    std::set<size_t> leaderSet;
    leaderSet.insert(0); // first instruction is always a leader

    for (size_t i = 0; i < instrs.size(); ++i) {
        if (instrs[i].isLabel()) {
            leaderSet.insert(i); // LABEL is a leader
            if (i + 1 < instrs.size()) leaderSet.insert(i + 1);
        }
        if (instrs[i].isControlFlow()) {
            // instruction after a branch/goto is a leader
            if (i + 1 < instrs.size()) leaderSet.insert(i + 1);
        }
    }

    std::vector<size_t> leaders(leaderSet.begin(), leaderSet.end());
    std::sort(leaders.begin(), leaders.end());

    // ---- Step 2: Partition into basic blocks ----
    for (size_t li = 0; li < leaders.size(); ++li) {
        auto bb = std::make_unique<BasicBlock>();
        bb->id = static_cast<int>(li);

        size_t start = leaders[li];
        size_t end   = (li + 1 < leaders.size()) ? leaders[li + 1] : instrs.size();

        for (size_t j = start; j < end; ++j) {
            bb->instructions.push_back(instrs[j]);
        }

        // Register label → block mapping
        if (!bb->instructions.empty() && bb->instructions.front().isLabel()) {
            labelToBlock_[bb->instructions.front().label] = bb.get();
        }

        blocks_.push_back(std::move(bb));
    }

    // ---- Step 3: Create edges ----
    for (size_t i = 0; i < blocks_.size(); ++i) {
        BasicBlock* bb = blocks_[i].get();
        if (bb->instructions.empty()) continue;

        const Instruction& last = bb->instructions.back();

        if (last.opcode == Opcode::GOTO) {
            // Unconditional jump — single edge to target
            BasicBlock* target = blockForLabel(last.label);
            if (target) {
                bb->successors.push_back(target);
                target->predecessors.push_back(bb);
            }
        } else if (last.opcode == Opcode::BRANCH) {
            // Conditional branch — edge to target AND fall-through
            BasicBlock* target = blockForLabel(last.label);
            if (target) {
                bb->successors.push_back(target);
                target->predecessors.push_back(bb);
            }
            // Fall-through
            if (i + 1 < blocks_.size()) {
                BasicBlock* next = blocks_[i + 1].get();
                bb->successors.push_back(next);
                next->predecessors.push_back(bb);
            }
        } else {
            // No control-flow terminator → fall through to next block
            if (i + 1 < blocks_.size()) {
                BasicBlock* next = blocks_[i + 1].get();
                bb->successors.push_back(next);
                next->predecessors.push_back(bb);
            }
        }
    }
}

BasicBlock* ControlFlowGraph::blockForLabel(const std::string& label) const {
    auto it = labelToBlock_.find(label);
    return (it != labelToBlock_.end()) ? it->second : nullptr;
}

std::string ControlFlowGraph::toString() const {
    std::ostringstream os;
    os << "=== Control-Flow Graph ===\n";
    for (auto& bb : blocks_) {
        os << bb->toString() << "\n";
    }
    return os.str();
}

} // namespace regalloc
