#include "layout.h"
#include <algorithm>
#include <functional>

static QMap<QString, QPointF> layoutTree(const Document &document, const QMap<QString, QSizeF> &sizes,
                                  QMap<QString, qreal> *leafRails, QVector<qreal> *layerSizes) {
    const auto &nodes = document.nodes;
    const bool classic = document.mindMap;
    QMap<QString, QPointF> positions;
    QMap<QString, qreal> spans;
    QMap<QString, qreal> stackedWidths;
    QVector<qreal> levels;
    const qreal gap = classic ? 24 : 16;
    const qreal levelGap = classic ? 64 : 62;
    if (leafRails) leafRails->clear();

    std::function<qreal(const QString &, int)> measure = [&](const QString &id, int depth) {
        const auto size = sizes.value(id);
        const qreal thickness = classic ? size.width() : size.height();
        if (levels.size() <= depth) levels.append(0);
        levels[depth] = qMax(levels[depth], thickness);
        const Node &node = nodes[id];
        qreal children = 0;
        if (!node.collapsed) {
            // Terminal groups get a column of leaves beside a shared rail.
            // The root keeps a row of main topics in every structure chart.
            const bool stack = leafRails && !classic && depth > 0 && node.children.size() > 1 &&
                std::all_of(node.children.cbegin(), node.children.cend(),
                            [&](const QString &child) { return nodes[child].children.isEmpty(); });
            if (stack) {
                qreal width = 0;
                for (const auto &child : node.children) width = qMax(width, measure(child, depth + 1));
                stackedWidths.insert(id, width);
                return spans[id] = qMax(size.width(), width + 44);
            }
            for (const auto &child : node.children) children += measure(child, depth + 1) + gap;
        }
        return spans[id] = qMax(classic ? size.height() : size.width(), children - gap);
    };
    measure(document.root, 0);
    const auto thicknesses = levels;
    if (layerSizes) *layerSizes = thicknesses;
    qreal edge = levels[0] / 2;
    for (int depth = 1; depth < levels.size(); ++depth) {
        const qreal half = levels[depth] / 2;
        levels[depth] = edge + levelGap + half;
        edge = levels[depth] + half;
    }
    levels[0] = 0;

    std::function<void(const QString &, int, qreal, int)> place;
    auto placeGroup = [&](const QStringList &children, int depth, qreal center, int direction) {
        qreal extent = -gap;
        for (const auto &child : children) extent += spans.value(child) + gap;
        qreal cursor = center - extent / 2;
        for (const auto &child : children) {
            const qreal span = spans.value(child);
            place(child, depth, cursor + span / 2, direction);
            cursor += span + gap;
        }
    };
    place = [&](const QString &id, int depth, qreal center, int direction) {
        const qreal level = levels[depth] * direction;
        positions[id] = classic ? QPointF(level, center) : QPointF(center, level);
        const auto &node = nodes[id];
        if (stackedWidths.contains(id)) {
            const qreal left = center - stackedWidths.value(id) / 2 + 11;
            qreal top = levels.at(depth + 1) - thicknesses.at(depth + 1) / 2;
            for (const auto &child : node.children) {
                const auto size = sizes.value(child);
                positions[child] = QPointF(left + size.width() / 2, top + size.height() / 2);
                if (leafRails) leafRails->insert(child, left - 20);
                top += size.height() + 10;
            }
        } else if (!node.collapsed) {
            placeGroup(node.children, depth + 1, center, direction);
        }
    };
    if (!classic) {
        place(document.root, 0, 0, 1);
    } else {
        positions[document.root] = {};
        const auto &root = nodes[document.root];
        if (!root.collapsed) {
            if (document.branchDirection != BranchDirection::Both) {
                placeGroup(root.children, 1, 0, document.branchDirection == BranchDirection::Right ? 1 : -1);
            } else {
                QStringList right, left;
                for (int i = 0; i < root.children.size(); ++i) {
                    const auto &child = root.children.at(i);
                    const auto lane = nodes[child].rootLane;
                    const bool goesRight = lane == BranchDirection::Right ||
                        (lane == BranchDirection::Both && i % 2 == 0);
                    (goesRight ? right : left).append(child);
                }
                placeGroup(right, 1, 0, 1);
                placeGroup(left, 1, 0, -1);
            }
        }
    }
    return positions;
}

QMap<QString, QPointF> layoutNodes(const Document &document, const QMap<QString, QSizeF> &sizes,
                                  QMap<QString, qreal> *leafRails, QVector<qreal> *layerSizes) {
    Document tree = document;
    for (auto it = document.floating.cbegin(); it != document.floating.cend(); ++it)
        tree.nodes[tree.root].children.removeAll(it.key());
    auto positions = layoutTree(tree, sizes, leafRails, layerSizes);
    for (auto it = document.floating.cbegin(); it != document.floating.cend(); ++it) {
        if (!document.nodes.value(document.root).children.contains(it.key())) continue;
        tree.root = it.key(); tree.branchDirection = BranchDirection::Right;
        const auto branch = layoutTree(tree, sizes, nullptr, nullptr);
        for (auto pos = branch.cbegin(); pos != branch.cend(); ++pos) positions.insert(pos.key(), pos.value() + it.value());
    }
    return positions;
}
