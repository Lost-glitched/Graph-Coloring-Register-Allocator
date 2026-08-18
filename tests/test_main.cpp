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

// ----------------------------------------------------------------------------
// 23. Final Validation
// ----------------------------------------------------------------------------
static bool validateFinalAllocation(const IRProgram& originalProgram, const IRProgram& finalProgram, const RegisterAllocator& alloc, int K) {
    auto& result = alloc.result();
    
    std::set<std::string> physNames;
    for (int i = 0; i < K; ++i) physNames.insert("R" + std::to_string(i));

    // 1. Every register in final IR must be physical
    for (auto& instr : finalProgram.instructions) {
        if (!instr.dest.empty()) {
            TEST_ASSERT(physNames.count(instr.dest) > 0, "Final IR contains virtual dest: " + instr.dest);
        }
        if (!instr.src1.empty() && instr.opcode != Opcode::GOTO) {
            if (instr.opcode == Opcode::BRANCH || instr.opcode != Opcode::LABEL) {
                TEST_ASSERT(physNames.count(instr.src1) > 0, "Final IR contains virtual src1: " + instr.src1);
            }
        }
        if (!instr.src2.empty()) {
            TEST_ASSERT(physNames.count(instr.src2) > 0, "Final IR contains virtual src2: " + instr.src2);
        }
    }
    
    // 2. Original virtual registers interference check
    // We build the interference graph of the original program.
    ControlFlowGraph cfg; cfg.build(originalProgram);
    LivenessAnalyzer liveness; liveness.analyze(cfg);
    
    // Check interference on all vars (before spill/coalesce)
    size_t globalIdx = 0;
    for (auto& bb : cfg.blocks()) {
        for (auto& instr : bb->instructions) {
            const auto& il = liveness.getInstrLiveness(globalIdx);
            for (auto& d : instr.getDefs()) {
                for (auto& v : il.liveOut) {
                    if (d == v) continue;
                    if (instr.isMove() && v == instr.src1) continue; // MOV exception
                    
                    // d and v interfere. If neither spilled, their physRegs MUST differ.
                    auto itD = result.assignments.find(d);
                    auto itV = result.assignments.find(v);
                    
                    if (itD != result.assignments.end() && itV != result.assignments.end()) {
                        if (!itD->second.spilled && !itV->second.spilled) {
                            TEST_ASSERT(itD->second.physReg != itV->second.physReg, 
                                "Interfering variables " + d + " and " + v + " assigned same register R" + std::to_string(itD->second.physReg));
                        }
                    }
                }
            }
            ++globalIdx;
        }
    }

    // 3. Spilled vars have stack slots
    for (auto& [var, asgn] : result.assignments) {
        if (asgn.spilled) {
            TEST_ASSERT(asgn.stackSlot >= 0, "Spilled variable missing stack slot: " + var);
            TEST_ASSERT(asgn.physReg == -1, "Spilled variable has phys reg: " + var);
        } else {
            TEST_ASSERT(asgn.physReg >= 0 && asgn.physReg < K, "Colored variable has invalid phys reg: " + var);
        }
    }
    
    return true;
}

// ----------------------------------------------------------------------------
// 1. Basic parser
// ----------------------------------------------------------------------------
static bool test_basicParser() {
    std::string ir = "a = b + c\nLABEL L1\nGOTO L1\nIF a GOTO L1\n";
    Parser p; auto prog = p.parse(ir);
    TEST_ASSERT(prog.instructions.size() == 4, "Should parse 4 instructions");
    TEST_ASSERT(prog.instructions[0].opcode == Opcode::ADD, "ADD opcode");
    TEST_ASSERT(prog.instructions[0].dest == "a", "dest a");
    TEST_ASSERT(prog.instructions[1].opcode == Opcode::LABEL, "LABEL opcode");
    TEST_ASSERT(prog.instructions[2].opcode == Opcode::GOTO, "GOTO opcode");
    TEST_ASSERT(prog.instructions[3].opcode == Opcode::BRANCH, "BRANCH opcode");
    return true;
}

