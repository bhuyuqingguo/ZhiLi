#pragma once

#include "document.h"

// Detach a complete subtree, including nodes hidden under collapsed topics.
// An empty root means the requested branch is missing or malformed.
inline Document extractBranch(const Document &source, const QString &branchRoot) {
    if (branchRoot.isEmpty()) return {};

    Document branch;
    branch.root = branchRoot;
    branch.mindMap = source.mindMap;
    branch.branchDirection = source.branchDirection;
    branch.connectorStyle = source.connectorStyle;

    QVector<QString> pending{branchRoot};
    while (!pending.isEmpty()) {
        const QString id = pending.takeLast();
        const auto node = source.nodes.constFind(id);
        if (node == source.nodes.cend() || branch.nodes.contains(id)) return {};

        branch.nodes.insert(id, node.value());
        for (const auto &child : node->children) pending.append(child);
    }
    if (branchRoot != source.root) branch.nodes[branchRoot].rootLane = BranchDirection::Both;
    branch.floating = source.floating;
    branch.relationships = source.relationships;
    branch.boundaries = source.boundaries;
    branch.pruneDecorations();
    return branch;
}
