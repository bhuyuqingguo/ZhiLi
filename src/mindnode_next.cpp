#include "mindnode_next.h"
#include <algorithm>

namespace {
struct Field {
    quint32 tag = 0;
    quint64 number = 0;
    QByteArray data;
    bool isBytes = false;
};

class Message {
public:
    bool read(const QByteArray &bytes) {
        int pos = 0;
        const auto varint = [&](quint64 &number) {
            number = 0;
            for (int shift = 0; shift < 64 && pos < bytes.size(); shift += 7) {
                const auto next = quint8(bytes.at(pos++));
                if (shift == 63 && next > 1) return false;
                number |= quint64(next & 127) << shift;
                if (!(next & 128)) return true;
            }
            return false;
        };
        while (pos < bytes.size()) {
            quint64 key;
            if (!varint(key) || key < 8 || key > 0xffffffffu || fields.size() >= 300000) return false;
            const int wire = key & 7;
            Field field;
            field.tag = quint32(key >> 3);
            if (wire == 0) {
                if (!varint(field.number)) return false;
                fields.append(field);
                continue;
            }
            quint64 length;
            if (wire == 2) {
                if (!varint(length)) return false;
                field.isBytes = true;
            } else if (wire == 1 || wire == 5) {
                length = wire == 1 ? 8 : 4;
            } else {
                return false;
            }
            if (length > quint64(bytes.size() - pos)) return false;
            if (field.isBytes) {
                // Views remain valid while the original Contents.data buffer is held by the caller.
                field.data = QByteArray::fromRawData(bytes.constData() + pos, int(length));
                fields.append(field);
            }
            pos += int(length);
        }
        return true;
    }

    QByteArray data(quint32 tag) const {
        for (const auto &field : fields)
            if (field.tag == tag && field.isBytes) return field.data;
        return {};
    }

    quint64 number(quint32 tag) const {
        for (const auto &field : fields)
            if (field.tag == tag && !field.isBytes) return field.number;
        return 0;
    }

    QVector<Field> fields;
};

bool readLabel(const QByteArray &bytes, QString &label) {
    Message text;
    if (!text.read(bytes)) return false;
    for (const auto &field : text.fields) {
        if (field.tag != 1 || !field.isBytes) continue;
        // Adjacent attributed spans can split a word. Newlines are stored in
        // the text runs themselves. Non-text runs carry only span lengths.
        Message span;
        if (!span.read(field.data)) return false;
        for (const auto &item : span.fields) {
            if (item.tag != 4 || !item.isBytes) continue;
            Message run;
            if (!run.read(item.data)) return false;
            const auto raw = run.data(1);
            const auto content = QString::fromUtf8(raw);
            if (content.toUtf8() != raw || content.size() > Document::maxText - label.size()) return false;
            label += content;
        }
    }
    return true;
}

QString topicId(const QByteArray &uuid) {
    return uuid.size() == 16 ? QString::fromLatin1(uuid.toHex()) : QString();
}
}

bool readMindNodeNext(const QByteArray &bytes, const QString &title, Document *out, QString *error) {
    const auto invalid = [&] {
        if (error) *error = QStringLiteral("MindNode Next 文件结构损坏，或使用了不受支持的主题记录");
        return false;
    };
    Message archive, snapshot, content, document, tree;
    if (!archive.read(bytes) || !snapshot.read(archive.data(1)) || !content.read(snapshot.data(2)) ||
        !document.read(content.data(1)) || !tree.read(document.data(3))) return invalid();
    const QString treeId = topicId(snapshot.data(1));
    if (treeId.isEmpty() || tree.data(1) != snapshot.data(1)) return invalid();
    Document next;
    QHash<QString, quint64> order;
    for (const auto &field : document.fields) {
        if (field.tag != 2 || !field.isBytes) continue;
        Message record, properties;
        if (!record.read(field.data) || !properties.read(record.data(2))) return invalid();
        Node node;
        node.id = topicId(record.data(1));
        if (node.id.isEmpty() || node.id == treeId || next.nodes.contains(node.id) ||
            next.nodes.size() >= Document::maxNodes || !readLabel(properties.data(11), node.text)) return invalid();
        order.insert(node.id, properties.number(7));
        next.nodes.insert(node.id, node);
    }
    if (next.nodes.isEmpty()) return invalid();
    QStringList roots;
    QSet<QString> linked;
    for (const auto &field : tree.fields) {
        if (field.tag != 2 || !field.isBytes) continue;
        Message relation;
        if (!relation.read(field.data)) return invalid();
        const QString id = topicId(relation.data(1));
        if (id == treeId) continue;
        const QString parent = topicId(relation.data(2));
        if (!next.nodes.contains(id) || linked.contains(id)) return invalid();
        if (parent == treeId) roots.append(id);
        else if (next.nodes.contains(parent)) next.nodes[parent].children.append(id);
        else return invalid();
        linked.insert(id);
    }
    if (linked.size() != next.nodes.size() || roots.isEmpty()) return invalid();
    const auto compare = [&](const QString &a, const QString &b) { return order.value(a) < order.value(b); };
    for (auto &node : next.nodes) std::stable_sort(node.children.begin(), node.children.end(), compare);
    if (roots.size() == 1) next.root = roots.first();
    else {
        std::stable_sort(roots.begin(), roots.end(), compare);
        Node root;
        root.id = treeId;
        root.text = title;
        root.children = roots;
        next.root = root.id;
        next.nodes.insert(root.id, root);
    }
    *out = std::move(next);
    return true;
}
