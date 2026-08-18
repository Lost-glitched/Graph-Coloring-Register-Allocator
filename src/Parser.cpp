// ============================================================================
// Parser.cpp — 3AC parser implementation
// ============================================================================

#include "Parser.h"
#include <sstream>
#include <vector>
#include <algorithm>
#include <cctype>

namespace regalloc {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

static bool isOperator(const std::string& s) {
    return s == "+" || s == "-" || s == "*" || s == "/";
}

static Opcode opFromChar(const std::string& s) {
    if (s == "+") return Opcode::ADD;
    if (s == "-") return Opcode::SUB;
    if (s == "*") return Opcode::MUL;
    if (s == "/") return Opcode::DIV;
    throw std::runtime_error("Unknown operator: " + s);
}

/// Strip surrounding brackets: "[foo]" -> "foo"
static std::string stripBrackets(const std::string& s) {
    std::string r = s;
    if (!r.empty() && r.front() == '[') r.erase(r.begin());
    if (!r.empty() && r.back()  == ']') r.pop_back();
    // Also strip trailing comma that may precede the bracket token
    if (!r.empty() && r.back()  == ',') r.pop_back();
    return r;
}

// ---------------------------------------------------------------------------
// parseLine
// ---------------------------------------------------------------------------
Instruction Parser::parseLine(const std::string& rawLine, int lineNumber) const {
    std::string line = trim(rawLine);
    auto tokens = tokenize(line);

    if (tokens.empty()) {
        throw std::runtime_error("Line " + std::to_string(lineNumber) + ": empty instruction");
    }

    // --- LABEL ---
    if (tokens[0] == "LABEL") {
        if (tokens.size() < 2)
            throw std::runtime_error("Line " + std::to_string(lineNumber) +
                                     ": LABEL requires a name");
        return Instruction::makeLabel(tokens[1]);
    }

    // --- GOTO ---
    if (tokens[0] == "GOTO") {
        if (tokens.size() < 2)
            throw std::runtime_error("Line " + std::to_string(lineNumber) +
                                     ": GOTO requires a label");
        return Instruction::makeGoto(tokens[1]);
    }

    // --- IF cond GOTO label ---
    if (tokens[0] == "IF") {
        if (tokens.size() < 4 || tokens[2] != "GOTO")
            throw std::runtime_error("Line " + std::to_string(lineNumber) +
                                     ": expected IF <cond> GOTO <label>");
        return Instruction::makeBranch(tokens[1], tokens[3]);
    }

    // --- LOAD dest, [slot] ---
    if (tokens[0] == "LOAD") {
        if (tokens.size() < 3)
            throw std::runtime_error("Line " + std::to_string(lineNumber) +
                                     ": LOAD requires dest and [slot]");
        // dest may have trailing comma: "a," -> "a"
        std::string dest = tokens[1];
        if (!dest.empty() && dest.back() == ',') dest.pop_back();
        std::string slot = stripBrackets(tokens[2]);
        int slotIdx = -1;
        if (slot.find("stack_slot_") == 0) {
            slotIdx = std::stoi(slot.substr(11));
        } else {
            try { slotIdx = std::stoi(slot); } catch (...) {}
        }
        return Instruction::makeLoad(dest, slotIdx);
    }

    // --- STORE src, [slot] ---
    if (tokens[0] == "STORE") {
        if (tokens.size() < 3)
            throw std::runtime_error("Line " + std::to_string(lineNumber) +
                                     ": STORE requires src and [slot]");
        std::string src = tokens[1];
        if (!src.empty() && src.back() == ',') src.pop_back();
        std::string slot = stripBrackets(tokens[2]);
        int slotIdx = -1;
        if (slot.find("stack_slot_") == 0) {
            slotIdx = std::stoi(slot.substr(11));
        } else {
            try { slotIdx = std::stoi(slot); } catch (...) {}
        }
        return Instruction::makeStore(src, slotIdx);
    }

    // --- Assignment: dest = src1 (op src2)? ---
    if (tokens.size() >= 3 && tokens[1] == "=") {
        std::string dest = tokens[0];
        std::string src1 = tokens[2];

        if (tokens.size() == 3) {
            // Simple copy: dest = src1
            return Instruction::makeMov(dest, src1);
        }
        if (tokens.size() >= 5 && isOperator(tokens[3])) {
            // Arithmetic: dest = src1 op src2
            Opcode op = opFromChar(tokens[3]);
            std::string src2 = tokens[4];
            return Instruction::makeArith(op, dest, src1, src2);
        }
        throw std::runtime_error("Line " + std::to_string(lineNumber) +
                                 ": malformed assignment");
    }

    throw std::runtime_error("Line " + std::to_string(lineNumber) +
                             ": unrecognized instruction: " + line);
}

// ---------------------------------------------------------------------------
// parse
// ---------------------------------------------------------------------------
IRProgram Parser::parse(const std::string& source) const {
    IRProgram program;
    std::istringstream iss(source);
    std::string line;
    int lineNumber = 0;

    while (std::getline(iss, line)) {
        ++lineNumber;
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue; // skip blanks & comments
        program.addInstruction(parseLine(trimmed, lineNumber));
    }
    return program;
}

} // namespace regalloc
