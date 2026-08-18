// ============================================================================
// MoveGraph.cpp
// ============================================================================

#include "MoveGraph.h"

namespace regalloc {

void MoveGraph::addMove(const std::string& src, const std::string& dest) {
    moves_.push_back({src, dest});
    moveRelated_.insert(src);
    moveRelated_.insert(dest);
}

bool MoveGraph::isMoveRelated(const std::string& v) const {
    return moveRelated_.count(v) > 0;
}

void MoveGraph::removeMovesFor(const std::string& v) {
    auto it = std::remove_if(moves_.begin(), moves_.end(),
        [&](const MovePair& mp) {
            return mp.src == v || mp.dest == v;
        });
    moves_.erase(it, moves_.end());

    // Recompute moveRelated_ from remaining moves
    moveRelated_.clear();
    for (auto& mp : moves_) {
        moveRelated_.insert(mp.src);
        moveRelated_.insert(mp.dest);
    }
}

void MoveGraph::clear() {
    moves_.clear();
    moveRelated_.clear();
}

} // namespace regalloc
