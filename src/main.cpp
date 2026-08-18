// ============================================================================
// main.cpp — Demonstration driver for the Chaitin-Briggs Register Allocator
// ============================================================================
// This program constructs a sample IR that exercises:
//   - Multiple virtual registers
//   - At least one branch (conditional + unconditional)
//   - At least one copy/move instruction
//   - Enough interference to demonstrate coalescing
//   - Enough register pressure to demonstrate spilling
//
// The value of K (number of physical registers) is configurable.
// ============================================================================

#include "Parser.h"
#include "IRProgram.h"
#include "ControlFlowGraph.h"
#include "LivenessAnalyzer.h"
#include "InterferenceGraph.h"
#include "RegisterAllocator.h"

#include <iostream>
#include <string>
#include <cstdlib>

using namespace regalloc;

int main(int argc, char* argv[]) {
    // -----------------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------------
    int K = 3;  // Number of physical registers

    if (argc >= 2) {
        K = std::atoi(argv[1]);
        if (K < 1) {
            std::cerr << "Error: K must be >= 1\n";
            return 1;
        }
    }

    std::cout << "==========================================================\n";
    std::cout << "  Chaitin-Briggs Graph-Coloring Register Allocator\n";
    std::cout << "  Physical registers: K = " << K << "\n";
    std::cout << "==========================================================\n\n";

    // -----------------------------------------------------------------------
    // Sample IR program
    // -----------------------------------------------------------------------
    // This program has:
    //   - 7 virtual registers (a, b, c, d, e, t1, t2)
    //   - A copy instruction (t2 = t1) for coalescing
    //   - A conditional branch for multi-block CFG
    //   - A loop (GOTO back to L1) for spill-cost testing
    //   - Enough variables alive simultaneously to force spilling with K=3
    //
    // Conceptual program:
    //   a = 1 (we simulate with a = input1 + input2 style)
    //   b = a + c
    //   t1 = b * d
    //   t2 = t1          (copy — coalescing candidate)
    //   e = t2 + a
    //   IF e GOTO L2
    //   LABEL L1
    //   d = a + b
    //   c = d * e
    //   GOTO L1           (loop back-edge)
    //   LABEL L2
    //   t1 = c + e
    //   a = t1 - d

    std::string irSource = R"(
# Sample IR for register allocation demonstration
# Variables: a, b, c, d, e, t1, t2
# Contains: branch, loop, copy instruction, high register pressure

a = b + c
t1 = a * d
t2 = t1
e = t2 + a
IF e GOTO L2
LABEL L1
d = a + b
c = d * e
GOTO L1
LABEL L2
t1 = c + e
a = t1 - d
)";

    // -----------------------------------------------------------------------
    // Parse
    // -----------------------------------------------------------------------
    Parser parser;
    IRProgram program;
    try {
        program = parser.parse(irSource);
    } catch (const std::exception& ex) {
        std::cerr << "Parse error: " << ex.what() << "\n";
        return 1;
    }

    std::cout << "=== Original IR ===\n";
    std::cout << program.toString() << "\n";

    // Keep a copy for comparison
    IRProgram originalProgram = program.clone();

    // -----------------------------------------------------------------------
    // Run the allocator
    // -----------------------------------------------------------------------
    RegisterAllocator allocator(K);

    // Enable verbose logging to stdout
    allocator.setLogger([](const std::string& msg) {
        std::cout << msg << "\n";
    });

    bool success = allocator.allocate(program);

    // -----------------------------------------------------------------------
    // Print results
    // -----------------------------------------------------------------------
    std::cout << "\n";
    std::cout << "==========================================================\n";
    std::cout << "  ALLOCATION " << (success ? "SUCCEEDED" : "FAILED") << "\n";
    std::cout << "==========================================================\n\n";

    if (success) {
        // Print allocation table
        std::cout << allocator.result().toString(K) << "\n";

        // Print final rewritten IR
        std::cout << "=== Final Rewritten IR ===\n";
        std::cout << program.toString() << "\n";

        // Print round-by-round summary
        std::cout << "=== Allocation Trace Summary ===\n";
        for (auto& trace : allocator.traces()) {
            std::cout << "Round " << trace.round << ":\n";

            // Coalescing
            if (!trace.coalesceResults.empty()) {
                std::cout << "  Coalescing:\n";
                for (auto& cr : trace.coalesceResults) {
                    if (cr.coalesced) {
                        std::cout << "    (" << cr.a << "," << cr.b << ") -> MERGED\n";
                    } else {
                        std::cout << "    (" << cr.a << "," << cr.b << ") -> REJECTED: "
                                  << cr.reason << "\n";
                    }
                }
            }

            // Simplify order
            if (!trace.simplifyOrder.empty()) {
                std::cout << "  Simplify order (push):";
                for (auto& v : trace.simplifyOrder) std::cout << " " << v;
                std::cout << "\n";
            }

            // Spill candidates
            if (!trace.spillCandidates.empty()) {
                std::cout << "  Optimistic spill candidates:";
                for (auto& v : trace.spillCandidates) std::cout << " " << v;
                std::cout << "\n";
            }

            // Select assignments
            if (!trace.selectAssignment.empty()) {
                std::cout << "  Select assignments:\n";
                // Sort for deterministic output
                std::vector<std::pair<std::string, int>> sorted(
                    trace.selectAssignment.begin(), trace.selectAssignment.end());
                std::sort(sorted.begin(), sorted.end());
                for (auto& [var, reg] : sorted) {
                    std::cout << "    " << var << " -> R" << reg << "\n";
                }
            }

            // Actual spills
            if (!trace.actualSpills.empty()) {
                std::cout << "  Actual spills:";
                for (auto& v : trace.actualSpills) std::cout << " " << v;
                std::cout << "\n";
            }

            std::cout << "\n";
        }
    }

    return success ? 0 : 1;
}
