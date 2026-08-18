// ============================================================================
// RegisterAllocator.cpp — Chaitin-Briggs allocation engine
// ============================================================================
// This is the central driver implementing the iterative allocation loop:
//
//   repeat until no spills or max rounds:
//     1. Build CFG from current IR
//     2. Run backward liveness analysis
//     3. Build interference graph (Chaitin's rule for MOV)
//     4. Detect move instructions, attempt conservative coalescing
//     5. Simplify: push degree<K nodes; optimistic spill for degree>=K
//     6. Select: pop and color; identify actual spills
//     7. If actual spills, rewrite IR and loop
//
// Deterministic tie-breaking: lexicographically smallest variable name.
// Physical registers: R0, R1, ..., R(K-1).
// ============================================================================

#include "RegisterAllocator.h"
#include <algorithm>
#include <sstream>
#include <iostream>
#include <set>
#include <cassert>

namespace regalloc {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
RegisterAllocator::RegisterAllocator(int K) : K_(K) {}

void RegisterAllocator::setLogger(std::function<void(const std::string&)> logger) {
    logger_ = std::move(logger);
}

void RegisterAllocator::log(const std::string& msg) const {
    if (logger_) logger_(msg);
}

// ---------------------------------------------------------------------------
// buildInterferenceGraph — from liveness data
// ---------------------------------------------------------------------------
// For each instruction that defines variable d:
//   - Add an edge (d, v) for every v in LIVE_OUT(instruction), v ≠ d.
//   - Exception (Chaitin's rule): for MOV d = s, do NOT add edge (d, s).
//     This preserves the opportunity to coalesce d and s.
// ---------------------------------------------------------------------------
InterferenceGraph RegisterAllocator::buildInterferenceGraph(
    const ControlFlowGraph& cfg,
    const LivenessAnalyzer& liveness) const {

    InterferenceGraph ig;

    // First, add all variables as nodes
    size_t globalIdx = 0;
    for (auto& bb : cfg.blocks()) {
        for (auto& instr : bb->instructions) {
            for (auto& d : instr.getDefs()) ig.addNode(d);
            for (auto& u : instr.getUses()) ig.addNode(u);
        }
    }

    // Now add interference edges
    globalIdx = 0;
    for (auto& bb : cfg.blocks()) {
        for (size_t i = 0; i < bb->instructions.size(); ++i) {
            const auto& instr = bb->instructions[i];
            const auto& il    = liveness.getInstrLiveness(globalIdx);
            auto defs = instr.getDefs();

            for (auto& d : defs) {
                for (auto& v : il.liveOut) {
                    if (v == d) continue;  // no self-edges

                    // Chaitin's MOV exception: for `d = s`, don't add edge (d, s)
                    if (instr.isMove() && v == instr.src1) continue;

                    ig.addEdge(d, v);
                }
            }
            ++globalIdx;
        }
    }

    return ig;
}

// ---------------------------------------------------------------------------
// detectMoves
// ---------------------------------------------------------------------------
MoveGraph RegisterAllocator::detectMoves(const IRProgram& program) const {
    MoveGraph mg;
    for (auto& instr : program.instructions) {
        if (instr.isMove() && !instr.src1.empty() && !instr.dest.empty()) {
            mg.addMove(instr.src1, instr.dest);
        }
    }
    return mg;
}

// ---------------------------------------------------------------------------
// simplify — push non-move-related nodes with degree < K; freeze; optimistic spill when stuck
// ---------------------------------------------------------------------------
std::vector<StackEntry> RegisterAllocator::simplify(
    InterferenceGraph& ig,
    MoveGraph& moves,
    const SpillCostAnalyzer& spillCosts,
    RoundTrace& trace) const {

    std::vector<StackEntry> stack;

    while (ig.activeNodeCount() > 0) {
        // --- Try to find a node with degree < K ---
        bool found = false;
        auto nodes = ig.activeNodes(); // sorted set

        for (auto& v : nodes) {
            if (ig.degree(v) < K_ && !moves.isMoveRelated(v)) {
                // Push and remove
                auto neighbors = ig.removeNode(v);
                stack.push_back({v, neighbors, false});
                trace.simplifyOrder.push_back(v);
                log("  SIMPLIFY: push " + v + " (degree " +
                    std::to_string(static_cast<int>(neighbors.size())) + " < " +
                    std::to_string(K_) + ", not move-related)");
                found = true;
                break;
            }
        }

        if (found) continue;

        // --- Freeze Phase ---
        // If simplify couldn't progress, try to freeze a low-degree move-related node
        bool frozen = false;
        for (auto& v : nodes) {
            if (ig.degree(v) < K_ && moves.isMoveRelated(v)) {
                log("  FREEZE: giving up coalescing for " + v);
                moves.removeMovesFor(v);
                trace.freezeOrder.push_back(v);
                frozen = true;
                break;
            }
        }

        if (frozen) continue;

        // --- All nodes have degree >= K → optimistic spill ---
        // Pick the node with the lowest spill cost (best candidate to spill).
        std::string bestCandidate;
        double bestCost = 1e18;

        for (auto& v : nodes) {
            double c = spillCosts.cost(v);
            if (c < bestCost || (c == bestCost && v < bestCandidate)) {
                bestCost = c;
                bestCandidate = v;
            }
        }

        if (bestCandidate.empty()) break; // safety

        auto neighbors = ig.removeNode(bestCandidate);
        stack.push_back({bestCandidate, neighbors, true});
        trace.spillCandidates.push_back(bestCandidate);
        trace.optimisticSpillOrder.push_back(bestCandidate);
        trace.simplifyOrder.push_back(bestCandidate);
        log("  SPILL CANDIDATE (optimistic): " + bestCandidate +
            " (cost=" + std::to_string(bestCost) + ")");
    }

    return stack;
}

// ---------------------------------------------------------------------------
// select — pop from stack, assign physical registers
// ---------------------------------------------------------------------------
std::unordered_set<std::string> RegisterAllocator::select(
    const std::vector<StackEntry>& stack,
    InterferenceGraph& ig,
    RoundTrace& trace) {

    std::unordered_set<std::string> actualSpills;

    // Process stack in LIFO order (last pushed = first popped)
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
        const auto& entry = *it;

        // Restore the node in the interference graph
        ig.restoreNode(entry.variable, entry.neighbors);

        // Determine which physical registers are already taken by neighbors
        std::set<int> usedRegs;
        for (auto& n : ig.neighbors(entry.variable)) {
            auto regIt = trace.selectAssignment.find(n);
            if (regIt != trace.selectAssignment.end()) {
                usedRegs.insert(regIt->second);
            }
        }

        // Find the lowest available register
        int assignedReg = -1;
        for (int r = 0; r < K_; ++r) {
            if (usedRegs.find(r) == usedRegs.end()) {
                assignedReg = r;
                break;
            }
        }

        if (assignedReg >= 0) {
            trace.selectAssignment[entry.variable] = assignedReg;
            log("  SELECT: " + entry.variable + " -> R" +
                std::to_string(assignedReg) +
                (entry.optimisticSpill ? " (was optimistic spill candidate)" : ""));
        } else {
            // Actual spill
            actualSpills.insert(entry.variable);
            trace.actualSpills.insert(entry.variable);
            log("  SELECT: " + entry.variable + " -> SPILL (no register available)" +
                (entry.optimisticSpill ? " [optimistic spill confirmed]" : ""));
        }
    }

