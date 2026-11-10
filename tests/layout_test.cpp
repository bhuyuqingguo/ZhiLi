#include "../src/layout.h"

#include <cmath>
#include <random>

namespace {
Document tree(int count, int fanout) {
    Document result;
    result.root = "0";
    for (int i = 0; i < count; ++i) {
        Node node;
        node.id = QString::number(i);
        node.text = QStringLiteral("主题 %1 🌿").arg(i);
        for (int child = i * fanout + 1;
             child <= i * fanout + fanout && child < count; ++child)
            node.children.append(QString::number(child));
        result.nodes.insert(node.id, node);
    }
    return result;
}

bool verify(const Document &document, const QMap<QString, QSizeF> &sizes) {
    QMap<QString, qreal> rails;
    const QMap<QString, QPointF> positions = layoutNodes(document, sizes, &rails);
    // A second layout must be deterministic, not depend on hash iteration order.
    QMap<QString, qreal> secondRails;
    if (positions != layoutNodes(document, sizes, &secondRails) || rails != secondRails) return false;
    QSet<QString> visible;
    QStringList pending{document.root};
    while (!pending.isEmpty()) {
        const QString id = pending.takeLast();
        visible.insert(id);
        if (!document.nodes.value(id).collapsed)
            pending.append(document.nodes.value(id).children);
    }
    if (positions.size() != visible.size() || positions.value(document.root) != QPointF())
        return false;
    QVector<QPair<QString, QRectF>> rectangles;
    for (const QString &id : visible) {
        if (!positions.contains(id)) return false;
        const QPointF center = positions.value(id);
        if (!std::isfinite(center.x()) || !std::isfinite(center.y())) return false;
        const QSizeF size = sizes.value(id);
        rectangles.append(qMakePair(id, QRectF(center - QPointF(size.width(), size.height()) / 2, size)));
        if (document.nodes.value(id).collapsed) continue;
        for (const QString &child : document.nodes.value(id).children) {
            const QPointF next = positions.value(child);
            const QSizeF childSize = sizes.value(child);
            if (document.mindMap) {
                const qreal separation = std::abs(next.x()) - std::abs(center.x());
                if (separation < (size.width() + childSize.width()) / 2 + 63.99)
                    return false;
            } else if (next.y() - center.y() < (size.height() + childSize.height()) / 2 + 61.99) {
                return false;
            }
        }
    }
    for (int i = 0; i < rectangles.size(); ++i) {
        for (int j = i + 1; j < rectangles.size(); ++j) {
            if (rectangles.at(i).second.intersects(rectangles.at(j).second)) {
                qCritical() << "Overlapping nodes:" << rectangles.at(i).first << rectangles.at(j).first;
                return false;
            }
        }
    }
    return true;
}
}

int main() {
#define CHECK(condition) do { if (!(condition)) { qCritical() << __LINE__ << #condition; return 1; } } while (false)
    std::mt19937 random(107);
    for (int count : {1, 2, 3, 100, 500, 1000, 3000}) {
        for (int fanout : {4, qMax(1, count)}) {
            Document document = tree(count, fanout);
            QMap<QString, QSizeF> sizes;
            for (const QString &id : document.nodes.keys())
                sizes[id] = QSizeF(80 + random() % 360, 30 + random() % 200);
            // Full-text labels may be much taller than ordinary short topics.
            sizes["0"] = QSizeF(260, 9000);
            if (count > 4) sizes["4"] = QSizeF(400, 100000);
            for (bool classic : {false, true}) {
                document.mindMap = classic;
                CHECK(verify(document, sizes));
                if (count > 4) {
                    document.nodes["1"].collapsed = true;
                    CHECK(verify(document, sizes));
                    document.nodes["1"].collapsed = false;
                }
                document.nodes[document.root].collapsed = true;
                CHECK(verify(document, sizes));
                document.nodes[document.root].collapsed = false;
            }
        }
    }
    // Exercise the maximum accepted nesting without exceeding the parser limit.
    Document chain = tree(Document::maxDepth + 1, 1);
    QMap<QString, QSizeF> sizes;
    for (const QString &id : chain.nodes.keys()) sizes[id] = QSizeF(150, 60);
    Document parsed;
    CHECK(parsed.parse(chain.serialize()));
    for (bool classic : {false, true}) {
        chain.mindMap = classic;
        CHECK(verify(chain, sizes));
    }
    Document compact = tree(21, 4);
    sizes.clear();
    for (const QString &id : compact.nodes.keys()) sizes[id] = QSizeF(120, 45);
    for (auto direction : {BranchDirection::Right, BranchDirection::Left}) {
        compact.branchDirection = direction;
        const auto positions = layoutNodes(compact, sizes);
        CHECK(verify(compact, sizes));
        for (auto it = positions.cbegin(); it != positions.cend(); ++it) {
            if (it.key() == compact.root) continue;
            CHECK(direction == BranchDirection::Right ? it->x() > 0 : it->x() < 0);
        }
        Document roundTrip;
        CHECK(roundTrip.parse(compact.serialize()) && roundTrip.branchDirection == direction);
    }
    compact.mindMap = false;
    QMap<QString, qreal> rails;
    const auto positions = layoutNodes(compact, sizes, &rails);
    CHECK(rails.size() == 16 && rails.value("5") == rails.value("6"));
    CHECK(positions.value("5").x() == positions.value("6").x() && positions.value("5").y() < positions.value("6").y());
    qInfo() << "Layout contracts passed: both layouts, deterministic positions, collapse, large labels, 3000 nodes and depth 512";
    return 0;
#undef CHECK
}
