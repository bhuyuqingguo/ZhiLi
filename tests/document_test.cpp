#include "../src/document.h"

namespace {
QByteArray encode(const QJsonObject &object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray chain(int depth) {
    Document doc;
    doc.root = "0";
    for (int i = 0; i <= depth; ++i) {
        Node node;
        node.id = QString::number(i);
        if (i < depth) node.children.append(QString::number(i + 1));
        doc.nodes.insert(node.id, node);
    }
    return doc.serialize();
}
}

int main() {
#define CHECK(condition) do { if (!(condition)) { qCritical() << __LINE__ << #condition; return 1; } } while (false)
    // The handoff writer used the same v1 data in both .json and .smap.json files.
    const QByteArray legacy = R"({
        "format": "simplemap-json", "version": 1, "root": "root",
        "nodes": [
            {"id": "first", "text": "第一分支\n🌿", "children": []},
            {"id": "root", "text": "", "children": ["second", "first"], "collapsed": true},
            {"id": "second", "text": "第二分支", "children": [], "collapsed": false}
        ], "layout": "organization"
    })";
    Document doc;
    QString error;
    CHECK(doc.parse(legacy, &error));
    CHECK(error.isEmpty());
    CHECK(!doc.mindMap && doc.root == "root" && doc.nodes.value("root").collapsed);
    CHECK(doc.nodes.value("root").text.isEmpty());
    const QStringList order{"second", "first"};
    CHECK(doc.nodes.value("root").children == order);
    CHECK(doc.nodes.value("first").text == QStringLiteral("第一分支\n🌿"));

    const QByteArray saved = doc.serialize();
    const auto rejects = [&](const QByteArray &input) {
        return !doc.parse(input, &error) && !error.isEmpty() && doc.serialize() == saved;
    };

    QJsonObject legacyObject = QJsonDocument::fromJson(legacy).object();
    QJsonObject changed = legacyObject;
    changed.remove("layout");
    Document fallback;
    CHECK(fallback.parse(encode(changed), &error) && fallback.mindMap);
    changed["layout"] = "unsupported";
    CHECK(rejects(encode(changed)));
    changed = legacyObject;
    changed["version"] = 1.5;
    CHECK(rejects(encode(changed)));
    changed["version"] = 2;
    CHECK(rejects(encode(changed)));

    QByteArray extraLink = legacy;
    extraLink.replace(R"("second", "first")", R"("second", "first", "first")");
    CHECK(rejects(extraLink));
    QByteArray duplicate = legacy;
    duplicate.replace(R"("second", "first")", R"("second", "second")");
    CHECK(rejects(duplicate));
    QByteArray missing = legacy;
    missing.replace(R"("second", "first")", R"("second", "missing")");
    CHECK(rejects(missing));
    QByteArray isolated = legacy;
    isolated.replace(R"("second", "first")", R"("second")");
    CHECK(rejects(isolated));
    QByteArray invalidUtf8 = legacy;
    invalidUtf8.replace(QByteArray("🌿"), QByteArray("\xC0\xAF", 2));
    CHECK(rejects(invalidUtf8));
    QByteArray loneSurrogate = legacy;
    loneSurrogate.replace(QByteArray("🌿"), R"(\uD800)");
    CHECK(rejects(loneSurrogate));
    CHECK(rejects(legacy.left(legacy.size() / 2)));
    CHECK(rejects(chain(Document::maxDepth + 1)));
    CHECK(fallback.parse(chain(Document::maxDepth), &error));

    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("分支 🌿.smap.json"));
    CHECK(writeAtomically(path, legacy, &error));
    CHECK(fallback.readFile(path, &error));
    CHECK(fallback.serialize() == saved);
    CHECK(writeAtomically(path, saved, &error));
    CHECK(doc.readFile(path, &error) && doc.serialize() == saved);

    const QString directory = temporary.filePath("directory");
    CHECK(QDir().mkpath(directory));
    CHECK(!writeAtomically(directory, saved, &error));
    CHECK(!error.isEmpty() && QFileInfo(directory).isDir());
    CHECK(!doc.readFile(directory, &error));
    CHECK(!doc.readFile(temporary.filePath("missing.smap"), &error));
    CHECK(!error.isEmpty() && doc.serialize() == saved);

    QFile oversized(temporary.filePath("oversized.smap"));
    CHECK(oversized.open(QIODevice::WriteOnly));
    CHECK(oversized.resize(Document::maxBytes + 1));
    oversized.close();
    CHECK(!doc.readFile(oversized.fileName(), &error));
    CHECK(!error.isEmpty() && doc.serialize() == saved);
    return 0;
#undef CHECK
}
