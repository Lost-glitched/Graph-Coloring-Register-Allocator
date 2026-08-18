// ============================================================================
// InterferenceGraph.cpp — Interference graph implementation
// ============================================================================

#include "InterferenceGraph.h"
#include <sstream>
#include <algorithm>

namespace regalloc {

void InterferenceGraph::addNode(const std::string& v) {
    if (adj_.find(v) == adj_.end()) {
        adj_[v] = {};
    }
}

void InterferenceGraph::addEdge(const std::string& a, const std::string& b) {
    if (a == b) return;                              // no self-edges
    addNode(a);
    addNode(b);
    adj_[a].insert(b);
    adj_[b].insert(a);
}

std::unordered_set<std::string> InterferenceGraph::removeNode(const std::string& v) {
    auto it = adj_.find(v);
    if (it == adj_.end()) return {};

    auto neighbors = it->second;

    // Remove v from every neighbor's adjacency list
    for (auto& n : neighbors) {
        adj_[n].erase(v);
    }

    // Remove v itself
    adj_.erase(it);

    return neighbors;
}

void InterferenceGraph::restoreNode(
    const std::string& v,
    const std::unordered_set<std::string>& originalNeighbors) {

    adj_[v] = {};

    // Only restore edges to neighbors that are currently active
    for (auto& n : originalNeighbors) {
        if (adj_.find(n) != adj_.end()) {
            adj_[v].insert(n);
            adj_[n].insert(v);
        }
    }
}

int InterferenceGraph::degree(const std::string& v) const {
    auto it = adj_.find(v);
    if (it == adj_.end()) return 0;
    return static_cast<int>(it->second.size());
}

std::unordered_set<std::string> InterferenceGraph::neighbors(const std::string& v) const {
    auto it = adj_.find(v);
    if (it == adj_.end()) return {};
    return it->second;
}

std::set<std::string> InterferenceGraph::activeNodes() const {
    std::set<std::string> nodes;
    for (auto& [k, _] : adj_) nodes.insert(k);
    return nodes;
}

bool InterferenceGraph::hasEdge(const std::string& a, const std::string& b) const {
    auto it = adj_.find(a);
    if (it == adj_.end()) return false;
    return it->second.count(b) > 0;
}

bool InterferenceGraph::hasNode(const std::string& v) const {
    return adj_.find(v) != adj_.end();
}

void InterferenceGraph::mergeNodes(const std::string& a, const std::string& b) {
    // Transfer all of b's edges to a (except the edge a-b itself)
    auto itB = adj_.find(b);
    if (itB == adj_.end()) return;

    auto bNeighbors = itB->second;
    for (auto& n : bNeighbors) {
        if (n == a) continue;       // skip the a-b edge itself
        adj_[n].erase(b);           // remove b from neighbor
        addEdge(a, n);              // add a-n edge (no-op if already exists)
    }

    // Remove the a-b edge from a's adjacency
    adj_[a].erase(b);

    // Remove b entirely
    adj_.erase(b);
}

std::string InterferenceGraph::toString() const {
    std::ostringstream os;
    os << "=== Interference Graph ===\n";
    auto nodes = activeNodes();   // sorted
    for (auto& n : nodes) {
        auto nbrs = neighbors(n);
        std::vector<std::string> sorted(nbrs.begin(), nbrs.end());
        std::sort(sorted.begin(), sorted.end());
        os << "  " << n << ":";
        for (auto& nb : sorted) os << " " << nb;
        os << "\n";
    }
    return os.str();
}

} // namespace regalloc
