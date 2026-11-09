#include "document.h"

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
    const QJsonDocument json = QJsonDocument::fromJson(bytes);
    if (!json.isObject()) return reject(QStringLiteral("JSON 文件解析失败"));
    const QJsonObject object = json.object();
    if (object.value("format").toString() != "simplemap-json" || object.value("version").toInt() != 1)
        return reject(QStringLiteral("文件格式或版本不受支持"));

    Document next;
    const auto entries = object.value("nodes").toArray();
    if (entries.isEmpty() || entries.size() > maxNodes) return reject(QStringLiteral("节点数量不合法"));
    const QString invalidNode = QStringLiteral("节点数据无效");
    for (const auto &entry : entries) {
        const auto fields = entry.toObject();
        Node node;
        node.id = fields.value("id").toString();
        node.text = fields.value("text").toString();
        node.collapsed = fields.value("collapsed").toBool();
        if (node.id.isEmpty() || next.nodes.contains(node.id) || !fields.value("text").isString()
            || node.text.size() > maxText || !fields.value("children").isArray()
            || (fields.contains("collapsed") && !fields.value("collapsed").isBool()))
            return reject(invalidNode);
        for (const auto &child : fields.value("children").toArray()) {
            if (!child.isString()) return reject(invalidNode);
            node.children.append(child.toString());
        }
        next.nodes.insert(node.id, node);
    }

    next.root = object.value("root").toString();
    next.mindMap = object.value("layout").toString() != "organization";
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
    return true;
}

bool writeAtomically(const QString &path, const QByteArray &bytes, QString *error) {
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit()) return true;
    if (error) *error = file.errorString();
    return false;
}
