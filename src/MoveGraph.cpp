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

void MoveGraph::clear() {
    moves_.clear();
    moveRelated_.clear();
}

} // namespace regalloc
