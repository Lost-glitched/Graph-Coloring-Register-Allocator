#pragma once
// ============================================================================
// MoveGraph.h — Track move-related pairs in the IR
// ============================================================================

#include <string>
#include <vector>
#include <utility>
#include <unordered_set>

namespace regalloc {

/// A single move-related pair (src → dest).
struct MovePair {
    std::string src;
    std::string dest;
};

class MoveGraph {
public:
    /// Register a move instruction: dest = src.
    void addMove(const std::string& src, const std::string& dest);

    /// All registered move pairs.
    const std::vector<MovePair>& moves() const { return moves_; }

    /// Whether `v` appears in any move pair.
    bool isMoveRelated(const std::string& v) const;

    /// Clear all moves.
    void clear();

private:
    std::vector<MovePair> moves_;
    std::unordered_set<std::string> moveRelated_;
};

} // namespace regalloc