// ----------------------------------------------------------------------------
// 2. LOAD/STORE parser
// ----------------------------------------------------------------------------
static bool test_loadStoreParser() {
    std::string ir = "LOAD dest, [12]\nSTORE src, [stack_slot_42]\n";
    Parser p; auto prog = p.parse(ir);
    TEST_ASSERT(prog.instructions.size() == 2, "2 instructions");
    TEST_ASSERT(prog.instructions[0].opcode == Opcode::LOAD, "Is LOAD");
    TEST_ASSERT(prog.instructions[0].slotIndex == 12, "Slot 12");
    TEST_ASSERT(prog.instructions[1].opcode == Opcode::STORE, "Is STORE");
    TEST_ASSERT(prog.instructions[1].slotIndex == 42, "Slot 42");
    return true;
}

// ----------------------------------------------------------------------------
// 3. Invalid parser input
// ----------------------------------------------------------------------------
static bool test_invalidParser() {
    Parser p;
    bool caught1 = false, caught2 = false, caught3 = false;
    try { p.parse("LOAD x, [12oops]"); } catch (const std::exception&) { caught1 = true; }
    try { p.parse("GOTO L1 garbage"); } catch (const std::exception&) { caught2 = true; }
    try { p.parse("a=1"); }             catch (const std::exception&) { caught3 = true; }
    
    TEST_ASSERT(caught1, "Caught invalid slot");
    TEST_ASSERT(caught2, "Caught extra tokens");
    TEST_ASSERT(caught3, "Caught malformed assignment without spaces");
    return true;
}

// ----------------------------------------------------------------------------
// 4. CFG construction
// ----------------------------------------------------------------------------
static bool test_cfgConstruction() {
    std::string ir = "a = 1\nIF a GOTO L1\nb = 2\nGOTO L2\nLABEL L1\nb = 3\nLABEL L2\nc = 4\n";
    Parser p; auto prog = p.parse(ir);
    ControlFlowGraph cfg; cfg.build(prog);
    
    const auto& blocks = cfg.blocks();
    TEST_ASSERT(blocks.size() >= 5, "CFG contains the expected leaders");
    for (size_t i = 0; i < blocks.size(); ++i) {
        const auto& instructions = blocks[i]->instructions;
        TEST_ASSERT(!instructions.empty(), "CFG blocks are non-empty");
        const auto& last = instructions.back();
        if (last.opcode == Opcode::BRANCH) {
            TEST_ASSERT(blocks[i]->successors.size() == 2, "IF block has 2 successors");
        } else if (last.opcode == Opcode::GOTO) {
            TEST_ASSERT(blocks[i]->successors.size() == 1, "GOTO block has 1 successor");
        } else if (i + 1 == blocks.size()) {
            TEST_ASSERT(blocks[i]->successors.empty(), "End block has 0 successors");
        } else {
            TEST_ASSERT(blocks[i]->successors.size() == 1, "Fallthrough block has 1 successor");
        }
    }
    return true;
}

// ----------------------------------------------------------------------------
// 5. Undefined label detection
// ----------------------------------------------------------------------------
static bool test_undefinedLabel() {
    std::string ir = "GOTO UNKNOWN\n";
    Parser p; auto prog = p.parse(ir);
    ControlFlowGraph cfg;
    bool caught = false;
    try { cfg.build(prog); } catch (const std::exception&) { caught = true; }
    TEST_ASSERT(caught, "Caught undefined label");
    
    ir = "LABEL L1\nLABEL L1\n";
    prog = p.parse(ir);
    caught = false;
    try { cfg.build(prog); } catch (const std::exception&) { caught = true; }
    TEST_ASSERT(caught, "Caught duplicate label");
    
    return true;
}

// ----------------------------------------------------------------------------
// 6. Liveness
// ----------------------------------------------------------------------------
static bool test_liveness() {
    std::string ir = "a = 1\nb = a\nc = b\n";
    Parser p; auto prog = p.parse(ir);
    ControlFlowGraph cfg; cfg.build(prog);
    LivenessAnalyzer liveness; liveness.analyze(cfg);
    
    TEST_ASSERT(liveness.getInstrLiveness(0).liveOut.count("a") > 0, "a live out of instr 0");
    TEST_ASSERT(liveness.getInstrLiveness(0).liveOut.count("b") == 0, "b not live out of instr 0");
    TEST_ASSERT(liveness.getInstrLiveness(1).liveOut.count("b") > 0, "b live out of instr 1");
    TEST_ASSERT(liveness.getInstrLiveness(1).liveOut.count("a") == 0, "a dead after instr 1");
    return true;
}

