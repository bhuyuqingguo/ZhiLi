#include "document.h"
#include <cmath>

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

QJsonObject writeStyle(const TopicStyle &style) {
    QJsonObject result;
    if (style.alignment != TextAlignment::Center)
        result.insert("alignment", style.alignment == TextAlignment::Left ? "left" : "right");
    if (!style.family.isEmpty()) result.insert("family", style.family);
    if (style.points != 0) result.insert("points", style.points);
    if (style.weight != -1) result.insert("bold", style.weight == 1);
    if (style.italic) result.insert("italic", true);
    if (style.underline) result.insert("underline", true);
    if (style.strikeOut) result.insert("strikeOut", true);
    if (!style.color.isEmpty()) result.insert("color", style.color);
    return result;
}

bool readStyle(const QJsonValue &value, TopicStyle &style) {
    if (value.isUndefined()) return true;
    if (!value.isObject()) return false;
    const QJsonObject fields = value.toObject();
    const auto alignment = fields.value("alignment");
    if (alignment == QJsonValue("left")) style.alignment = TextAlignment::Left;
    else if (alignment == QJsonValue("right")) style.alignment = TextAlignment::Right;
    else if (!alignment.isUndefined() && alignment != QJsonValue("center")) return false;
    if (fields.contains("family")) {
        const auto family = fields.value("family");
        if (!family.isString() || family.toString().size() > 128 || !isUnicode(family.toString())) return false;
        style.family = family.toString();
    }
    if (fields.contains("points")) {
        style.points = fields.value("points").toInt();
        if (style.points < 9 || style.points > 48) return false;
    }
    if (fields.contains("bold")) {
        const auto bold = fields.value("bold");
        if (!bold.isBool()) return false;
        style.weight = bold.toBool() ? 1 : 0;
    }
    for (const auto &name : {QStringLiteral("italic"), QStringLiteral("underline"), QStringLiteral("strikeOut")}) {
        if (fields.contains(name) && !fields.value(name).isBool()) return false;
    }
    style.italic = fields.value("italic").toBool();
    style.underline = fields.value("underline").toBool();
    style.strikeOut = fields.value("strikeOut").toBool();
    if (fields.contains("color")) {
        static const QRegularExpression hex(QStringLiteral("^#[0-9a-fA-F]{6}$"));
        style.color = fields.value("color").toString();
        if (!hex.match(style.color).hasMatch()) return false;
        style.color = style.color.toLower();
    }
    return true;
}
}

