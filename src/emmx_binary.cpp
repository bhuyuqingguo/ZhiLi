#include "emmx_binary.h"
#include <cstring>

namespace {
constexpr int shapeArrayTag = 110;
constexpr int centralTopicType = 27, mainTopic = 28, subTopic = 29;

class Reader {
public:
    Reader(const QByteArray &bytes, int start, int end) : pos(start), bytes(bytes), end(end) {}
    int pos;

    bool number(quint32 &value) {
        value = 0;
        for (int shift = 0; shift <= 28 && pos < end; shift += 7) {
            const auto next = quint8(bytes.at(pos++));
            if (shift == 28 && next > 15) return false;
            value |= quint32(next & 127) << shift;
            if (!(next & 128)) return true;
        }
        return false;
    }

    bool text(QString &value) {
        const char *start = bytes.constData() + pos;
        const int limit = qMin(end - pos, 4 * Document::maxText + 1);
        const auto *zero = static_cast<const char *>(std::memchr(start, 0, size_t(limit)));
        if (!zero) return false;
        const QByteArray raw(start, int(zero - start));
        value = QString::fromUtf8(raw);
        pos += raw.size() + 1;
        return value.toUtf8() == raw;
    }

    bool skipFloat() {
        if (pos >= end) return false;
        pos += bytes.at(pos) ? 4 : 1;
        return pos <= end;
    }

    bool numberFields(quint32 maximum) {
        quint32 tag, unused;
        while (number(tag)) {
            if (tag == 0) return true;
            if (tag > maximum || !number(unused)) return false;
        }
        return false;
    }

private:
    const QByteArray &bytes;
    int end;
};

bool readRuns(Reader &reader, QString &label) {
    quint32 count;
    if (!reader.number(count) || count > quint32(Document::maxText)) return false;
    for (quint32 i = 0; i < count; ++i) {
        quint32 field, unused;
        do {
            if (!reader.number(field)) return false;
            if (field == 4) {
                QString run;
                if (!reader.text(run) || run.size() > Document::maxText - label.size()) return false;
                label += run;
            } else if (field && (field > 3 || !reader.number(unused))) return false;
        } while (field);
    }
    return true;
}

bool readParagraphs(Reader &reader, QString &label) {
    quint32 count;
    if (!reader.number(count) || count > quint32(Document::maxText)) return false;
    for (quint32 i = 0; i < count; ++i) {
        if (i) label += '\n';
        quint32 field, unused;
        do {
            if (!reader.number(field)) return false;
            if (field == 4) {
                if (!readRuns(reader, label)) return false;
            } else if (field && (field > 3 || !reader.number(unused))) return false;
        } while (field);
        if (label.size() > Document::maxText) return false;
    }
    return true;
}

bool readText(Reader &reader, QString &label) {
    quint32 tag, unused;
    while (reader.number(tag)) {
        switch (tag) {
        case 0: return true;
        case 2: if (!reader.numberFields(9)) return false; break;
        case 3: if (!reader.skipFloat()) return false; break;
        case 6: if (!readParagraphs(reader, label)) return false; break;
        case 9: if (!reader.number(unused)) return false; break;
        default: return false;
        }
    }
    return false;
}

struct Shape {
    int offset = 0, fields = 0;
    quint32 id = 0, type = 0;
};

bool readHeader(const QByteArray &bytes, int offset, Shape &shape) {
    Reader reader(bytes, offset, bytes.size());
    quint32 tag;
    shape.offset = offset;
    if (!reader.number(tag) || tag != 1 || !reader.number(shape.id) || !shape.id ||
        !reader.number(tag) || tag != 2 || !reader.number(shape.type) || shape.type < 25 || shape.type > 100)
        return false;
    shape.fields = reader.pos;
    return true;
}

// The verified schema uses zero-terminated shape messages and a counted array.
// Require both the header sequence and its declared count to agree.
bool findShapes(const QByteArray &bytes, QVector<Shape> &shapes) {
    QVector<Shape> following;
    for (int at = 1; at + 3 < bytes.size(); ++at) {
        if (bytes.at(at - 1) != 0 || bytes.at(at) != 1) continue;
        Shape shape;
        if (readHeader(bytes, at, shape)) following.append(shape);
        if (following.size() > Document::maxNodes * 3) return false;
    }
    const int first = following.isEmpty() ? bytes.size() : following.first().offset;
    for (int at = 0; at < first; ++at) {
        if (quint8(bytes.at(at)) != shapeArrayTag) continue;
        Reader reader(bytes, at + 1, first);
        quint32 count;
        Shape shape;
        if (!reader.number(count) || count != quint32(following.size() + 1) ||
            reader.pos >= first || !readHeader(bytes, reader.pos, shape) || shape.fields > first)
            continue;
        if (!shapes.isEmpty()) return false;
        shapes.append(shape);
    }
    if (shapes.isEmpty()) return false;
    shapes += following;
    return true;
}

struct Topic {
    QString label;
    quint32 parent = 0;
    QVector<quint32> children;
};

bool readTopic(Reader &reader, Topic &topic) {
    quint32 previous = 2, tag;
    while (reader.number(tag)) {
        if (tag == 0 || tag > 14) return true;
        if (tag <= previous) return false;
        previous = tag;
        if (tag == 4) {
            if (!reader.numberFields(3)) return false;
        } else if (tag == 5) {
            if (!readText(reader, topic.label)) return false;
        } else if (tag == 14) {
            quint32 count, id;
            if (!reader.number(count) || count > quint32(Document::maxNodes)) return false;
            for (quint32 i = 0; i < count; ++i) {
                if (!reader.number(id) || !id) return false;
                topic.children.append(id);
            }
        } else {
            quint32 value;
            if (!reader.number(value)) return false;
            if (tag == 8) topic.parent = value;
        }
    }
    return false;
}
}

