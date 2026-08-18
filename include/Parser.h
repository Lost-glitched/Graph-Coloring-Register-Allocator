#pragma once
// ============================================================================
// Parser.h — Simple 3AC Parser
// ============================================================================
// Parses a textual three-address-code program into an IRProgram.
//
// Supported grammar:
//   instruction ::= assignment | load | store | goto | branch | label
//   assignment  ::= IDENT '=' IDENT (OP IDENT)?
//   load        ::= 'LOAD' IDENT ',' '[' IDENT ']'
//   store       ::= 'STORE' IDENT ',' '[' IDENT ']'
//   goto        ::= 'GOTO' IDENT
//   branch      ::= 'IF' IDENT 'GOTO' IDENT
//   label       ::= 'LABEL' IDENT
//   OP          ::= '+' | '-' | '*' | '/'
//
// Lines starting with '#' or empty lines are ignored (comments / whitespace).
// ============================================================================

#include "IRProgram.h"
#include <string>
#include <stdexcept>

namespace regalloc {

class Parser {
public:
    /// Parse the given source text and return an IRProgram.
    /// Throws std::runtime_error on syntax errors.
    IRProgram parse(const std::string& source) const;

private:
    /// Parse a single non-empty, non-comment line.
    Instruction parseLine(const std::string& line, int lineNumber) const;
};

} // namespace regalloc