// ----------------------------------------------------------------------------
// 7. Interference graph
// ----------------------------------------------------------------------------
static bool test_interferenceGraph() {
    std::string ir = "a = 1\nb = 2\nc = a + b\n";
    Parser p; auto prog = p.parse(ir);
    ControlFlowGraph cfg; cfg.build(prog);
    LivenessAnalyzer liveness; liveness.analyze(cfg);
    
    RegisterAllocator alloc(2); // purely to access private methods? No, we can just build one manually
    // Actually, we can just run a partial allocator or check graph manually if we expose it.
    // Wait, ig is built internally. We can just test via allocation success.
    // Let's implement an IG directly.
    InterferenceGraph ig;
    ig.addNode("a"); ig.addNode("b"); ig.addNode("c");
    ig.addEdge("a", "b");
    
    TEST_ASSERT(ig.hasEdge("a", "b"), "Has edge");
    TEST_ASSERT(ig.hasEdge("b", "a"), "Symmetric edge");
    TEST_ASSERT(!ig.hasEdge("a", "c"), "No phantom edge");
    TEST_ASSERT(!ig.hasEdge("a", "a"), "No self edge");
    TEST_ASSERT(ig.degree("a") == 1, "Degree is 1");
    
    auto neighbors = ig.removeNode("a");
    TEST_ASSERT(!ig.hasNode("a"), "Node removed");
    TEST_ASSERT(ig.degree("b") == 0, "Neighbor degree reduced");
    
    ig.restoreNode("a", neighbors);
    TEST_ASSERT(ig.hasNode("a"), "Node restored");
    TEST_ASSERT(ig.degree("b") == 1, "Neighbor degree restored");
    
    return true;
}

// ----------------------------------------------------------------------------
// 8. MOVE interference exception
// ----------------------------------------------------------------------------
static bool test_moveException() {
    std::string ir = "a = 1\nb = a\n"; 
    // liveness: a is live out of 0. b is live out of 1.
    // at 'b = a', b is def, a is live-in, but is it live-out? No.
    // Let's force them both to be live-out to test the exception:
    std::string ir2 = "a = 1\nb = a\nc = a + b\n";
    // at 'b = a', 'a' is still live out! Because it's used in 'c = a + b'.
    // Normally 'b' and 'a' would interfere since 'a' is liveOut and 'b' is def.
    // BUT due to MOVE exception, they shouldn't interfere on this instruction.
    Parser p; auto prog = p.parse(ir2);
    RegisterAllocator alloc(2);
    TEST_ASSERT(alloc.allocate(prog), "Allocation succeeded"); // Should coalesce a and b because they don't interfere
    
    bool coalesced = false;
    for (auto& t : alloc.traces()) {
        for (auto& cr : t.coalesceResults) {
            if (cr.a == "a" && cr.b == "b" && cr.coalesced) coalesced = true;
            if (cr.b == "a" && cr.a == "b" && cr.coalesced) coalesced = true;
        }
    }
    TEST_ASSERT(coalesced, "a and b coalesced thanks to MOV exception");
    return true;
}

// ----------------------------------------------------------------------------
// 9. Safe coalescing
// ----------------------------------------------------------------------------
static bool test_safeCoalescing() {
    std::string ir = "a = 1\nb = a\nc = b\n";
    Parser p; auto prog = p.parse(ir);
    RegisterAllocator alloc(3);
    TEST_ASSERT(alloc.allocate(prog), "Allocation succeeded");
    
    bool coalesced = false;
    for (auto& t : alloc.traces()) {
        for (auto& cr : t.coalesceResults) {
            if (cr.coalesced) coalesced = true;
        }
    }
    TEST_ASSERT(coalesced, "Safe coalescing occurred");
    TEST_ASSERT(!alloc.result().aliases.empty(), "Coalesced alias was reported");
    TEST_ASSERT(alloc.result().toString(3).find(" -> ") != std::string::npos,
        "Allocation output includes coalesced alias mapping");
    return true;
}