    return actualSpills;
}

// ---------------------------------------------------------------------------
// applyAssignment — replace variable names with physical register names
// ---------------------------------------------------------------------------
void RegisterAllocator::applyAssignment(
    IRProgram& program,
    const std::unordered_map<std::string, int>& assignment) const {

    for (auto& [var, reg] : assignment) {
        std::string physName = "R" + std::to_string(reg);
        program.renameVariable(var, physName);
    }
}

// ---------------------------------------------------------------------------
// allocate — the main iterative allocation loop
// ---------------------------------------------------------------------------
bool RegisterAllocator::allocate(IRProgram& program) {
    traces_.clear();
    result_ = AllocationResult{};
    nextStackSlot_ = 0;
    tempCounter_ = 0;
    spilledVariables_.clear();
    coalescedAliases_.clear();

    // Check for physical register name collisions in the input
    std::set<std::string> invalidNames;
    for (int i = 0; i < K_; ++i) invalidNames.insert("R" + std::to_string(i));
    for (auto& v : program.allVariables()) {
        if (invalidNames.count(v)) {
            // Prepend a prefix to sanitize user variables colliding with R0, R1...
            program.renameVariable(v, "_user_" + v);
        }
    }

    for (int round = 0; round < maxRounds_; ++round) {
        RoundTrace trace;
        trace.round = round;

        log("\n========== Allocation Round " + std::to_string(round) +
            " ==========\n");

        ControlFlowGraph cfg;
        LivenessAnalyzer liveness;
        InterferenceGraph ig;
        MoveGraph moves;
        std::vector<CoalesceResult> roundCoalesceResults;

        bool coalescingProgress = true;
        while (coalescingProgress) {
            coalescingProgress = false;

            // --- 1. Build CFG ---
            cfg.build(program);
            if (roundCoalesceResults.empty()) { // only log on first iteration
                trace.cfgDump = cfg.toString();
                log(trace.cfgDump);
            }

            // --- 2. Liveness analysis ---
            liveness.analyze(cfg);
            if (roundCoalesceResults.empty()) {
                trace.livenessDump = liveness.toString();
                log(trace.livenessDump);
            }

            // --- 3. Build interference graph ---
            ig = buildInterferenceGraph(cfg, liveness);
            if (roundCoalesceResults.empty()) {
                trace.interferenceGraphDump = ig.toString();
                log(trace.interferenceGraphDump);
            }

            // If no variables to allocate, we're done
            if (ig.activeNodeCount() == 0) {
                break;
            }

            // --- 4. Move detection + coalescing ---
            moves = detectMoves(program);
            Coalescer coalescer;
            auto cr = coalescer.coalesce(ig, moves, program, K_);
            
            if (!cr.empty()) {
                for (auto& c : cr) {
                    roundCoalesceResults.push_back(c);
                    if (c.coalesced) {
                        coalescedAliases_[c.b] = c.a;
                        coalescingProgress = true;
                        log("  Coalescing: " + c.a + " <- " + c.b + " : SAFE (merged), rebuilding CFG...");
                    } else {
                        log("  Coalescing: " + c.a + " <- " + c.b +
                            " : REJECTED (" + c.reason + ")");
                    }
                }
            }
        }
        trace.coalesceResults = roundCoalesceResults;

        if (ig.activeNodeCount() == 0) {
            log("No variables to allocate.\n");
            traces_.push_back(std::move(trace));
            break;
        }

        if (!roundCoalesceResults.empty()) {
            log("\n  After coalescing (final for round):\n" + ig.toString());
        }

        // --- 5. Spill cost analysis ---
        SpillCostAnalyzer spillCosts;
        spillCosts.compute(program, cfg, ig);

        // --- 6. Simplify ---
        log("\n--- Simplify Phase ---");
        auto selectStack = simplify(ig, moves, spillCosts, trace);

        // --- 7. Select / Color ---
        log("\n--- Select Phase ---");
        auto actualSpills = select(selectStack, ig, trace);

        // --- 8. Check for spills ---
        if (actualSpills.empty()) {
            // SUCCESS: apply assignment to IR
            log("\n--- All variables colored successfully! ---\n");

            // Build final result
            for (auto& [var, slot] : spilledVariables_) {
                Assignment a;
                a.variable = var;
                a.spilled  = true;
                a.physReg  = -1;
                a.stackSlot = slot;
                result_.assignments[var] = a;
            }

            for (auto& [var, reg] : trace.selectAssignment) {
                Assignment a;
                a.variable = var;
                a.spilled  = false;
                a.physReg  = reg;
                a.stackSlot = -1;
                result_.assignments[var] = a;
            }
            result_.totalStackSlots = nextStackSlot_;
            result_.aliases = coalescedAliases_;

            // Apply physical register names to the IR
            applyAssignment(program, trace.selectAssignment);
            traces_.push_back(std::move(trace));

            return true;
        }

        // --- 9. Spill rewriting ---
        log("\n--- Spill Rewriting ---");
        for (auto& sv : actualSpills) {
            log("  Spilling: " + sv);
        }

        SpillRewriter rewriter;
        auto newTemps = rewriter.rewrite(program, actualSpills, nextStackSlot_, tempCounter_, spilledVariables_);
        trace.spillTemps.insert(trace.spillTemps.end(), newTemps.begin(), newTemps.end());
        traces_.push_back(std::move(trace));

        log("  Inserted " + std::to_string(newTemps.size()) +
            " new temporaries for spills.");
        log("  Rewritten IR:\n" + program.toString());
    }

    // If we get here, we exhausted rounds without converging
    log("\nERROR: Allocation failed after " + std::to_string(maxRounds_) +
        " rounds.\n");
    return false;
}

std::string RegisterAllocator::finalIR(const IRProgram& program) const {
    return program.toString();
}

} // namespace regalloc
