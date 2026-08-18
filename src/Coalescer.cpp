// ============================================================================
// Coalescer.cpp — Briggs' conservative coalescing implementation
// ============================================================================
// Briggs' criterion: two nodes a and b can be coalesced if the resulting
// merged node has fewer than K neighbors whose degree (in the merged graph)
// is >= K.  This guarantees that coalescing does not make an otherwise
// K-colorable graph uncolorable.
// ============================================================================

#include "Coalescer.h"
#include <algorithm>
#include <unordered_set>

namespace regalloc {

// ---------------------------------------------------------------------------
// briggsSafe — evaluate Briggs' criterion
// ---------------------------------------------------------------------------
bool Coalescer::briggsSafe(const InterferenceGraph& ig,
                           const std::string& a,
                           const std::string& b,
                           int K) const {
    // Compute the union of neighbors of a and b (excluding a and b themselves).
    auto na = ig.neighbors(a);
    auto nb = ig.neighbors(b);

    std::unordered_set<std::string> combined;
    for (auto& n : na) { if (n != b) combined.insert(n); }
    for (auto& n : nb) { if (n != a) combined.insert(n); }

    // Count how many of these combined neighbors would have degree >= K
    // in the merged graph.  When we merge a and b into a single node, a
    // neighbor that was adjacent to both a and b loses one edge (the duplicate
    // is collapsed).  So the new degree of neighbor n is:
    //   newDeg(n) = currentDeg(n)
    //            - (n is neighbor of both a AND b ? 1 : 0)
    //
    // But we also need to account for the fact that a and b themselves are
    // being removed and replaced by a single node, so if n was adjacent to
    // both, one of those adjacency entries merges into one.
    //
    // More precisely: after merging a and b into ab, the neighbor set of
    // each existing node n (n != a, n != b) becomes:
    //   (neighbors(n) - {a, b}) ∪ {ab}   if n was adjacent to a or b
    //
    // So newDeg(n) = deg(n) - #{a,b} that are neighbors of n + 1
    //   = deg(n) - (isNeighborOfA + isNeighborOfB) + 1
    //   if n is neighbor of both a and b:  newDeg = deg(n) - 2 + 1 = deg(n) - 1
    //   if n is neighbor of only one:      newDeg = deg(n) - 1 + 1 = deg(n)

    int significantCount = 0;
    for (auto& n : combined) {
        bool adjA = na.count(n) > 0;
        bool adjB = nb.count(n) > 0;
        int currentDeg = ig.degree(n);
        int newDeg = currentDeg;
        if (adjA && adjB) {
            newDeg = currentDeg - 1;
        }
        // else newDeg = currentDeg (unchanged)

        if (newDeg >= K) {
            ++significantCount;
        }
    }

    return significantCount < K;
}

// ---------------------------------------------------------------------------
// coalesce
// ---------------------------------------------------------------------------
std::vector<CoalesceResult> Coalescer::coalesce(
    InterferenceGraph& ig,
    MoveGraph& moves,
    IRProgram& program,
    int K) {

    std::vector<CoalesceResult> results;

    // Work through moves. Because coalescing changes the graph, we iterate
    // until no more coalescing is possible in this round.
    bool progress = true;
    while (progress) {
        progress = false;

        auto movePairs = moves.moves(); // snapshot
        moves.clear();

        for (auto& mp : movePairs) {
            std::string a = mp.dest;
            std::string b = mp.src;

            // Skip if either node has been removed (already coalesced earlier)
            if (!ig.hasNode(a) || !ig.hasNode(b)) {
                continue;
            }

            // Skip if a and b are the same (self-move after renaming)
            if (a == b) {
                continue;
            }

            // If a and b already interfere, coalescing is impossible
            if (ig.hasEdge(a, b)) {
                CoalesceResult cr{a, b, false, "nodes already interfere"};
                results.push_back(cr);
                // Re-add the move so it stays in the worklist
                moves.addMove(b, a);
                continue;
            }

            // Check Briggs' criterion
            if (briggsSafe(ig, a, b, K)) {
                // Safe to coalesce: merge b into a
                ig.mergeNodes(a, b);
                program.renameVariable(b, a);

                // Remove redundant MOV instructions (dest = src where both are now `a`)
                auto& instrs = program.instructions;
                instrs.erase(
                    std::remove_if(instrs.begin(), instrs.end(),
                        [](const Instruction& inst) {
                            return inst.isMove() && inst.dest == inst.src1;
                        }),
                    instrs.end());

                results.push_back({a, b, true, ""});
                progress = true;
            } else {
                results.push_back({a, b, false, "unsafe for K-coloring (Briggs' criterion)"});
                moves.addMove(b, a);
            }
        }
    }

    return results;
}

} // namespace regalloc