// ----------------------------------------------------------------------------
// 10. Unsafe coalescing
// ----------------------------------------------------------------------------
static bool test_unsafeCoalescing() {
    // Make K=2. We want merging a and b to result in node with >=2 neighbors of degree >= 2.
    // a and b are move related.
    // neighbors of a: x, y
    // neighbors of b: x, y
    // If a and b merge, merged node has neighbors x, y.
    // If x and y have degree >= 2, Briggs fails.
    std::string ir = R"(
x = 1
y = 2
a = 3
b = a
c = a + x
d = b + y
e = c + d
)";
    Parser p; auto prog = p.parse(ir);
    RegisterAllocator alloc(2);
    TEST_ASSERT(alloc.allocate(prog), "Allocation succeeded");
    
    bool rejected = false;
    for (auto& t : alloc.traces()) {
        for (auto& cr : t.coalesceResults) {
            if (!cr.coalesced && (cr.a == "a" || cr.a == "b")) rejected = true;
        }
    }
    TEST_ASSERT(rejected, "Unsafe coalescing was correctly rejected");
    return true;
}

// ----------------------------------------------------------------------------
// 11. Freeze
// ----------------------------------------------------------------------------
static bool test_freeze() {
    // We need a move-related node with degree < K, but coalescing is unsafe.
    // Actually, if coalescing is unsafe, it's rejected. Then Simplify runs.
    // If NO node has degree < K and is non-move-related, Freeze must trigger.
    // Let's create a graph where every node is move-related to something, 
    // and K is small, but some node has degree < K.
    std::string ir = R"(
a = 1
b = a
c = 2
d = c
e = a + c
f = b + d
)";
    Parser p; auto prog = p.parse(ir);
    RegisterAllocator alloc(2);
    TEST_ASSERT(alloc.allocate(prog), "Allocation succeeded");
    
    bool froze = false;
    for (auto& t : alloc.traces()) {
        if (!t.freezeOrder.empty()) froze = true;
    }
    TEST_ASSERT(froze, "Freeze phase was reached and executed");
    return true;
}

// ----------------------------------------------------------------------------
// 12. No-spill coloring
// ----------------------------------------------------------------------------
static bool test_noSpillColoring() {
    std::string ir = "a = 1\nb = 2\nc = a + b\n";
    Parser p; auto prog = p.parse(ir);
    RegisterAllocator alloc(3);
    TEST_ASSERT(alloc.allocate(prog), "Allocation succeeded");
    
    TEST_ASSERT(alloc.result().totalStackSlots == 0, "0 spills");
    TEST_ASSERT(validateFinalAllocation(prog, prog, alloc, 3), "Validation passed");
    return true;
}

// ----------------------------------------------------------------------------
// 13. Optimistic candidate successfully colored
// ----------------------------------------------------------------------------
static bool test_optimisticColored() {
    // K=2. We need a node with degree >= 2 (so it's selected as optimistic spill),
    // but its neighbors get assigned the SAME color, leaving a color free for it.
    std::string ir = R"(
a = 1
b = 2
c = 3
d = a + b
e = a + c
f = b + c
)";
    // Wait, complete graph K3 requires 3 colors. If K=2, it WILL spill.
    // We need a star graph: a in center, b and c on outside. b and c don't interfere.
    std::string ir2 = R"(
b = 1
c = 2
a = 3
d = a + b
e = a + c
)";
    // 'a' interferes with 'b' and 'c'. 'b' and 'c' do not interfere.
    // With K=2: degree(a) = 2. So 'a' might be optimistically spilled.
    // But 'b' and 'c' can both get R0. Then 'a' can get R1!
    Parser p; auto prog = p.parse(ir2);
    RegisterAllocator alloc(2);
    TEST_ASSERT(alloc.allocate(prog), "Allocation succeeded");
    
    bool optimisticSuccess = false;
    for (auto& t : alloc.traces()) {
        for (auto& cand : t.optimisticSpillOrder) {
                if (t.actualSpills.find(cand) == t.actualSpills.end() &&
                     t.selectAssignment.find(cand) != t.selectAssignment.end()) {
                optimisticSuccess = true;
            }
        }
    }
    TEST_ASSERT(optimisticSuccess, "Optimistic candidate was successfully colored");
    return true;
}

