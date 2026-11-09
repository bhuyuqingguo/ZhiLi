#include "../src/templates.h"
#include "../src/layout.h"
#include <cmath>

namespace {
QString outline(const Document &document, const QString &id) {
    const auto &node = document.nodes[id];
    QString result = '[' + node.text;
    for (const auto &child : node.children) result += outline(document, child);
    return result + ']';
}
}

int main() {
#define CHECK(condition) do { if (!(condition)) { qCritical() << __LINE__ << #condition; return 1; } } while (false)
    const auto entries = zhili::builtInTemplates();
    CHECK(entries.size() >= 80);
    QSet<QString> ids, titles, outlines, allNodeIds;
    QMap<QString, int> categories;
    int totalNodes = 0;
    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    for (const auto &entry : entries) {
        CHECK(!entry.id.isEmpty() && !ids.contains(entry.id));
        CHECK(!entry.title.isEmpty() && !titles.contains(entry.title));
        CHECK(!entry.description.isEmpty() && !entry.tags.isEmpty());
        CHECK(entry.nodeCount >= 17);
        ids.insert(entry.id);
        titles.insert(entry.title);
        ++categories[entry.category];
        Document document;
        QString error = QStringLiteral("stale error");
        CHECK(zhili::loadBuiltInTemplate(entry.id, &document, &error));
        CHECK(error.isEmpty());
        CHECK(document.nodes.size() == entry.nodeCount);
        CHECK(document.mindMap == entry.mindMap);
        CHECK(document.nodes.value(document.root).text == entry.title);
        CHECK(document.nodes.value(document.root).children.size() >= 4);
        totalNodes += document.nodes.size();
        const QString content = outline(document, document.root);
        CHECK(!outlines.contains(content));
        outlines.insert(content);
        for (const auto &node : document.nodes) {
            CHECK(!node.text.trimmed().isEmpty());
            CHECK(node.text.size() <= 80);
            CHECK(!node.collapsed);
            CHECK(!allNodeIds.contains(node.id));
            CHECK(!QUuid(node.id).isNull());
            allNodeIds.insert(node.id);
        }
        Document parsed;
        CHECK(parsed.parse(document.serialize(), &error));
        CHECK(parsed.serialize() == document.serialize());
        const QString path = temporary.filePath(entry.id + QStringLiteral(".smap"));
        CHECK(writeAtomically(path, document.serialize(), &error));
        CHECK(parsed.readFile(path, &error));
        CHECK(parsed.serialize() == document.serialize());

        Document another;
        CHECK(zhili::loadBuiltInTemplate(entry.id, &another, nullptr));
        CHECK(outline(another, another.root) == content);
        for (const auto &node : another.nodes) CHECK(!allNodeIds.contains(node.id));
        another.nodes[another.root].text = QStringLiteral("changed");
        CHECK(document.nodes.value(document.root).text == entry.title);

        // Both layouts must place all template nodes without overlaps, even
        // when the template's recommended layout differs from the chosen one.
        QMap<QString, QSizeF> sizes;
        for (const auto &node : document.nodes) sizes.insert(node.id, QSizeF(260, 70));
        for (const bool mindMap : {false, true}) {
            document.mindMap = mindMap;
            const auto positions = layoutNodes(document, sizes);
            CHECK(positions.size() == document.nodes.size());
            QVector<QRectF> rectangles;
            for (auto it = positions.cbegin(); it != positions.cend(); ++it) {
                CHECK(std::isfinite(it->x()) && std::isfinite(it->y()));
                const QRectF rectangle(*it - QPointF(130, 35), QSizeF(260, 70));
                for (const auto &previous : rectangles) CHECK(!rectangle.intersects(previous));
                rectangles.append(rectangle);
            }
        }
    }
    CHECK(categories.size() == 4);
    for (const auto &name : {QStringLiteral("活动组织"), QStringLiteral("体系视图"),
                             QStringLiteral("能力视图"), QStringLiteral("架构视图")})
        CHECK(categories.value(name) >= 20);
    CHECK(totalNodes >= 1600);

    Document unchanged;
    QString error;
    CHECK(zhili::loadBuiltInTemplate(entries.first().id, &unchanged, &error));
    const QByteArray before = unchanged.serialize();
    for (const auto &id : {QString(), QStringLiteral("missing"), QStringLiteral("../catalog.json"),
                           QStringLiteral(":/templates/activity-kickoff.smap"), QStringLiteral("活动组织")}) {
        CHECK(!zhili::loadBuiltInTemplate(id, &unchanged, &error));
        CHECK(!error.isEmpty() && unchanged.serialize() == before);
        CHECK(!zhili::loadBuiltInTemplate(id, &unchanged, nullptr));
    }
    CHECK(!zhili::loadBuiltInTemplate(entries.first().id, nullptr, &error));
    CHECK(!error.isEmpty());
    auto changedCatalog = zhili::builtInTemplates();
    changedCatalog.first().title = QStringLiteral("Changed title");
    CHECK(zhili::builtInTemplates().first().title == entries.first().title);
    CHECK(zhili::loadBuiltInTemplate(entries.first().id, &unchanged, &error));
    CHECK(unchanged.nodes.value(unchanged.root).text == entries.first().title);
    qInfo() << "Validated" << entries.size() << "templates and" << totalNodes << "nodes in both layouts";
    return 0;
#undef CHECK
}
