#include "document.h"

namespace {
bool isUnicode(const QString &text) {
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i).isHighSurrogate()) {
            ++i;
            if (i == text.size() || !text.at(i).isLowSurrogate()) return false;
        } else if (text.at(i).isLowSurrogate()) {
            return false;
        }
    }
    return true;
}
}

QByteArray Document::serialize() const {
    QJsonArray entries;
    for (const Node &node : nodes) {
        entries.append(QJsonObject{
            {"id", node.id}, {"text", node.text}, {"collapsed", node.collapsed},
            {"children", QJsonArray::fromStringList(node.children)}
        });
    }
    return QJsonDocument(QJsonObject{
        {"format", "simplemap-json"}, {"version", 1}, {"root", root},
        {"nodes", entries}, {"layout", mindMap ? "mindmap" : "organization"}
    }).toJson(QJsonDocument::Compact);
}

bool Document::parse(const QByteArray &bytes, QString *error) {
    const auto reject = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (bytes.size() > maxBytes) return reject(QStringLiteral("文件超过 50 MiB"));
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return reject(QStringLiteral("JSON 在第 %1 字节解析失败").arg(parseError.offset + 1));
    if (!json.isObject()) return reject(QStringLiteral("JSON 文件必须以对象为根"));
    const QJsonObject object = json.object();
    if (object.value("format").toString() != "simplemap-json" || object.value("version").toInt() != 1)
        return reject(QStringLiteral("文件格式或版本不受支持"));

    Document next;
    const auto entries = object.value("nodes").toArray();
    if (entries.isEmpty() || entries.size() > maxNodes) return reject(QStringLiteral("节点数量不合法"));
    const QJsonValue layout = object.value("layout");
    if (!layout.isUndefined() && layout != QJsonValue("mindmap") && layout != QJsonValue("organization"))
        return reject(QStringLiteral("布局类型不受支持"));
    next.mindMap = layout != QJsonValue("organization");

    // A tree with N nodes can have at most N - 1 child references.
    int remainingLinks = entries.size() - 1;
    const QString invalidNode = QStringLiteral("节点数据无效");
    for (const auto &entry : entries) {
        const auto fields = entry.toObject();
        Node node;
        node.id = fields.value("id").toString();
        node.text = fields.value("text").toString();
        node.collapsed = fields.value("collapsed").toBool();
        if (node.id.isEmpty() || next.nodes.contains(node.id) || !fields.value("text").isString()
            || node.text.size() > maxText || !isUnicode(node.id) || !isUnicode(node.text)
            || !fields.value("children").isArray()
            || (fields.contains("collapsed") && !fields.value("collapsed").isBool()))
            return reject(invalidNode);
        const auto children = fields.value("children").toArray();
        if (children.size() > remainingLinks)
            return reject(QStringLiteral("导图存在环或重复引用"));
        remainingLinks -= children.size();
        for (const auto &child : children) {
            if (!child.isString() || child.toString().isEmpty()) return reject(invalidNode);
            node.children.append(child.toString());
        }
        next.nodes.insert(node.id, node);
    }

    next.root = object.value("root").toString();
    QSet<QString> visited;
    QVector<QPair<QString, int>> pending{{next.root, 0}};
    while (!pending.isEmpty()) {
        const auto entry = pending.takeLast();
        const auto node = next.nodes.constFind(entry.first);
        if (node == next.nodes.cend() || visited.contains(entry.first))
            return reject(QStringLiteral("导图存在缺失节点、环或重复引用"));
        if (entry.second > maxDepth) return reject(QStringLiteral("导图层级过深"));
        visited.insert(entry.first);
        for (const auto &child : node->children) pending.append(qMakePair(child, entry.second + 1));
    }
    if (visited.size() != next.nodes.size()) return reject(QStringLiteral("导图包含孤立节点"));
    *this = std::move(next);
    if (error) error->clear();
    return true;
}

bool Document::readFile(const QString &path, QString *error) {
    if (!QFileInfo(path).isFile()) {
        if (error) *error = QStringLiteral("文件不存在或不是普通文件");
        return false;
    }
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        if (file.size() > maxBytes) {
            if (error) *error = QStringLiteral("文件超过 50 MiB");
            return false;
        }
        // Also cap reads if the file grows after the size check.
        const QByteArray data = file.read(maxBytes + 1);
        if (file.error() == QFileDevice::NoError) return parse(data, error);
    }
    if (error) *error = file.errorString();
    return false;
}

bool writeAtomically(const QString &path, const QByteArray &bytes, QString *error) {
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit()) {
        if (error) error->clear();
        return true;
    }
    if (error) *error = file.errorString();
    return false;
}