bool readBinaryEmmxPage(const QByteArray &bytes, Document *out, QString *error) {
    const auto invalid = [&] {
        if (error) *error = QStringLiteral("MindMaster 二进制页面结构损坏，或含有尚不支持的记录");
        return false;
    };
    QVector<Shape> shapes;
    if (bytes.size() < 6 || bytes.at(0) != 1 || bytes.at(bytes.size() - 1) != 0 || !findShapes(bytes, shapes))
        return invalid();
    QHash<quint32, Topic> topics;
    QSet<quint32> shapeIds;
    quint32 root = 0;
    for (int i = 0; i < shapes.size(); ++i) {
        const auto shape = shapes.at(i);
        if (shapeIds.contains(shape.id)) return invalid();
        shapeIds.insert(shape.id);
        if (shape.type != centralTopicType && shape.type != mainTopic && shape.type != subTopic) continue;
        const int end = i + 1 < shapes.size() ? shapes.at(i + 1).offset : bytes.size();
        Reader reader(bytes, shape.fields, end);
        Topic topic;
        if (!readTopic(reader, topic)) return invalid();
        if (shape.type == centralTopicType) {
            if (root || topic.parent) return invalid();
            root = shape.id;
        }
        topics.insert(shape.id, std::move(topic));
    }
    if (!root || topics.size() > Document::maxNodes) return invalid();
    Document next;
    next.root = QString::number(root);
    QSet<quint32> referenced;
    for (auto it = topics.cbegin(); it != topics.cend(); ++it) {
        Node node;
        node.id = QString::number(it.key());
        node.text = it->label;
        for (const auto child : it->children) {
            const auto target = topics.constFind(child);
            if (target == topics.cend() || referenced.contains(child) || target->parent != it.key()) return invalid();
            referenced.insert(child);
            node.children.append(QString::number(child));
        }
        next.nodes.insert(node.id, node);
    }
    if (referenced.size() + 1 != topics.size() || referenced.contains(root)) return invalid();
    Document checked;
    if (!checked.parse(next.serialize(), error)) return false;
    *out = std::move(checked);
    return true;
}
