// ============================================================================
// SpillRewriter.cpp — Spill rewriting implementation
// ============================================================================
// For each spilled variable v:
//   - Assign v → stack_slot_N  (unique slot per variable)
//   - Scan every instruction:
//       * If v is USED: insert LOAD temp, [slot] before this instruction,
//         replace v with temp in the use.
//       * If v is DEFINED: replace v with temp in the def,
//         insert STORE temp, [slot] after this instruction.
//   - Each temp is unique (v.spill.0, v.spill.1, ...) ensuring short live
//     ranges.
//
// Optimization: if an instruction both defines and uses the same spilled
// variable (e.g., x = x + y), we reuse the same temp for that instruction
// to avoid a redundant STORE+LOAD pair.
// ============================================================================

#include "SpillRewriter.h"
#include <algorithm>

namespace regalloc {

std::string SpillRewriter::freshTemp(const std::string& base, int& tempCounter) {
    return base + ".spill." + std::to_string(tempCounter++);
}

std::unordered_set<std::string> SpillRewriter::rewrite(
    IRProgram& program,
    const std::unordered_set<std::string>& spilledVars,
    int& nextSlot,
    int& tempCounter,
    std::unordered_map<std::string, int>& varToSlot) {

    std::unordered_set<std::string> newTemps;

    // Assign stack slots to spilled variables
    std::vector<std::string> sortedSpilledVars(spilledVars.begin(), spilledVars.end());
    std::sort(sortedSpilledVars.begin(), sortedSpilledVars.end());
    for (const auto& v : sortedSpilledVars) {
        if (varToSlot.find(v) == varToSlot.end()) {
            varToSlot[v] = nextSlot++;
        }
    }

    // Rebuild the instruction list in source order so inserted memory
    // operations stay adjacent to the instruction they support.
    std::vector<Instruction> newInstrs;

    for (size_t i = 0; i < program.instructions.size(); ++i) {
        Instruction instr = program.instructions[i];

        // Skip LABEL, GOTO — they don't use/def virtual registers we'd spill
        if (instr.isLabel() || instr.opcode == Opcode::GOTO) {
            newInstrs.push_back(instr);
            continue;
        }

        // Determine which spilled vars are used / defined in this instruction
        auto uses = instr.getUses();
        auto defs = instr.getDefs();

        // Map: spilled variable → temp name for this instruction
        std::unordered_map<std::string, std::string> spillTempMap;

        // --- Insert LOADs for spilled uses (before the instruction) ---
        for (auto& u : uses) {
            if (spilledVars.count(u) == 0) continue;

            std::string tmp;
            auto existing = spillTempMap.find(u);
            if (existing != spillTempMap.end()) {
                tmp = existing->second; // reuse if we already loaded this var
            } else {
                tmp = freshTemp(u, tempCounter);
                spillTempMap[u] = tmp;
                newTemps.insert(tmp);

                // Insert LOAD tmp, [slot]
                newInstrs.push_back(Instruction::makeLoad(tmp, varToSlot[u]));
            }

            // Replace u with tmp in the instruction
            if (instr.src1 == u) instr.src1 = tmp;
            if (instr.src2 == u) instr.src2 = tmp;
        }

        // --- Handle spilled defs ---
        std::string defSpillTemp;
        std::string defSpillVar;
        for (auto& d : defs) {
            if (spilledVars.count(d) == 0) continue;
            defSpillVar = d;

            // Check if we already have a temp for this var (use+def same var)
            auto existing = spillTempMap.find(d);
            if (existing != spillTempMap.end()) {
                defSpillTemp = existing->second;
            } else {
                defSpillTemp = freshTemp(d, tempCounter);
                spillTempMap[d] = defSpillTemp;
                newTemps.insert(defSpillTemp);
            }

            // Replace d with temp in the instruction
            if (instr.dest == d) instr.dest = defSpillTemp;
        }

        // Emit the (possibly modified) instruction
        newInstrs.push_back(instr);

        // --- Insert STORE after the instruction if there was a spilled def ---
        if (!defSpillTemp.empty()) {
            newInstrs.push_back(
                Instruction::makeStore(defSpillTemp, varToSlot[defSpillVar]));
        }
    }

    program.instructions = std::move(newInstrs);
    return newTemps;
}

} // namespace regalloc