// ----------------------------------------------------------------------------
// 14. Actual spill
// ----------------------------------------------------------------------------
static bool test_actualSpill() {
    // Complete graph K3 with K=2. MUST spill.
    std::string ir = R"(
a = 1
b = 2
c = 3
d = a + b
e = b + c
f = c + a
)";
    Parser p; auto prog = p.parse(ir);
    RegisterAllocator alloc(2);
    TEST_ASSERT(alloc.allocate(prog), "Allocation succeeded");
    
    bool actualSpill = false;
    for (auto& t : alloc.traces()) {
        if (!t.actualSpills.empty()) actualSpill = true;
    }
    TEST_ASSERT(actualSpill, "Actual spill occurred");
    return true;
}

// ----------------------------------------------------------------------------
// 15. Spill rewrite
// ----------------------------------------------------------------------------
static bool test_spillRewrite() {
    // Verify that the final IR contains LOAD/STORE
    std::string ir = R"(
a = 1
b = 2
c = 3
d = a + b
e = b + c
f = c + a
)";
    Parser p; auto originalProg = p.parse(ir);
    auto prog = originalProg;
    RegisterAllocator alloc(2);
    TEST_ASSERT(alloc.allocate(prog), "Allocation succeeded");
    
    bool hasLoadStore = false;
    for (auto& instr : prog.instructions) {
        if (instr.opcode == Opcode::LOAD || instr.opcode == Opcode::STORE) {
            hasLoadStore = true;
        }
    }
    TEST_ASSERT(hasLoadStore, "IR was rewritten with LOAD/STORE");
    TEST_ASSERT(validateFinalAllocation(originalProg, prog, alloc, 2), "Validation passed");
    return true;
}

// ----------------------------------------------------------------------------
// 16. Multiple spill rounds & Persistent Spill Temps
// ----------------------------------------------------------------------------
static bool test_multipleSpillRounds() {
    // High pressure to force multiple rounds
    std::string ir = "a = 1\nb = 2\nc = 3\nd = a + b\ne = b + c\nf = c + a\n";
    Parser p; auto originalProg = p.parse(ir);
    auto prog = originalProg;
    RegisterAllocator alloc(2);
    TEST_ASSERT(alloc.allocate(prog), "Allocation succeeded");
    
        int actualSpillRounds = 0;
        for (const auto& trace : alloc.traces()) {
            if (!trace.actualSpills.empty()) ++actualSpillRounds;
        }
        TEST_ASSERT(actualSpillRounds >= 2, "Took multiple actual spill rounds");
    
    // Check for unique spill temps (v.spill.0, v.spill.1 etc.)
    std::set<std::string> seenTemps;
    for (const auto& trace : alloc.traces()) {
        for (const auto& temp : trace.spillTemps) {
            TEST_ASSERT(seenTemps.insert(temp).second,
                "Spill temporary was reused: " + temp);
        }
    }
    TEST_ASSERT(!seenTemps.empty(), "Spill rewriting created temporaries");
    TEST_ASSERT(validateFinalAllocation(originalProg, prog, alloc, 2), "Validation passed");
    return true;
}

// ----------------------------------------------------------------------------
// 17. Physical-register-name collision
// ----------------------------------------------------------------------------
static bool test_physRegCollision() {
    std::string ir = "R0 = 1\nR1 = 2\na = R0 + R1\n";
    Parser p; auto originalProg = p.parse(ir);
    auto prog = originalProg;
    RegisterAllocator alloc(2);
    TEST_ASSERT(alloc.allocate(prog), "Allocation succeeded");
    
    // Original program used R0/R1. Allocator must have sanitized them.
    TEST_ASSERT(alloc.result().assignments.count("_user_R0") == 1,
        "R0 collision was sanitized and assigned");
    TEST_ASSERT(alloc.result().assignments.count("_user_R1") == 1,
        "R1 collision was sanitized and assigned");
    TEST_ASSERT(validateFinalAllocation(originalProg, prog, alloc, 2), "Validation passed with collisions safely handled");
    return true;
}

