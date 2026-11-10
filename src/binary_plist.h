#pragma once

#include <QtCore>

// Reads only the scalar and container types needed by MindNode. Access is
// by object reference so unrelated styles and resources are never decoded.
class BinaryPlist {
public:
    explicit BinaryPlist(const QByteArray &data) : data(data) {}
    static constexpr int missing = -1;

    bool open() {
        const int end = data.size() - 32;
        if (end < 8 || !data.startsWith("bplist00")) return false;
        const int offsetWidth = byte(end + 6);
        referenceWidth = byte(end + 7);
        quint64 count, top, start;
        if (!read(end + 8, 8, data.size(), count) || !read(end + 16, 8, data.size(), top) ||
            !read(end + 24, 8, data.size(), start) || count == 0 || count > 2000000 || top >= count ||
            start < 8 || start > quint64(end) || offsetWidth < 1 || offsetWidth > 8 ||
            referenceWidth < 1 || referenceWidth > 8 || count * offsetWidth > quint64(end) - start)
            return false;
        table = int(start);
        root = int(top);
        offsets.reserve(int(count));
        for (int i = 0; i < int(count); ++i) {
            quint64 offset;
            if (!read(table + i * offsetWidth, offsetWidth, end, offset) || offset < 8 || offset >= start)
                return false;
            offsets.append(int(offset));
        }
        return true;
    }

    int rootObject() const { return root; }

    bool dictionary(int id) const { return marker(id) == 0xd; }

    int value(int id, const QString &key) const {
        int at, count;
        if (!sequence(id, 0xd, referenceWidth * 2, at, count)) return missing;
        for (int i = 0; i < count; ++i) {
            QString name;
            if (!string(reference(at + i * referenceWidth), name)) return missing;
            if (name == key) return reference(at + (count + i) * referenceWidth);
        }
        return missing;
    }

    bool array(int id, QVector<int> &values) const {
        int at, count;
        if (!sequence(id, 0xa, referenceWidth, at, count) || count > 100000) return false;
        values.reserve(count);
        for (int i = 0; i < count; ++i) {
            const int entry = reference(at + i * referenceWidth);
            if (entry == missing) return false;
            values.append(entry);
        }
        return true;
    }

    bool string(int id, QString &result) const {
        const int kind = marker(id);
        if (kind != 5 && kind != 6 && kind != 7) return false;
        int at, count;
        if (!sequence(id, kind, kind == 6 ? 2 : 1, at, count) || count > 1024 * 1024) return false;
        if (kind == 6) {
            result.resize(count);
            for (int i = 0; i < count; ++i)
                result[i] = QChar(quint16(byte(at + i * 2) * 256 + byte(at + i * 2 + 1)));
            return true;
        }
        const QByteArray bytes = data.mid(at, count);
        result = QString::fromUtf8(bytes);
        return result.toUtf8() == bytes;
    }

    bool flag(int id) const { return id >= 0 && id < offsets.size() && byte(offsets.at(id)) == 9; }

private:
    int byte(int at) const { return uchar(data.at(at)); }

    bool read(int at, int width, int end, quint64 &value) const {
        if (width < 1 || width > 8 || at < 0 || at > end - width) return false;
        value = 0;
        for (int i = 0; i < width; ++i) value = (value << 8) | byte(at + i);
        return true;
    }

    int marker(int id) const {
        return id >= 0 && id < offsets.size() ? byte(offsets.at(id)) >> 4 : missing;
    }

    int reference(int at) const {
        quint64 id;
        return read(at, referenceWidth, table, id) && id < quint64(offsets.size()) ? int(id) : missing;
    }

    bool sequence(int id, int kind, int stride, int &at, int &count) const {
        if (marker(id) != kind) return false;
        at = offsets.at(id);
        quint64 length = byte(at++) & 15;
        if (length == 15) {
            if (at >= table) return false;
            const int size = byte(at++);
            if (size < 0x10 || size > 0x13) return false;
            const int width = 1 << (size & 15);
            if (!read(at, width, table, length)) return false;
            at += width;
        }
        if (length > quint64((table - at) / stride)) return false;
        count = int(length);
        return true;
    }

    const QByteArray &data;
    QVector<int> offsets;
    int table = 0, referenceWidth = 0, root = missing;
};
