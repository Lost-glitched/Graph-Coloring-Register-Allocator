#pragma once
// ============================================================================
// RegisterAllocator.h — Chaitin-Briggs register allocator driver
// ============================================================================
// Orchestrates the complete allocation pipeline:
//   1. Build/rebuild CFG
//   2. Liveness analysis
//   3. Interference graph construction
//   4. Move detection + conservative coalescing
//   5. Simplify (degree < K) + optimistic spill selection
//   6. Select / graph coloring
//   7. If actual spills → rewrite IR and repeat
//
// Terminates when all variables are allocated or when progress is impossible.
// ============================================================================

#include "IRProgram.h"
#include "ControlFlowGraph.h"
#include "LivenessAnalyzer.h"
#include "InterferenceGraph.h"
#include "MoveGraph.h"
#include "Coalescer.h"
#include "SpillCostAnalyzer.h"
#include "SpillRewriter.h"
#include "AllocationResult.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace regalloc {

/// Entry on the simplify/select stack.
struct StackEntry {
    std::string variable;
    std::unordered_set<std::string> neighbors;  // neighbors at time of removal
    bool optimisticSpill{false};                 // was this an optimistic spill?
};

/// Detailed trace of one allocation round.
struct RoundTrace {
    int round{0};
    std::string cfgDump;
    std::string livenessDump;
    std::string interferenceGraphDump;
    std::vector<CoalesceResult> coalesceResults;
    std::vector<std::string> simplifyOrder;     // pushed in this order
    std::vector<std::string> spillCandidates;   // optimistic spill candidates
    std::vector<std::string> freezeOrder;       // nodes frozen in this round
    std::vector<std::string> optimisticSpillOrder; // same as spillCandidates but explicit naming
    std::vector<std::string> spillTemps;         // fresh temporaries created during rewriting
    std::unordered_map<std::string, int> selectAssignment;  // var → reg index
    std::unordered_set<std::string> actualSpills;           // vars that actually spilled
};

class RegisterAllocator {
public:
    /// Configure the number of physical registers.
    explicit RegisterAllocator(int K);

    /// Run the full allocation pipeline on the given program.
    /// Returns true if allocation succeeds, false if it is impossible.
    bool allocate(IRProgram& program);

    /// Access the final allocation result.
    const AllocationResult& result() const { return result_; }

    /// Access the trace of each allocation round.
    const std::vector<RoundTrace>& traces() const { return traces_; }

    /// Get the final rewritten IR as a string.
    std::string finalIR(const IRProgram& program) const;

    /// Set a verbosity callback for live logging.
    void setLogger(std::function<void(const std::string&)> logger);

private:
    int K_;
    int maxRounds_{50};
    int nextStackSlot_{0};
    int tempCounter_{0};
    std::unordered_map<std::string, int> spilledVariables_;
    AllocationResult result_;
    std::vector<RoundTrace> traces_;
    std::function<void(const std::string&)> logger_;

    void log(const std::string& msg) const;

    // --- Pipeline stages ---

    /// Build the interference graph from liveness information.
    InterferenceGraph buildInterferenceGraph(
        const ControlFlowGraph& cfg,
        const LivenessAnalyzer& liveness) const;

    /// Detect move instructions in the program.
    MoveGraph detectMoves(const IRProgram& program) const;

    /// Simplify phase + freeze + optimistic spill selection.
    /// Returns the select stack (top = last pushed = first to pop).
    std::vector<StackEntry> simplify(
        InterferenceGraph& ig,
        MoveGraph& moves,
        const SpillCostAnalyzer& spillCosts,
        RoundTrace& trace) const;

    /// Select phase: pop from stack, assign registers.
    /// Returns the set of actually spilled variables (empty if all colored).
    std::unordered_set<std::string> select(
        const std::vector<StackEntry>& stack,
        InterferenceGraph& ig,
        RoundTrace& trace);

    /// Apply the register assignment to the IR, replacing variable names
    /// with physical register names (R0, R1, ...).
    void applyAssignment(IRProgram& program,
                         const std::unordered_map<std::string, int>& assignment) const;
};

} // namespace regalloc
