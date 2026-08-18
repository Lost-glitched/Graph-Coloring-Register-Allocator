// ============================================================================
// LivenessAnalyzer.cpp — Liveness analysis implementation
// ============================================================================
// Block-level:
//   USE[B] = variables used in B before any definition in B
//   DEF[B] = variables defined in B
//   Fixed point:
//     LIVE_OUT[B] = ∪ LIVE_IN[S]  for every successor S of B
//     LIVE_IN[B]  = USE[B] ∪ (LIVE_OUT[B] − DEF[B])
//
// Instruction-level (backward scan within each block):
//   LIVE_OUT(last)  = LIVE_OUT[B]
//   LIVE_OUT(i)     = LIVE_IN(i+1)
//   LIVE_IN(i)      = USE(i) ∪ (LIVE_OUT(i) − DEF(i))
// ============================================================================

#include "LivenessAnalyzer.h"
#include <sstream>
#include <algorithm>

namespace regalloc {

// ---------------------------------------------------------------------------
// Set helpers
// ---------------------------------------------------------------------------
static std::unordered_set<std::string> setUnion(
    const std::unordered_set<std::string>& a,
    const std::unordered_set<std::string>& b) {
    auto result = a;
    result.insert(b.begin(), b.end());
    return result;
}

static std::unordered_set<std::string> setDifference(
    const std::unordered_set<std::string>& a,
    const std::unordered_set<std::string>& b) {
    std::unordered_set<std::string> result;
    for (auto& x : a) {
        if (b.find(x) == b.end()) result.insert(x);
    }
    return result;
}

// ---------------------------------------------------------------------------
// computeBlockSets
// ---------------------------------------------------------------------------
void LivenessAnalyzer::computeBlockSets(const ControlFlowGraph& cfg) {
    for (auto& bb : cfg.blocks()) {
        BlockSummary& s = blockSummaries_[bb->id];
        s.use.clear();
        s.def.clear();

        for (auto& instr : bb->instructions) {
            // Uses that are not yet defined in this block → upward exposed
            for (auto& u : instr.getUses()) {
                if (s.def.find(u) == s.def.end()) {
                    s.use.insert(u);
                }
            }
            // Definitions
            for (auto& d : instr.getDefs()) {
                s.def.insert(d);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// computeFixedPoint
// ---------------------------------------------------------------------------
void LivenessAnalyzer::computeFixedPoint(const ControlFlowGraph& cfg) {
    // Initialize LIVE_IN and LIVE_OUT to empty
    for (auto& bb : cfg.blocks()) {
        blockSummaries_[bb->id].liveIn.clear();
        blockSummaries_[bb->id].liveOut.clear();
    }

    bool changed = true;
    while (changed) {
        changed = false;

        // Process blocks in reverse order for faster convergence
        for (auto it = cfg.blocks().rbegin(); it != cfg.blocks().rend(); ++it) {
            BasicBlock* bb = it->get();
            BlockSummary& s = blockSummaries_[bb->id];

            // LIVE_OUT[B] = ∪ LIVE_IN[S] for every successor S
            std::unordered_set<std::string> newLiveOut;
            for (auto* succ : bb->successors) {
                auto& succIn = blockSummaries_[succ->id].liveIn;
                newLiveOut.insert(succIn.begin(), succIn.end());
            }

            // LIVE_IN[B] = USE[B] ∪ (LIVE_OUT[B] − DEF[B])
            auto newLiveIn = setUnion(s.use, setDifference(newLiveOut, s.def));

            if (newLiveIn != s.liveIn || newLiveOut != s.liveOut) {
                changed = true;
                s.liveIn  = std::move(newLiveIn);
                s.liveOut = std::move(newLiveOut);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// computeInstrLevel
// ---------------------------------------------------------------------------
void LivenessAnalyzer::computeInstrLevel(const ControlFlowGraph& cfg) {
    // Flatten instructions across blocks and compute instruction-level liveness
    // by scanning backward within each block.

    // First, compute total instruction count to pre-allocate
    size_t total = 0;
    for (auto& bb : cfg.blocks()) total += bb->instructions.size();
    instrLiveness_.resize(total);

    size_t globalIdx = 0;
    for (auto& bb : cfg.blocks()) {
        size_t blockStart = globalIdx;
        size_t blockEnd   = globalIdx + bb->instructions.size();

        // Process backward within the block
        for (int i = static_cast<int>(bb->instructions.size()) - 1; i >= 0; --i) {
            size_t idx = blockStart + static_cast<size_t>(i);
            auto& il = instrLiveness_[idx];

            // LIVE_OUT(last) = LIVE_OUT[Block]
            // LIVE_OUT(i) = LIVE_IN(i+1)
            if (static_cast<size_t>(i) == bb->instructions.size() - 1) {
                il.liveOut = blockSummaries_[bb->id].liveOut;
            } else {
                il.liveOut = instrLiveness_[idx + 1].liveIn;
            }

            // LIVE_IN(i) = USE(i) ∪ (LIVE_OUT(i) − DEF(i))
            const auto& instr = bb->instructions[static_cast<size_t>(i)];
            auto defs = instr.getDefs();
            auto uses = instr.getUses();

            std::unordered_set<std::string> defSet(defs.begin(), defs.end());
            std::unordered_set<std::string> useSet(uses.begin(), uses.end());

            il.liveIn = setUnion(useSet, setDifference(il.liveOut, defSet));
        }

        globalIdx = blockEnd;
    }
}

// ---------------------------------------------------------------------------
// analyze
// ---------------------------------------------------------------------------
void LivenessAnalyzer::analyze(const ControlFlowGraph& cfg) {
    blockSummaries_.clear();
    instrLiveness_.clear();
    computeBlockSets(cfg);
    computeFixedPoint(cfg);
    computeInstrLevel(cfg);
}

const BlockSummary& LivenessAnalyzer::getBlockSummary(int blockId) const {
    return blockSummaries_.at(blockId);
}

const InstrLiveness& LivenessAnalyzer::getInstrLiveness(size_t globalIndex) const {
    return instrLiveness_.at(globalIndex);
}

// ---------------------------------------------------------------------------
// toString
// ---------------------------------------------------------------------------
std::string LivenessAnalyzer::toString() const {
    std::ostringstream os;
    os << "=== Liveness Analysis ===\n";

    // Sort block IDs for deterministic output
    std::vector<int> ids;
    for (auto& [id, _] : blockSummaries_) ids.push_back(id);
    std::sort(ids.begin(), ids.end());

    for (int id : ids) {
        auto& s = blockSummaries_.at(id);
        os << "Block B" << id << ":\n";

        auto printSet = [&](const std::string& name,
                            const std::unordered_set<std::string>& set) {
            os << "  " << name << ": {";
            std::vector<std::string> sorted(set.begin(), set.end());
            std::sort(sorted.begin(), sorted.end());
            for (size_t i = 0; i < sorted.size(); ++i) {
                if (i) os << ", ";
                os << sorted[i];
            }
            os << "}\n";
        };

        printSet("USE     ", s.use);
        printSet("DEF     ", s.def);
        printSet("LIVE_IN ", s.liveIn);
        printSet("LIVE_OUT", s.liveOut);
        os << "\n";
    }

    // Instruction-level
    os << "--- Instruction-Level Liveness ---\n";
    for (size_t i = 0; i < instrLiveness_.size(); ++i) {
        auto& il = instrLiveness_[i];
        std::vector<std::string> in(il.liveIn.begin(), il.liveIn.end());
        std::vector<std::string> out(il.liveOut.begin(), il.liveOut.end());
        std::sort(in.begin(), in.end());
        std::sort(out.begin(), out.end());

        os << "  Instr " << i << ": IN={";
        for (size_t j = 0; j < in.size(); ++j) { if (j) os << ","; os << in[j]; }
        os << "} OUT={";
        for (size_t j = 0; j < out.size(); ++j) { if (j) os << ","; os << out[j]; }
        os << "}\n";
    }

    return os.str();
}

} // namespace regalloc