QByteArray Document::serialize() const {
    QJsonArray entries;
    for (const Node &node : nodes) {
        QJsonObject fields{
            {"id", node.id}, {"text", node.text}, {"collapsed", node.collapsed},
            {"children", QJsonArray::fromStringList(node.children)}
        };
        if (!node.notes.isEmpty()) fields.insert("notes", node.notes);
        if (!node.symbolId.isEmpty()) fields.insert("symbolId", node.symbolId);
        if (node.rootLane != BranchDirection::Both)
            fields.insert("rootLane", node.rootLane == BranchDirection::Right ? "right" : "left");
        const auto style = writeStyle(node.style);
        if (!style.isEmpty()) fields.insert("style", style);
        entries.append(fields);
    }
    QJsonObject map{
        {"format", "simplemap-json"}, {"version", 1}, {"root", root},
        {"nodes", entries}, {"layout", mindMap ? "mindmap" : "organization"}
    };
    if (!floating.isEmpty() || !relationships.isEmpty() || !boundaries.isEmpty()) map.insert("version", 2);
    QJsonObject positions;
    for (auto it = floating.cbegin(); it != floating.cend(); ++it)
        positions.insert(it.key(), QJsonArray{it->x(), it->y()});
    if (!positions.isEmpty()) map.insert("floating", positions);
    QJsonArray links, frames;
    for (const auto &link : relationships)
        links.append(QJsonObject{{"id", link.id}, {"from", link.from}, {"to", link.to}, {"text", link.text},
            {"straight", link.straight}, {"bend", QJsonArray{link.bend.x(), link.bend.y()}}});
    for (const auto &frame : boundaries)
        frames.append(QJsonObject{{"id", frame.id}, {"text", frame.text}, {"members", QJsonArray::fromStringList(frame.members)}});
    if (!links.isEmpty()) map.insert("relationships", links);
    if (!frames.isEmpty()) map.insert("boundaries", frames);
    if (mindMap && branchDirection != BranchDirection::Both)
        map.insert("branchDirection", branchDirection == BranchDirection::Right ? "right" : "left");
    switch (connectorStyle) {
    case ConnectorStyle::Orthogonal: map.insert("connectorStyle", "orthogonal"); break;
    case ConnectorStyle::Straight: map.insert("connectorStyle", "straight"); break;
    case ConnectorStyle::Curved: map.insert("connectorStyle", "curved"); break;
    case ConnectorStyle::Automatic: break;
    }
    return QJsonDocument(map).toJson(QJsonDocument::Compact);
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
    if (object.value("format").toString() != "simplemap-json" ||
        (object.value("version") != QJsonValue(1) && object.value("version") != QJsonValue(2)))
        return reject(QStringLiteral("文件格式或版本不受支持"));

    Document next;
    const auto entries = object.value("nodes").toArray();
    if (entries.isEmpty() || entries.size() > maxNodes) return reject(QStringLiteral("节点数量不合法"));
    const QJsonValue layout = object.value("layout");
    if (!layout.isUndefined() && layout != QJsonValue("mindmap") && layout != QJsonValue("organization"))
        return reject(QStringLiteral("布局类型不受支持"));
    next.mindMap = layout != QJsonValue("organization");
    const auto direction = object.value("branchDirection");
    if (!direction.isUndefined() && direction != QJsonValue("right") && direction != QJsonValue("left"))
        return reject(QStringLiteral("分支方向不受支持"));
    if (next.mindMap && direction == QJsonValue("right")) next.branchDirection = BranchDirection::Right;
    if (next.mindMap && direction == QJsonValue("left")) next.branchDirection = BranchDirection::Left;
    const auto connectors = object.value("connectorStyle");
    if (connectors == QJsonValue("orthogonal")) next.connectorStyle = ConnectorStyle::Orthogonal;
    else if (connectors == QJsonValue("straight")) next.connectorStyle = ConnectorStyle::Straight;
    else if (connectors == QJsonValue("curved")) next.connectorStyle = ConnectorStyle::Curved;
    else if (!connectors.isUndefined() && connectors != QJsonValue("automatic"))
        return reject(QStringLiteral("连线线型不受支持"));

    // A tree with N nodes can have at most N - 1 child references.
    int remainingLinks = entries.size() - 1;
    const QString invalidNode = QStringLiteral("节点数据无效");
    for (const auto &entry : entries) {
        const auto fields = entry.toObject();
        Node node;
        node.id = fields.value("id").toString();
        node.text = fields.value("text").toString();
        node.notes = fields.value("notes").toString();
        node.symbolId = fields.value("symbolId").toString();
        node.collapsed = fields.value("collapsed").toBool();
        const auto lane = fields.value("rootLane");
        if (lane == QJsonValue("right")) node.rootLane = BranchDirection::Right;
        else if (lane == QJsonValue("left")) node.rootLane = BranchDirection::Left;
        else if (!lane.isUndefined()) return reject(invalidNode);
        if (node.id.isEmpty() || next.nodes.contains(node.id) || !fields.value("text").isString()
            || node.text.size() > maxText || !isUnicode(node.id) || !isUnicode(node.text)
            || (fields.contains("notes") && !fields.value("notes").isString())
            || node.notes.size() > maxText || !isUnicode(node.notes)
            || (fields.contains("symbolId") && !fields.value("symbolId").isString())
            || node.symbolId.size() > 128 || !isUnicode(node.symbolId)
            || !readStyle(fields.value("style"), node.style)
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
    const auto point = [](const QJsonValue &value, QPointF *out) {
        const auto a = value.toArray();
        if (a.size() != 2 || !a[0].isDouble() || !a[1].isDouble()) return false;
        const double x = a[0].toDouble(), y = a[1].toDouble();
        if (!std::isfinite(x) || !std::isfinite(y) || qAbs(x) > 10000000 || qAbs(y) > 10000000) return false;
        *out = QPointF(x, y); return true;
    };
    const QString invalid = QStringLiteral("关系线、外框或浮动主题数据无效");
    if (object.contains("floating") && !object.value("floating").isObject()) return reject(invalid);
    const auto floatingFields = object.value("floating").toObject();
    for (auto it = floatingFields.begin(); it != floatingFields.end(); ++it) {
        QPointF p;
        if (!next.nodes[next.root].children.contains(it.key()) || !point(it.value(), &p)) return reject(invalid);
        next.floating.insert(it.key(), p);
    }
    QSet<QString> decorationIds;
    for (const QString &key : {QStringLiteral("relationships"), QStringLiteral("boundaries")}) {
        if (object.contains(key) && !object.value(key).isArray()) return reject(invalid);
        const auto entries = object.value(key).toArray();
        if (entries.size() > maxNodes) return reject(invalid);
        for (const auto &entry : entries) {
            const auto fields = entry.toObject();
            const QString id = fields.value("id").toString(), text = fields.value("text").toString();
            if (id.isEmpty() || id.size() > 128 || !isUnicode(id) || decorationIds.contains(id) ||
                !fields.value("text").isString() || text.size() > maxText || !isUnicode(text)) return reject(invalid);
            decorationIds.insert(id);
            if (key == "relationships") {
                Relationship link; link.id = id; link.text = text;
                link.from = fields.value("from").toString(); link.to = fields.value("to").toString();
                if (!next.nodes.contains(link.from) || !next.nodes.contains(link.to) || link.from == link.to ||
                    !fields.value("straight").isBool() || !point(fields.value("bend"), &link.bend)) return reject(invalid);
                link.straight = fields.value("straight").toBool(); next.relationships.append(link);
            } else {
                Boundary frame; frame.id = id; frame.text = text;
                if (!fields.value("members").isArray()) return reject(invalid);
                QSet<QString> seen;
                for (const auto &member : fields.value("members").toArray()) {
                    const QString topic = member.toString();
                    if (!next.nodes.contains(topic) || seen.contains(topic)) return reject(invalid);
                    seen.insert(topic); frame.members.append(topic);
                }
                if (frame.members.isEmpty()) return reject(invalid);
                next.boundaries.append(frame);
            }
        }
    }
    *this = std::move(next);
    if (error) error->clear();
    return true;
}

void Document::pruneDecorations() {
    for (auto it = floating.begin(); it != floating.end();)
        if (!nodes.value(root).children.contains(it.key())) it = floating.erase(it); else ++it;
    for (int i = relationships.size() - 1; i >= 0; --i)
        if (!nodes.contains(relationships[i].from) || !nodes.contains(relationships[i].to)) relationships.removeAt(i);
    for (int i = boundaries.size() - 1; i >= 0; --i) {
        auto &members = boundaries[i].members;
        for (int j = members.size() - 1; j >= 0; --j) if (!nodes.contains(members[j])) members.removeAt(j);
        if (members.isEmpty()) boundaries.removeAt(i);
    }
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
