#include "../src/clipboard.h"

namespace {
Document sample() {
    Document doc;
    doc.root = "root";
    Node root;
    root.id = "root";
    root.text = QStringLiteral("研发项目");
    root.children = QStringList{"a", "b"};
    doc.nodes.insert(root.id, root);
    Node a;
    a.id = "a";
    a.text = QStringLiteral("分支 A\n第二行 🌿");
    a.children = QStringList{"leaf"};
    a.collapsed = true;
    doc.nodes.insert(a.id, a);
    Node leaf;
    leaf.id = "leaf";
    leaf.text = QString(500, QChar(0x679D));
    doc.nodes.insert(leaf.id, leaf);
    Node b;
    b.id = "b";
    b.text = QStringLiteral("分支 B");
    doc.nodes.insert(b.id, b);
    return doc;
}

Document chain(int depth) {
    Document doc;
    doc.root = "0";
    for (int i = 0; i <= depth; ++i) {
        Node node;
        node.id = QString::number(i);
        node.text = node.id;
        if (i < depth) node.children.append(QString::number(i + 1));
        doc.nodes.insert(node.id, node);
    }
    return doc;
}
}

int main() {
#define CHECK(condition) do { if (!(condition)) { qCritical() << __LINE__ << #condition; return 1; } } while (false)
    Document doc = sample();
    QString error = "old error";
    CHECK(QString::fromLatin1(BranchClipboard::mimeType()) == "application/x-zhili-branch+json");
    const QByteArray encoded = BranchClipboard::encode(doc, "a", &error);
    CHECK(!encoded.isEmpty() && error.isEmpty());
    Document copied;
    CHECK(copied.parse(encoded));
    CHECK(copied.root == "a" && copied.nodes.size() == 2);
    CHECK(copied.nodes.value("a").text == doc.nodes.value("a").text);
    CHECK(copied.nodes.value("a").collapsed && copied.nodes.value("leaf").text.size() == 500);
    CHECK(!copied.nodes.contains("root") && !copied.nodes.contains("b"));
    CHECK(BranchClipboard::outline(doc, "a") == QStringLiteral("分支 A 第二行 🌿\n\t") + QString(500, QChar(0x679D)));
    CHECK(BranchClipboard::encode(doc, "missing", &error).isEmpty() && !error.isEmpty());
    CHECK(BranchClipboard::outline(doc, "missing").isEmpty());
    Document malformed = doc;
    malformed.nodes["a"].text = QString(QChar(0xd800));
    CHECK(BranchClipboard::encode(malformed, "a", &error).isEmpty() && !error.isEmpty());
    malformed = doc;
    malformed.nodes["a"].symbolId = QString(QChar(0xdc00));
    CHECK(BranchClipboard::encode(malformed, "a", &error).isEmpty() && !error.isEmpty());

    doc.mindMap = false;
    doc.nodes["b"].collapsed = true;
    QStringList inserted;
    CHECK(BranchClipboard::paste(&doc, "b", encoded, &inserted, &error));
    CHECK(error.isEmpty() && inserted.size() == 1 && !doc.mindMap && doc.root == "root");
    CHECK(doc.nodes.size() == 6 && !doc.nodes.value("b").collapsed);
    const QString pasted = inserted.first();
    CHECK(pasted != "a" && doc.nodes.value("b").children == inserted);
    const Node pastedNode = doc.nodes.value(pasted);
    CHECK(pastedNode.text == copied.nodes.value("a").text && pastedNode.collapsed);
    CHECK(pastedNode.children.size() == 1 && pastedNode.children.first() != "leaf");
    CHECK(doc.nodes.value(pastedNode.children.first()).text == copied.nodes.value("leaf").text);
    CHECK(doc.nodes.value("root").children == (QStringList{"a", "b"}));
    CHECK(BranchClipboard::paste(&doc, "b", encoded, &inserted, &error));
    CHECK(doc.nodes.size() == 8 && inserted.first() != pasted);
    CHECK(doc.nodes.value("b").children == (QStringList{pasted, inserted.first()}));

    // Copying the center topic is permitted and still creates fresh identifiers.
    const Document original = sample();
    Document target = chain(0);
    CHECK(BranchClipboard::paste(&target, "0", BranchClipboard::encode(original, original.root), &inserted, &error));
    CHECK(target.nodes.size() == 5 && inserted.size() == 1 && target.root == "0");
    CHECK(target.nodes.value(inserted.first()).children.size() == 2);
    Document roundTrip;
    CHECK(roundTrip.parse(target.serialize()));

    const QByteArray before = doc.serialize();
    const QStringList priorRoots = inserted;
    const auto rejects = [&](const QByteArray &data, const QString &parent) {
        return !BranchClipboard::paste(&doc, parent, data, &inserted, &error)
            && !error.isEmpty() && doc.serialize() == before && inserted == priorRoots;
    };
    CHECK(rejects("not JSON", "root"));
    CHECK(rejects(encoded.left(encoded.size() / 2), "root"));
    CHECK(rejects(encoded, "missing"));
    Document invalid = original;
    invalid.nodes["leaf"].children.append("a");
    CHECK(rejects(invalid.serialize(), "root"));
    CHECK(BranchClipboard::encode(invalid, "a", &error).isEmpty() && !error.isEmpty());
    CHECK(BranchClipboard::outline(invalid, "a").isEmpty());
    CHECK(!BranchClipboard::paste(nullptr, "root", encoded, &inserted, &error));

    // Plain text supports several top-level branches, blank lines, CRLF, tabs,
    // and consistently aligned space indentation. Literal punctuation is kept.
    target = chain(0);
    const QString outline = QStringLiteral("  第一项\r\n    子项 🌿\r\n      孙项\r\n\r\n    第二个子项\r\n  - 第二项  \r\n");
    CHECK(BranchClipboard::pasteOutline(&target, "0", outline, &inserted, &error));
    CHECK(target.nodes.size() == 6 && inserted.size() == 2);
    const Node first = target.nodes.value(inserted.first());
    CHECK(first.text == QStringLiteral("第一项") && first.children.size() == 2);
    CHECK(target.nodes.value(first.children.first()).text == QStringLiteral("子项 🌿"));
    CHECK(target.nodes.value(first.children.first()).children.size() == 1);
    CHECK(target.nodes.value(inserted.last()).text == QStringLiteral("- 第二项  "));
    CHECK(BranchClipboard::pasteOutline(&target, "0", QStringLiteral("A\n\tB\n\t\tC\n\tD\nE"), &inserted, &error));
    CHECK(inserted.size() == 2);
    CHECK(target.nodes.value(inserted.first()).children.size() == 2);

    const QByteArray beforeOutline = target.serialize();
    const QStringList beforeRoots = inserted;
    const auto rejectsOutline = [&](const QString &text) {
        return !BranchClipboard::pasteOutline(&target, "0", text, &inserted, &error)
            && !error.isEmpty() && target.serialize() == beforeOutline && inserted == beforeRoots;
    };
    CHECK(rejectsOutline(" \n\t\r\n"));
    CHECK(rejectsOutline("  A\nB"));
    CHECK(rejectsOutline("A\n    B\n  C"));
    CHECK(rejectsOutline(QString(Document::maxText + 1, 'x')));
    CHECK(rejectsOutline(QString(QChar(0xd800))));
    CHECK(rejectsOutline(QString(QChar(0xdc00))));
    CHECK(rejectsOutline(QStringLiteral("A\n\t") + QChar(0xd800) + QStringLiteral("B")));

    // Combined depth limits are checked transactionally, not only per source.
    target = chain(Document::maxDepth);
    const QByteArray deepest = target.serialize();
    CHECK(!BranchClipboard::paste(&target, QString::number(Document::maxDepth), encoded, &inserted, &error));
    CHECK(target.serialize() == deepest && !error.isEmpty());
    CHECK(!BranchClipboard::pasteOutline(&target, QString::number(Document::maxDepth), "too deep", &inserted, &error));
    CHECK(target.serialize() == deepest);
    CHECK(BranchClipboard::pasteOutline(&target, QString::number(Document::maxDepth - 1), "at limit", &inserted, &error));
    CHECK(roundTrip.parse(target.serialize()));

    // The node ceiling also accounts for the existing destination document.
    target = chain(0);
    for (int i = 1; i < Document::maxNodes; ++i) {
        Node node;
        node.id = QString::number(i);
        target.nodes.insert(node.id, node);
        target.nodes["0"].children.append(node.id);
    }
    const QByteArray full = target.serialize();
    CHECK(!BranchClipboard::paste(&target, "0", encoded, &inserted, &error));
    CHECK(target.serialize() == full && !error.isEmpty());
    CHECK(!BranchClipboard::pasteOutline(&target, "0", "one more", &inserted, &error));
    CHECK(target.serialize() == full);
    qInfo() << "ZhiLi clipboard tests passed";
    return 0;
#undef CHECK
}