// ----------------------------------------------------------------------------
// 18. Different K values
// ----------------------------------------------------------------------------
static bool test_differentK() {
    std::string ir = "a = 1\nb = a\nc = b\n";
    Parser p;
    auto prog1 = p.parse(ir);
    RegisterAllocator alloc3(3);
    TEST_ASSERT(alloc3.allocate(prog1), "K=3 allocation succeeded");
    TEST_ASSERT(alloc3.result().totalStackSlots == 0, "K=3 has no spills");
    
    auto prog2 = p.parse(
        "a = 1\nb = 2\nc = 3\nd = a + b\ne = b + c\nf = c + a\n");
    RegisterAllocator alloc2(2);
    TEST_ASSERT(alloc2.allocate(prog2), "K=2 allocation succeeded");
    TEST_ASSERT(alloc2.result().totalStackSlots > 0, "K=2 has spills");
    return true;
}

// ----------------------------------------------------------------------------
// 19. Loop-containing program
// ----------------------------------------------------------------------------
static bool test_loopProgram() {
    std::string ir = R"(
i = 0
LABEL L1
IF i GOTO L2
i = i + 1
GOTO L1
LABEL L2
)";
    Parser p; auto originalProg = p.parse(ir);
    auto prog = originalProg;
    RegisterAllocator alloc(3);
    TEST_ASSERT(alloc.allocate(prog), "Allocation succeeded");
    TEST_ASSERT(validateFinalAllocation(originalProg, prog, alloc, 3), "Loop validation passed");
    return true;
}

// ----------------------------------------------------------------------------
// 20. Final allocation invariant validation
// ----------------------------------------------------------------------------
static bool test_finalValidationDirect() {
    // This is already checked implicitly by validateFinalAllocation in other tests.
    // Let's just do one explicitly complex one.
    std::string ir = "a = 1\nb = 2\nc = 3\nd = 4\ne = a + b\nf = c + d\ng = e + f\n";
    Parser p; auto originalProg = p.parse(ir);
    auto prog = originalProg;
    RegisterAllocator alloc(2);
    TEST_ASSERT(alloc.allocate(prog), "Allocation succeeded");
    TEST_ASSERT(validateFinalAllocation(originalProg, prog, alloc, 2), "Final invariants strictly hold");
    return true;
}

// ============================================================================
// Test runner
// ============================================================================
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
    std::cout << "  Register Allocator — Automated Test Suite (Strict)\n";
    std::cout << "==========================================================\n\n";

    runTest("1. Basic parser", test_basicParser);
    runTest("2. LOAD/STORE parser", test_loadStoreParser);
    runTest("3. Invalid parser input", test_invalidParser);
    runTest("4. CFG construction", test_cfgConstruction);
    runTest("5. Undefined label detection", test_undefinedLabel);
    runTest("6. Liveness", test_liveness);
    runTest("7. Interference graph", test_interferenceGraph);
    runTest("8. MOVE interference exception", test_moveException);
    runTest("9. Safe coalescing", test_safeCoalescing);
    runTest("10. Unsafe coalescing", test_unsafeCoalescing);
    runTest("11. Freeze", test_freeze);
    runTest("12. No-spill coloring", test_noSpillColoring);
    runTest("13. Optimistic candidate successfully colored", test_optimisticColored);
    runTest("14. Actual spill", test_actualSpill);
    runTest("15. Spill rewrite", test_spillRewrite);
    runTest("16. Multiple spill rounds", test_multipleSpillRounds);
    runTest("17. Physical-register-name collision", test_physRegCollision);
    runTest("18. Different K values", test_differentK);
    runTest("19. Loop-containing program", test_loopProgram);
    runTest("20. Final allocation invariant validation", test_finalValidationDirect);

    std::cout << "\n==========================================================\n";
    std::cout << "  Results: " << testsPassed << " / " << testsRun << " passed\n";
    std::cout << "==========================================================\n";

    return (testsPassed == testsRun) ? 0 : 1;
}
