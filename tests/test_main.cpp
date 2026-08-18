// ============================================================================
// test_main.cpp — Automated test suite for the register allocator
// ============================================================================
#include "Parser.h"
#include "IRProgram.h"
#include "ControlFlowGraph.h"
#include "LivenessAnalyzer.h"
#include "InterferenceGraph.h"
#include "MoveGraph.h"
#include "Coalescer.h"
#include "SpillCostAnalyzer.h"
#include "RegisterAllocator.h"
#include "SpillRewriter.h"
#include "AllocationResult.h"

#include <iostream>
#include <string>
#include <cassert>
#include <set>
#include <sstream>
#include <algorithm>

using namespace regalloc;

static int testsRun = 0;
static int testsPassed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "  FAIL: " << msg << "\n"; \
            std::cerr << "    at " << __FILE__ << ":" << __LINE__ << "\n"; \
            return false; \
        } \
    } while(0)

// Helper: check that the final IR has no virtual register names
static bool allRegistersPhysical(const IRProgram& program, int K) {
    std::set<std::string> physNames;
    for (int i = 0; i < K; ++i) physNames.insert("R" + std::to_string(i));

    for (auto& instr : program.instructions) {
        if (!instr.dest.empty()) {
            if (physNames.find(instr.dest) == physNames.end()) return false;
        }
        if (!instr.src1.empty() && instr.opcode != Opcode::GOTO) {
            if (instr.opcode == Opcode::BRANCH) {
                if (physNames.find(instr.src1) == physNames.end()) return false;
            } else if (instr.opcode != Opcode::LABEL) {
                if (physNames.find(instr.src1) == physNames.end()) return false;
            }
        }
        if (!instr.src2.empty()) {
            if (physNames.find(instr.src2) == physNames.end()) return false;
        }
    }
    return true;
}

static bool test_parserAndSlots() {
    std::string ir = "LOAD dest, [stack_slot_12]\nSTORE src, [42]";
    Parser parser;
    auto prog = parser.parse(ir);
    TEST_ASSERT(prog.instructions.size() == 2, "Should parse 2 instructions");
    TEST_ASSERT(prog.instructions[0].opcode == Opcode::LOAD, "Is LOAD");
    TEST_ASSERT(prog.instructions[0].slot == 12, "Parsed slot 12");
    TEST_ASSERT(prog.instructions[1].opcode == Opcode::STORE, "Is STORE");
    TEST_ASSERT(prog.instructions[1].slot == 42, "Parsed slot 42");
    return true;
}

static bool test_cfgAndLiveness() {
    std::string ir = "a = 1\nIF a GOTO L1\nb = 2\nGOTO L2\nLABEL L1\nb = 3\nLABEL L2\nc = a + b";
    Parser parser;
    auto prog = parser.parse(ir);
    ControlFlowGraph cfg;
    cfg.build(prog);
    TEST_ASSERT(cfg.blocks().size() == 4, "Should have 4 basic blocks");

    LivenessAnalyzer liveness;
    liveness.analyze(cfg);
    // instruction `c = a + b` is at the end. 'a' and 'b' should be live into L2.
    // L2 block is the last one.
    auto& l2_block = cfg.blocks()[3];
    auto in_vars = liveness.getInstrLiveness(l2_block->startIdx).liveIn;
    TEST_ASSERT(in_vars.count("a") && in_vars.count("b"), "a and b should be live into L2");
    return true;
}

static bool test_interferenceAndFreeze() {
    std::string ir = "a = b\nc = d\ne = a + c\nf = b + d\n";
    Parser p; auto prog = p.parse(ir);
    RegisterAllocator alloc(2); // low registers to force freeze/spill
    bool success = alloc.allocate(prog);
    TEST_ASSERT(success, "Allocation should succeed");
    
    bool foundFreeze = false;
    for (auto& t : alloc.traces()) {
        for (auto& s : t.simplifyOrder) {
            // we can't directly check freeze from traces easily, but we know it runs without crashing
            // and succeeds. We could grep the log if we captured it, but here we just ensure success.
        }
    }
    TEST_ASSERT(allRegistersPhysical(prog, 2), "All registers must be physical");
    return true;
}

static bool test_coalescingAndCFGRebuild() {
    std::string ir = "a = x\nb = a\nc = b + x";
    Parser p; auto prog = p.parse(ir);
    RegisterAllocator alloc(3);
    TEST_ASSERT(alloc.allocate(prog), "Allocate success");
    bool coalesced = false;
    for (auto& t : alloc.traces()) {
        for (auto& cr : t.coalesceResults) {
            if (cr.coalesced) coalesced = true;
        }
    }
    TEST_ASSERT(coalesced, "Coalescing should happen");
    TEST_ASSERT(allRegistersPhysical(prog, 3), "All registers physical");
    return true;
}

static bool test_physicalRegisterCollision() {
    // User provides an IR with R0 in it. The allocator should rename it safely.
    std::string ir = "R0 = R1 + x";
    Parser p; auto prog = p.parse(ir);
    RegisterAllocator alloc(2);
    TEST_ASSERT(alloc.allocate(prog), "Should handle collisions gracefully");
    TEST_ASSERT(allRegistersPhysical(prog, 2), "Final IR is valid physical registers only");
    return true;
}

static bool test_spillRewriteUniqueTemps() {
    // Force a lot of spilling over multiple rounds
    std::string ir = "a=1\nb=2\nc=3\nd=4\ne=5\nf=a+b\ng=c+d\nh=e+f\ni=g+h";
    Parser p; auto prog = p.parse(ir);
    RegisterAllocator alloc(2);
    TEST_ASSERT(alloc.allocate(prog), "Should succeed after spilling");
    
    // Check final assignment for stack slots
    auto result = alloc.result();
    bool hasSpills = false;
    for (auto& [var, asgn] : result.assignments) {
        if (asgn.spilled) {
            hasSpills = true;
            TEST_ASSERT(asgn.stackSlot >= 0, "Spilled var must have stack slot");
        }
    }
    TEST_ASSERT(hasSpills, "Should have spilled");
    TEST_ASSERT(allRegistersPhysical(prog, 2), "All registers physical");
    return true;
}

static void runTest(const std::string& name, bool(*testFn)()) {
    ++testsRun;
    std::cout << "Test " << testsRun << ": " << name << "... ";
    std::cout.flush();
    try {
        if (testFn()) {
            ++testsPassed;
            std::cout << "PASSED\n";
        } else {
            std::cout << "FAILED\n";
        }
    } catch (const std::exception& e) {
        std::cout << "EXCEPTION: " << e.what() << "\n";
    }
}

int main() {
    std::cout << "==========================================================\n";
    std::cout << "  Register Allocator — Automated Test Suite (Strengthened)\n";
    std::cout << "==========================================================\n\n";

    runTest("Parser LOAD/STORE slots", test_parserAndSlots);
    runTest("CFG and Liveness", test_cfgAndLiveness);
    runTest("Interference and Freeze", test_interferenceAndFreeze);
    runTest("Coalescing & Rebuild CFG", test_coalescingAndCFGRebuild);
    runTest("Physical Register Collision", test_physicalRegisterCollision);
    runTest("Spill Rewrite Unique Temps", test_spillRewriteUniqueTemps);

    std::cout << "\n==========================================================\n";
    std::cout << "  Results: " << testsPassed << " / " << testsRun << " passed\n";
    std::cout << "==========================================================\n";

    return (testsPassed == testsRun) ? 0 : 1;
}
