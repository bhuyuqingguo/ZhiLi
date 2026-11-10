#include "../src/clipboard.h"

int main() {
#define CHECK(condition) do { if (!(condition)) { qCritical() << __LINE__ << #condition; return 1; } } while (false)
    const QByteArray oldFile = R"({"format":"simplemap-json","version":1,"root":"root","nodes":[{"id":"root","text":"旧版文件","children":["a"]},{"id":"a","text":"分支","children":[]}]})";
    Document document;
    QString error;
    CHECK(document.parse(oldFile, &error));
    CHECK(document.nodes.value("root").symbolId.isEmpty());
    CHECK(!document.serialize().contains("symbolId"));
    document.nodes["root"].symbolId = "command-control-center";
    document.nodes["a"].symbolId = "future-symbol-not-in-this-version";
    const QByteArray encoded = document.serialize();
    Document reopened;
    CHECK(reopened.parse(encoded, &error));
    CHECK(reopened.serialize() == encoded);
    CHECK(reopened.nodes.value("root").symbolId == "command-control-center");
    CHECK(reopened.nodes.value("a").symbolId == "future-symbol-not-in-this-version");

    const QByteArray copied = BranchClipboard::encode(document, "a", &error);
    QStringList roots;
    CHECK(BranchClipboard::paste(&reopened, "root", copied, &roots, &error));
    CHECK(roots.size() == 1 && roots.first() != "a");
    CHECK(reopened.nodes.value(roots.first()).symbolId == document.nodes.value("a").symbolId);

    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("符号往返.smap"));
    CHECK(writeAtomically(path, encoded, &error));
    CHECK(reopened.readFile(path, &error) && reopened.serialize() == encoded);

    QJsonObject object = QJsonDocument::fromJson(encoded).object();
    const auto rejects = [&](const QJsonValue &value) {
        QJsonObject changed = object;
        QJsonArray nodes = changed.value("nodes").toArray();
        QJsonObject node = nodes.first().toObject();
        node["symbolId"] = value;
        nodes[0] = node;
        changed["nodes"] = nodes;
        return !reopened.parse(QJsonDocument(changed).toJson(), &error)
            && !error.isEmpty() && reopened.serialize() == encoded;
    };
    CHECK(rejects(QJsonValue(3)));
    CHECK(rejects(QJsonValue(QJsonValue::Null)));
    CHECK(rejects(QJsonValue(QJsonArray{})));
    CHECK(rejects(QJsonValue(QString(129, 'x'))));
    // Construct the invalid JSON escape directly: Qt 5.12's serializer replaces
    // a lone QString surrogate before the parser ever sees it.
    QByteArray malformed = encoded;
    malformed.replace("command-control-center", "\\ud800");
    CHECK(!reopened.parse(malformed, &error) && !error.isEmpty() && reopened.serialize() == encoded);
    document.nodes["root"].symbolId.clear();
    document.nodes["a"].symbolId.clear();
    CHECK(!document.serialize().contains("symbolId"));
    CHECK(reopened.parse(document.serialize(), &error));
    CHECK(reopened.nodes.value("root").symbolId.isEmpty());
    qInfo() << "Symbol persistence passed: legacy, unknown ID, clipboard, disk, invalid input";
    return 0;
#undef CHECK
}
