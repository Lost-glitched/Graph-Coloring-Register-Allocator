// ============================================================================
// SpillCostAnalyzer.cpp — Spill cost heuristic implementation
// ============================================================================
// Loop depth estimation: A simple DFS on the CFG detects back-edges
// (edges to already-visited blocks that are ancestors on the DFS tree).
// Each block inside a back-edge cycle gets an increased loop depth.
//
// For simplicity, we use a single-pass heuristic: count back-edges that
// include each block.  This is approximate but sufficient for a
// teaching/demonstration allocator.
// ============================================================================

#include "SpillCostAnalyzer.h"
#include <cmath>
#include <algorithm>
#include <unordered_set>
#include <vector>
#include <stack>
#include <functional>

namespace regalloc {

// ---------------------------------------------------------------------------
// computeBlockDepths — estimate loop depth per block via back-edge counting
// ---------------------------------------------------------------------------
std::unordered_map<int, int> SpillCostAnalyzer::computeBlockDepths(
    const ControlFlowGraph& cfg) const {

    std::unordered_map<int, int> depth;
    for (auto& bb : cfg.blocks()) depth[bb->id] = 0;

    if (cfg.blocks().empty()) return depth;

    // DFS to find back-edges
    enum class Color { WHITE, GRAY, BLACK };
    std::unordered_map<int, Color> color;
    for (auto& bb : cfg.blocks()) color[bb->id] = Color::WHITE;

    // Collect back-edges
    std::vector<std::pair<int, int>> backEdges;

    std::function<void(BasicBlock*)> dfs = [&](BasicBlock* u) {
        color[u->id] = Color::GRAY;
        for (auto* v : u->successors) {
            if (color[v->id] == Color::GRAY) {
                // Back-edge found: u → v
                backEdges.push_back({u->id, v->id});
            } else if (color[v->id] == Color::WHITE) {
                dfs(v);
            }
        }
        color[u->id] = Color::BLACK;
    };

    dfs(cfg.blocks().front().get());

    // For each back-edge (u→v), increment loop depth for all blocks from v to u
    // in the block order (a simple approximation).
    for (auto& [tailId, headId] : backEdges) {
        int lo = std::min(headId, tailId);
        int hi = std::max(headId, tailId);
        for (auto& bb : cfg.blocks()) {
            if (bb->id >= lo && bb->id <= hi) {
                depth[bb->id]++;
            }
        }
    }

    return depth;
}

// ---------------------------------------------------------------------------
// compute
// ---------------------------------------------------------------------------
void SpillCostAnalyzer::compute(const IRProgram& program,
                                const ControlFlowGraph& cfg,
                                const InterferenceGraph& ig) {
    costs_.clear();

    auto blockDepths = computeBlockDepths(cfg);

    // Walk through blocks and instructions, accumulating cost per variable
    std::unordered_map<std::string, double> rawCost;

    for (auto& bb : cfg.blocks()) {
        int loopDepth = blockDepths[bb->id];
        double weight = std::pow(10.0, loopDepth);

        for (auto& instr : bb->instructions) {
            for (auto& d : instr.getDefs()) {
                rawCost[d] += weight;
            }
            for (auto& u : instr.getUses()) {
                rawCost[u] += weight;
            }
        }
    }

    // Divide by degree
    for (auto& [var, cost] : rawCost) {
        int deg = ig.degree(var);
        costs_[var] = cost / std::max(deg, 1);
    }
}

double SpillCostAnalyzer::cost(const std::string& var) const {
    auto it = costs_.find(var);
    if (it == costs_.end()) return 1e18; // unknown → very expensive to spill
    return it->second;
}

} // namespace regalloc
