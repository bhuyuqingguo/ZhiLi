#include "layout.h"
#include <functional>

QMap<QString, QPointF> layoutNodes(const Document &document, const QMap<QString, QSizeF> &sizes) {
    const auto &nodes = document.nodes;
    const bool classic = document.mindMap;
    QMap<QString, QPointF> positions;
    QMap<QString, qreal> spans;
    QVector<qreal> levels;
    const qreal gap = classic ? 24 : 30;
    const qreal levelGap = classic ? 64 : 62;

    std::function<qreal(const QString &, int)> measure = [&](const QString &id, int depth) {
        const auto size = sizes.value(id);
        const qreal thickness = classic ? size.width() : size.height();
        if (levels.size() <= depth) levels.append(0);
        levels[depth] = qMax(levels[depth], thickness);
        const Node &node = nodes[id];
        qreal children = 0;
        if (!node.collapsed) {
            for (const auto &child : node.children) children += measure(child, depth + 1) + gap;
        }
        return spans[id] = qMax(classic ? size.height() : size.width(), children - gap);
    };
    measure(document.root, 0);
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
        if (!node.collapsed) placeGroup(node.children, depth + 1, center, direction);
    };
    if (!classic) {
        place(document.root, 0, 0, 1);
    } else {
        positions[document.root] = {};
        const auto &root = nodes[document.root];
        if (!root.collapsed) {
            QStringList right, left;
            for (int i = 0; i < root.children.size(); ++i)
                (i % 2 == 0 ? right : left).append(root.children.at(i));
            placeGroup(right, 1, 0, 1);
            placeGroup(left, 1, 0, -1);
        }
    }
    return positions;
}
