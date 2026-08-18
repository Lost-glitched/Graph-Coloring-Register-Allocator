#pragma once
// ============================================================================
// InterferenceGraph.h — Undirected interference graph
// ============================================================================
// One node per virtual register. An edge (a, b) means a and b are
// simultaneously live and therefore cannot share a physical register.
//
// Supports dynamic node removal/restoration for the simplify/select phases.
// ============================================================================

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <set>

namespace regalloc {

class InterferenceGraph {
public:
    /// Add a node for variable `v` (idempotent).
    void addNode(const std::string& v);

    /// Add an undirected interference edge between `a` and `b`.
    /// No self-edges, no duplicates.
    void addEdge(const std::string& a, const std::string& b);

    /// Temporarily remove a node and all its edges from the "active" graph.
    /// Returns the set of neighbors at the time of removal (for later restoration).
    std::unordered_set<std::string> removeNode(const std::string& v);

    /// Restore a previously removed node with its original edges.
    void restoreNode(const std::string& v,
                     const std::unordered_set<std::string>& originalNeighbors);

    /// Degree of a node in the active graph.
    int degree(const std::string& v) const;

    /// Neighbors of a node in the active graph.
    std::unordered_set<std::string> neighbors(const std::string& v) const;

    /// All currently active nodes.
    std::set<std::string> activeNodes() const;

    /// Check if an edge exists in the active graph.
    bool hasEdge(const std::string& a, const std::string& b) const;

    /// Total number of active nodes.
    size_t activeNodeCount() const { return adj_.size(); }

    /// Merge node `b` into node `a` (for coalescing).
    /// All of b's edges are transferred to a. b is removed.
    void mergeNodes(const std::string& a, const std::string& b);

    /// Check if a node exists and is active.
    bool hasNode(const std::string& v) const;

    /// Pretty-print adjacency lists.
    std::string toString() const;

private:
    // Active adjacency map. Removed nodes are erased from this map and their
    // edges are stored in removedEdges_ for potential restoration.
    std::unordered_map<std::string, std::unordered_set<std::string>> adj_;
};

} // namespace regalloc
