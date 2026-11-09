#include "importer.h"

#include <algorithm>
#include <limits>

// Compile only the pinned MIT raw inflater, with a caller-owned bounded buffer.
#define MINIZ_NO_MALLOC
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_USE_UNALIGNED_LOADS_AND_STORES 0
#define MINIZ_HAS_64BIT_REGISTERS 0
#include "vendor/miniz/miniz_tinfl.c"

namespace {
const quint64 expandedLimit = 100 * 1024 * 1024;
const int entryLimit = 4096;
const int xmlDepthLimit = Document::maxDepth * 3 + 64;
const int tokenLimit = 2000000;
const QString mmNamespace = QStringLiteral("http://schemas.mindjet.com/MindManager/Application/2003");

bool fail(QString *error, const QString &message) {
    if (error) *error = message;
    return false;
}

quint16 u16(const QByteArray &b, int p) {
    return quint8(b[p]) | (quint16(quint8(b[p + 1])) << 8);
}
quint32 u32(const QByteArray &b, int p) {
    return u16(b, p) | (quint32(u16(b, p + 2)) << 16);
}
quint32 checksum(const QByteArray &b) {
    quint32 crc = 0xffffffffu;
    for (unsigned char c : b) {
        crc ^= c;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

struct Entry {
    QString name;
    quint32 crc, compressed, expanded, offset;
    quint16 flags, method;
    int data;
};

// Deliberately small ZIP32 reader. No extraction and no filesystem paths opened
// from archive content. All entries are validated before any decompression.
class Archive {
public:
    QByteArray bytes;
    QMap<QString, Entry> entries;

    bool parse(QString *error) {
        const auto bad = [error]() { return fail(error, QStringLiteral("ZIP 文件损坏，或使用了暂不支持的 ZIP 格式")); };
        if (bytes.size() < 22 || bytes.size() > Document::maxBytes) return bad();
        int end = -1;
        const int first = qMax(0, bytes.size() - 22 - 65535);
        for (int p = bytes.size() - 22; p >= first; --p) {
            if (u32(bytes, p) == 0x06054b50u && p + 22 + u16(bytes, p + 20) == bytes.size()) {
                end = p;
                break;
            }
        }
        if (end < 0 || u16(bytes, end + 4) || u16(bytes, end + 6)) return bad();
        const quint16 count = u16(bytes, end + 10);
        const quint32 length = u32(bytes, end + 12), start = u32(bytes, end + 16);
        if (!count || count > entryLimit || count != u16(bytes, end + 8)
            || quint64(start) + length != quint64(end)) return bad();
        quint64 total = 0;
        quint64 cursor = start;
        QSet<QString> names;
        QVector<QPair<quint64, quint64>> spans;
        for (int n = 0; n < count; ++n) {
            if (cursor + 46 > quint64(end)) return bad();
            const int p = int(cursor);
            if (u32(bytes, p) != 0x02014b50u) return bad();
            const int nameLength = u16(bytes, p + 28), extra = u16(bytes, p + 30), comment = u16(bytes, p + 32);
            cursor += 46 + nameLength + extra + comment;
            if (!nameLength || cursor > quint64(end) || u16(bytes, p + 34)) return bad();
            Entry e;
            e.flags = u16(bytes, p + 8);
            e.method = u16(bytes, p + 10);
            e.crc = u32(bytes, p + 16);
            e.compressed = u32(bytes, p + 20);
            e.expanded = u32(bytes, p + 24);
            e.offset = u32(bytes, p + 42);
            if (e.flags & (1u | 0x40u | 0x2000u))
                return fail(error, QStringLiteral("不支持加密或密码保护的导图，请先在原软件中另存未加密副本"));
            if ((e.flags & ~(2u | 4u | 8u | 0x800u)) || (e.method != 0 && e.method != 8)) return bad();
            total += e.expanded;
            if (e.expanded > quint32(Document::maxBytes) || total > expandedLimit)
                return fail(error, QStringLiteral("导图解压后的数据超过安全限制"));
            if (e.compressed > quint32(bytes.size()) || e.offset >= start) return bad();
            const QByteArray rawName = bytes.mid(p + 46, nameLength);
            e.name = QString::fromUtf8(rawName);
            if (e.name.toUtf8() != rawName || e.name.contains(QChar(0))) return bad();
            e.name.replace('\\', '/');
            if (e.name.startsWith('/') || e.name.contains(':')) return bad();
            const QStringList pieces = e.name.split('/');
            for (int j = 0; j < pieces.size(); ++j)
                if (pieces[j] == "." || pieces[j] == ".." || (pieces[j].isEmpty() && j != pieces.size() - 1)) return bad();
            const QString folded = e.name.toCaseFolded();
            if (names.contains(folded)) return fail(error, QStringLiteral("ZIP 包含重复或大小写冲突的文件名"));
            names.insert(folded);
            if (e.name.endsWith('/') && (e.expanded || e.compressed)) return bad();
            const quint64 local = e.offset;
            if (local + 30 > start) return bad();
            const int l = int(local);
            if (u32(bytes, l) != 0x04034b50u || u16(bytes, l + 6) != e.flags || u16(bytes, l + 8) != e.method) return bad();
            const int localName = u16(bytes, l + 26), localExtra = u16(bytes, l + 28);
            const quint64 data = local + 30 + localName + localExtra;
            if (data + e.compressed > start || localName != nameLength
                || bytes.mid(l + 30, localName) != rawName) return bad();
            if (!(e.flags & 8u) && (u32(bytes, l + 14) != e.crc || u32(bytes, l + 18) != e.compressed
                || u32(bytes, l + 22) != e.expanded)) return bad();
            e.data = int(data);
            spans.append(qMakePair(local, data + e.compressed));
            entries.insert(e.name, e);
        }
        if (cursor != quint64(end)) return bad();
        std::sort(spans.begin(), spans.end());
        for (int i = 1; i < spans.size(); ++i)
            if (spans[i].first < spans[i - 1].second) return bad();
        return true;
    }

    bool read(const QString &name, QByteArray *out, QString *error) const {
        const auto it = entries.constFind(name);
        if (it == entries.cend()) return fail(error, QStringLiteral("导图缺少必要内容文件"));
        const Entry &e = it.value();
        QByteArray result;
        if (e.method == 0) {
            if (e.expanded != e.compressed) return fail(error, QStringLiteral("ZIP 内容长度不一致"));
            result = bytes.mid(e.data, int(e.compressed));
        } else {
            // Allocate exactly the checked advertised size. An overproducing
            // stream must fail, never grow this buffer based on input data.
            result.resize(qMax(1, int(e.expanded)));
            tinfl_decompressor state;
            tinfl_init(&state);
            size_t inSize = e.compressed, outSize = e.expanded;
            const tinfl_status status = tinfl_decompress(&state,
                reinterpret_cast<const mz_uint8 *>(bytes.constData() + e.data), &inSize,
                reinterpret_cast<mz_uint8 *>(result.data()), reinterpret_cast<mz_uint8 *>(result.data()),
                &outSize, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
            if (status != TINFL_STATUS_DONE || inSize != e.compressed || outSize != e.expanded)
                return fail(error, QStringLiteral("ZIP 压缩内容损坏或长度超限"));
            result.resize(int(e.expanded));
        }
        if (checksum(result) != e.crc) return fail(error, QStringLiteral("ZIP 内容校验失败"));
        *out = std::move(result);
        return true;
    }
};

struct XmlGuard {
    QXmlStreamReader xml;
    QStringList path;
    QStringList namespaces;
    int tokens = 0;
    explicit XmlGuard(const QByteArray &data) : xml(data) {}
    bool advance(QString *error) {
        xml.readNext();
        if (++tokens > tokenLimit) return fail(error, QStringLiteral("XML 内容过于复杂"));
        if (xml.isDTD() || xml.isEntityReference())
            return fail(error, QStringLiteral("为安全起见，不接受包含 DTD 或实体引用的 XML"));
        if (xml.isStartElement()) {
            path.append(xml.name().toString());
            namespaces.append(xml.namespaceUri().toString());
            if (path.size() > xmlDepthLimit) return fail(error, QStringLiteral("XML 层级过深"));
        }
        return true;
    }
    void endToken() { if (xml.isEndElement()) { path.removeLast(); namespaces.removeLast(); } }
};

bool parseMmap(const QByteArray &bytes, Document *out, QStringList *warnings, QString *error) {
    XmlGuard reader(bytes);
    auto &xml = reader.xml;
    struct Active { QString id; int depth; bool text = false; };
    QVector<Active> active;
    QSet<QString> sourceIds, omitted;
    Document next;
    bool rootSeen = false;
    while (!xml.atEnd()) {
        if (!reader.advance(error)) return false;
        const int depth = reader.path.size();
        const QString name = xml.name().toString();
        const bool ap = xml.namespaceUri() == mmNamespace;
        if (xml.isStartElement()) {
            if (depth == 1) {
                if (name != "Map" || !ap) return fail(error, QStringLiteral("不是受支持的 MindManager XML 导图"));
                rootSeen = true;
            }
            const bool rootTopic = depth == 3 && reader.path[1] == "OneTopic" && reader.namespaces[1] == mmNamespace;
            const bool childTopic = !active.isEmpty() && depth == active.last().depth + 2
                && reader.path[depth - 2] == "SubTopics" && reader.namespaces[depth - 2] == mmNamespace;
            if (ap && name == "Topic" && (rootTopic || childTopic)) {
                if (next.nodes.size() >= Document::maxNodes || active.size() > Document::maxDepth)
                    return fail(error, QStringLiteral("导图节点数量或层级超过限制"));
                const QString sourceId = xml.attributes().value("OId").toString();
                if (sourceId.isEmpty() || sourceIds.contains(sourceId))
                    return fail(error, QStringLiteral("MindManager 主题缺少 ID 或 ID 重复"));
                sourceIds.insert(sourceId);
                Node node;
                node.id = QStringLiteral("import-%1").arg(next.nodes.size() + 1);
                if (active.isEmpty()) {
                    if (!next.root.isEmpty()) return fail(error, QStringLiteral("暂不支持多个中心主题的 MindManager 文件"));
                    next.root = node.id;
                } else next.nodes[active.last().id].children.append(node.id);
                next.nodes.insert(node.id, node);
                Active item; item.id = node.id; item.depth = depth;
                active.append(item);
            } else if (ap && name == "Text" && !active.isEmpty() && depth == active.last().depth + 1) {
                if (active.last().text || !xml.attributes().hasAttribute("PlainText"))
                    return fail(error, QStringLiteral("MindManager 主题文字格式暂不支持"));
                const QString text = xml.attributes().value("PlainText").toString();
                if (text.size() > Document::maxText) return fail(error, QStringLiteral("主题文字超过长度限制"));
                next.nodes[active.last().id].text = text;
                active.last().text = true;
            }
            if (ap && name == "NotesGroup") omitted.insert(QStringLiteral("备注"));
            if (ap && (name == "OneImage" || name == "AttachmentGroup")) omitted.insert(QStringLiteral("图片和附件"));
            if (ap && name == "Hyperlink") omitted.insert(QStringLiteral("超链接"));
            if (ap && (name == "Relationship" || name == "Boundary")) omitted.insert(QStringLiteral("关系线、边界或概要"));
            if (ap && (name == "FloatingTopics" || name == "Callout")) omitted.insert(QStringLiteral("浮动主题或标注"));
            if (ap && (name == "Task" || name == "TextLabels" || name == "Icon" || name == "IconMarkers")) omitted.insert(QStringLiteral("任务、标签或图标"));
            if (ap && (name == "Custom" || name == "CustomProperties" || name == "SmartRules")) omitted.insert(QStringLiteral("自定义或动态数据"));
        } else if (xml.isEndElement() && !active.isEmpty() && depth == active.last().depth) {
            if (!active.last().text) return fail(error, QStringLiteral("MindManager 主题缺少可读取的文字"));
            active.removeLast();
        }
        reader.endToken();
    }
    if (xml.hasError() || !rootSeen || next.root.isEmpty()) return fail(error, QStringLiteral("MindManager XML 损坏或没有可导入的主题"));
    QStringList dropped = omitted.values(); dropped.sort();
    if (!dropped.isEmpty()) warnings->append(QStringLiteral("检测到以下内容未转换：%1。").arg(dropped.join(QStringLiteral("、"))));
    *out = std::move(next);
    return true;
}

struct EmmxTopic {
    Node node; QString parent; QStringList ordered;
    bool orderPresent = false, parentPresent = false, textPresent = false;
};

bool parseEmmx(const QByteArray &bytes, Document *out, QStringList *warnings, QString *error) {
    XmlGuard reader(bytes);
    auto &xml = reader.xml;
    QMap<QString, EmmxTopic> topics;
    QSet<QString> shapeIds;
    QStringList encounterOrder;
    QString current, root;
    bool rootSeen = false, inParagraph = false;
    int paragraphCount = 0;
    while (!xml.atEnd()) {
        if (!reader.advance(error)) return false;
        const int depth = reader.path.size();
        const QString name = xml.name().toString();
        if (xml.isStartElement()) {
            if (!xml.namespaceUri().isEmpty()) return fail(error, QStringLiteral("MindMaster 页面使用暂不支持的 XML 命名空间"));
            if (depth == 1) {
                if (name != "Page" || !xml.namespaceUri().isEmpty()) return fail(error, QStringLiteral("不是受支持的 MindMaster XML 页面"));
                rootSeen = true;
            }
            if (depth == 2 && name == "Shape") {
                current.clear(); paragraphCount = 0;
                const QString shapeId = xml.attributes().value("ID").toString();
                if (!shapeId.isEmpty()) {
                    if (shapeIds.contains(shapeId)) return fail(error, QStringLiteral("MindMaster 图形 ID 重复"));
                    shapeIds.insert(shapeId);
                }
                const QString type = xml.attributes().value("Type").toString();
                if (type == "MainIdea" || type == "MainTopic" || type == "SubTopic") {
                    const QString id = xml.attributes().value("ID").toString();
                    if (id.isEmpty() || topics.contains(id) || topics.size() >= Document::maxNodes)
                        return fail(error, QStringLiteral("MindMaster 主题 ID 或节点数量不合法"));
                    if (type == "MainIdea") {
                        if (!root.isEmpty()) return fail(error, QStringLiteral("暂不支持多个中心主题的 MindMaster 页面"));
                        root = id;
                    }
                    EmmxTopic topic; topic.node.id = id;
                    topics.insert(id, topic); encounterOrder.append(id); current = id;
                }
            }
            if (!current.isEmpty()) {
                if (depth == 4 && reader.path[2] == "LevelData") {
                    if (name == "Super") {
                        if (topics[current].parentPresent) return fail(error, QStringLiteral("MindMaster 父级引用重复"));
                        topics[current].parentPresent = true;
                        topics[current].parent = xml.attributes().value("V").toString();
                    }
                    if (name == "SubLevel") {
                        if (topics[current].orderPresent) return fail(error, QStringLiteral("MindMaster 子级顺序重复"));
                        topics[current].orderPresent = true;
                        const QString value = xml.attributes().value("V").toString();
                        if (!value.isEmpty()) topics[current].ordered = value.split(';');
                    }
                }
                if (depth == 6 && reader.path.mid(2) == (QStringList{"Text", "TextBlock", "Text", "pp"})) {
                    inParagraph = true;
                    topics[current].textPresent = true;
                    if (paragraphCount++) topics[current].node.text += '\n';
                }
            }
        } else if (xml.isCharacters() && !current.isEmpty() && inParagraph
            && depth == 7 && name.isEmpty() && reader.path.last() == "tp") {
            QString &text = topics[current].node.text;
            if (text.size() + xml.text().size() > Document::maxText)
                return fail(error, QStringLiteral("主题文字超过长度限制"));
            text += xml.text();
        } else if (xml.isEndElement()) {
            if (depth == 6 && name == "pp") inParagraph = false;
            if (depth == 2 && name == "Shape") current.clear();
        }
        reader.endToken();
    }
    if (xml.hasError() || !rootSeen || root.isEmpty()) return fail(error, QStringLiteral("MindMaster 页面损坏或没有中心主题"));
    Document next; next.root = root;
    QMap<QString, QStringList> byParent;
    for (const QString &id : encounterOrder) {
        const EmmxTopic &t = topics[id];
        if (!t.textPresent) return fail(error, QStringLiteral("MindMaster 主题文字格式暂不支持"));
        if (id == root) {
            if (!t.parent.isEmpty()) return fail(error, QStringLiteral("MindMaster 中心主题存在父级引用"));
        } else {
            if (!topics.contains(t.parent)) return fail(error, QStringLiteral("MindMaster 主题缺少有效父级"));
            byParent[t.parent].append(id);
        }
    }
    bool inferredOrder = false;
    for (const QString &id : encounterOrder) {
        EmmxTopic &t = topics[id];
        if (t.orderPresent) {
            QSet<QString> found;
            for (const QString &child : t.ordered) {
                if (!topics.contains(child) || topics[child].parent != id || found.contains(child))
                    return fail(error, QStringLiteral("MindMaster 子主题顺序或父子引用不一致"));
                found.insert(child);
            }
            if (found.size() != byParent.value(id).size()) return fail(error, QStringLiteral("MindMaster 子主题引用不完整"));
            t.node.children = t.ordered;
        } else {
            t.node.children = byParent.value(id);
            if (!t.node.children.isEmpty()) inferredOrder = true;
        }
        next.nodes.insert(id, t.node);
    }
    if (inferredOrder) warnings->append(QStringLiteral("部分主题缺少顺序信息，已按文件中的出现顺序排列。"));
    warnings->append(QStringLiteral("MindMaster XML 导入仅保留主题文字和层级；备注、图片、附件、链接、关系线、标签、任务及其他图形未转换。"));
    *out = std::move(next);
    return true;
}
}

bool importMindMap(const QString &path, Document *out, QStringList *warnings, QString *error) {
    if (!out) return fail(error, QStringLiteral("没有导入目标"));
    const QString extension = QFileInfo(path).suffix().toLower();
    if (extension != "mmap" && extension != "emmx") return fail(error, QStringLiteral("请选择 .mmap 或受支持的单页 XML .emmx 文件"));
    if (!QFileInfo(path).isFile()) return fail(error, QStringLiteral("文件不存在或不是普通文件"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, file.errorString());
    if (file.size() > Document::maxBytes) return fail(error, QStringLiteral("文件超过 50 MiB"));
    Archive archive; archive.bytes = file.read(Document::maxBytes + 1);
    if (file.error() != QFileDevice::NoError) return fail(error, file.errorString());
    if (archive.bytes.size() > Document::maxBytes) return fail(error, QStringLiteral("文件超过 50 MiB"));
    if (!archive.parse(error)) return false;
    Document next; QStringList messages;
    QByteArray xml;
    if (extension == "mmap") {
        if (!archive.read(QStringLiteral("Document.xml"), &xml, error) || !parseMmap(xml, &next, &messages, error)) return false;
    } else {
        QStringList pages;
        for (const QString &name : archive.entries.keys()) {
            if (name.startsWith("mmpage/") && name.endsWith(".bin"))
                return fail(error, QStringLiteral("此 .emmx 使用暂不支持的二进制页面。请在 MindMaster / EdrawMind 中导出为 MindManager (.mmap) 后导入；导出可能需要付费版。"));
            if (name.startsWith("page/") && name.endsWith(".xml")) pages.append(name);
        }
        if (!archive.entries.contains("document.xml") || pages.size() != 1)
            return fail(error, QStringLiteral("目前只支持含一个 XML 页面的旧版 .emmx；多页或其他格式请先在原软件中分别导出为 .mmap。"));
        if (!archive.read(pages.first(), &xml, error) || !parseEmmx(xml, &next, &messages, error)) return false;
    }
    // Reuse the native graph/Unicode/size validation, and commit atomically.
    Document validated;
    if (!validated.parse(next.serialize(), error)) return false;
    messages.prepend(QStringLiteral("已转换为枝理的可编辑副本，仅保留受支持的主题文字和层级；原文件未修改。原有样式、布局及其他专有内容不保证保留，请检查后另存为 .smap。"));
    *out = std::move(validated);
    if (warnings) *warnings = messages;
    if (error) error->clear();
    return true;
}
