#include "importer.h"
#include "binary_plist.h"
#include "clipboard.h"
#include "emmx_binary.h"
#include "mindnode_next.h"

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

QString appendTopic(Document &document, const QString &parent, const QString &text) {
    Node node;
    node.id = QStringLiteral("import-%1").arg(document.nodes.size() + 1);
    node.text = text;
    if (parent.isEmpty()) document.root = node.id;
    else document.nodes[parent].children.append(node.id);
    document.nodes.insert(node.id, node);
    return node.id;
}

struct ExchangePage {
    QString title;
    QStringList order;
    QHash<QString, QString> labels;
    QVector<QPair<QString, QString>> links;

    bool add(const QString &id, const QString &label, QString *error) {
        if (id.isEmpty() || labels.contains(id) || order.size() >= Document::maxNodes || label.size() > Document::maxText)
            return fail(error, QStringLiteral("形状编号无效、重复，或文字与节点数超限"));
        order.append(id);
        labels.insert(id, label);
        return true;
    }
};

bool pagesToDocument(const QVector<ExchangePage> &pages, const QString &title,
                     Document *out, QStringList *warnings, QString *error) {
    if (pages.isEmpty()) return fail(error, QStringLiteral("文件没有可转换的页面"));
    Document next;
    int omittedLinks = 0;
    if (pages.size() > 1) appendTopic(next, {}, title);
    for (const auto &page : pages) {
        QHash<QString, QString> parents, groups;
        QHash<QString, QStringList> children;
        auto representative = [&](QString id) {
            QString root = id;
            while (groups.contains(root)) root = groups.value(root);
            while (id != root) {
                const QString parent = groups.value(id);
                groups[id] = root;
                id = parent;
            }
            return root;
        };
        for (const auto &link : page.links) {
            if (!page.labels.contains(link.first) || !page.labels.contains(link.second) || parents.contains(link.second)) {
                ++omittedLinks;
                continue;
            }
            const QString from = representative(link.first), to = representative(link.second);
            if (from == to) {
                ++omittedLinks;
                continue;
            }
            groups.insert(to, from);
            parents.insert(link.second, link.first);
            children[link.first].append(link.second);
        }
        QStringList roots;
        for (const auto &id : page.order)
            if (!parents.contains(id)) roots.append(id);
        if (qint64(next.nodes.size()) + page.order.size() + (pages.size() > 1 || roots.size() != 1) > Document::maxNodes)
            return fail(error, QStringLiteral("导图节点数量超过限制"));
        const bool wrap = pages.size() > 1 || roots.size() != 1;
        const QString pageRoot = wrap ? appendTopic(next, next.root, page.title) : QString();
        struct Item { QString id, parent; int depth; };
        QVector<Item> pending;
        const int startDepth = pages.size() > 1 ? 2 : wrap ? 1 : 0;
        for (auto it = roots.crbegin(); it != roots.crend(); ++it) pending.append(Item{*it, pageRoot, startDepth});
        while (!pending.isEmpty()) {
            const auto item = pending.takeLast();
            if (item.depth > Document::maxDepth) return fail(error, QStringLiteral("转换后的导图层级超过限制"));
            const QString id = appendTopic(next, item.parent, page.labels.value(item.id));
            const auto &branch = children.value(item.id);
            for (auto it = branch.crbegin(); it != branch.crend(); ++it) pending.append(Item{*it, id, item.depth + 1});
        }
    }
    if (omittedLinks)
        warnings->append(QStringLiteral("有 %1 条连线因缺少端点、循环或多父级未转换为导图分支。").arg(omittedLinks));
    *out = std::move(next);
    return true;
}

void useSingleTopLevelTopic(Document &document) {
    const auto roots = document.nodes.value(document.root).children;
    if (roots.size() == 1) {
        document.nodes.remove(document.root);
        document.root = roots.first();
    }
}

bool decodeOutline(const QByteArray &bytes, QString &text, QString *error) {
    QByteArray utf8 = bytes;
    if (utf8.startsWith("\xef\xbb\xbf")) utf8.remove(0, 3);
    text = QString::fromUtf8(utf8);
    if (text.toUtf8() != utf8 || text.contains(QChar(0)))
        return fail(error, QStringLiteral("文本大纲须采用 UTF-8 编码"));
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    return true;
}

bool parseMermaid(const QStringList &lines, const QString &title, Document *out, QString *error) {
    static const QRegularExpression shape(
        QStringLiteral(R"(^[^\s()[\]{}]+(?:\(\((.*)\)\)|\)\)(.*)\(\(|\{\{(.*)\}\}|\[(.*)\]|\((.*)\)|\)(.*)\()$)"));
    static const QRegularExpression lineBreak(QStringLiteral("<br\\s*/?>"), QRegularExpression::CaseInsensitiveOption);
    QStringList outline;
    bool inMap = false, frontMatter = false;
    for (int i = 0; i < lines.size(); ++i) {
        const auto &line = lines.at(i);
        QString label = line.trimmed();
        if (i == 0 && label == "---") { frontMatter = true; continue; }
        if (frontMatter) {
            if (label == "---") frontMatter = false;
            continue;
        }
        if (label.isEmpty() || label.startsWith("%%") || label.startsWith("::")) continue;
        if (!inMap) {
            if (label != "mindmap") return fail(error, QStringLiteral("仅支持 Mermaid mindmap 思维导图"));
            inMap = true;
            continue;
        }
        const auto match = shape.match(label);
        if (match.hasMatch()) {
            for (int part = 1; part <= match.lastCapturedIndex(); ++part)
                if (match.capturedStart(part) >= 0) { label = match.captured(part); break; }
            if (label.size() >= 2 && label.startsWith('"') && label.endsWith('"'))
                label = label.mid(1, label.size() - 2);
        }
        label.replace(lineBreak, QStringLiteral(" "));
        int prefix = 0;
        while (prefix < line.size() && (line.at(prefix) == ' ' || line.at(prefix) == '\t')) ++prefix;
        outline.append(line.left(prefix) + label);
    }
    if (!inMap || !BranchClipboard::parseOutline(outline.join('\n'), title, out, error)) {
        if (!inMap) return fail(error, QStringLiteral("没有 Mermaid mindmap 内容"));
        return false;
    }
    if (out->nodes.value(out->root).children.size() != 1)
        return fail(error, QStringLiteral("Mermaid 思维导图须有一个中心主题"));
    useSingleTopLevelTopic(*out);
    return true;
}

bool parseMarkdown(const QStringList &lines, const QString &title, Document *out, QString *error) {
    static const QRegularExpression heading(QStringLiteral(R"(^ {0,3}(#{1,6})[ \t]+(.*)$)"));
    static const QRegularExpression endHashes(QStringLiteral(R"([ \t]+#+[ \t]*$)"));
    static const QRegularExpression bullet(QStringLiteral(R"(^([ \t]*)(?:[-*+]|[0-9]{1,9}[.)])[ \t]+(.*)$)"));
    static const QRegularExpression fence(QStringLiteral(R"(^[ \t]*(`{3,}|~{3,})(.*)$)"));
    static const QRegularExpression separator(QStringLiteral(R"(^(?:-{3,}|\*{3,}|_{3,})$)"));
    Document next;
    appendTopic(next, {}, title);
    QVector<QPair<int, QString>> headings, items;
    QString block;
    for (int i = 0; i < lines.size(); ++i) {
        const auto &line = lines.at(i);
        QString label = line.trimmed();
        if (i == 0 && label == "---") {
            const int end = lines.indexOf(QStringLiteral("---"), 1);
            if (end >= 0) { i = end; continue; }
        }
        const auto fenced = fence.match(line);
        if (!block.isEmpty()) {
            if (fenced.hasMatch() && fenced.captured(1).startsWith(block) && fenced.captured(2).trimmed().isEmpty())
                block.clear();
            continue;
        }
        if (fenced.hasMatch()) {
            if (fenced.captured(2).trimmed().compare("mermaid", Qt::CaseInsensitive) == 0) {
                QStringList diagram;
                for (int j = i + 1; j < lines.size() && !lines.at(j).trimmed().startsWith(fenced.captured(1)); ++j)
                    diagram.append(lines.at(j));
                if (!diagram.isEmpty() && diagram.first().trimmed() == "mindmap")
                    return parseMermaid(diagram, title, out, error);
            }
            block = fenced.captured(1);
            continue;
        }
        if (label.isEmpty() || separator.match(label).hasMatch()) continue;
        const auto header = heading.match(line), listItem = bullet.match(line);
        int headingLevel = header.hasMatch() ? header.capturedLength(1) : 0;
        if (!headingLevel && !listItem.hasMatch() && i + 1 < lines.size()) {
            static const QRegularExpression underline(QStringLiteral(R"(^ {0,3}(=+|-+)[ \t]*$)"));
            const auto setext = underline.match(lines.at(i + 1));
            if (setext.hasMatch()) {
                headingLevel = setext.captured(1).startsWith('=') ? 1 : 2;
                ++i;
            }
        }
        QString parent;
        int indent = 0;
        if (headingLevel) {
            items.clear();
            while (!headings.isEmpty() && headings.last().first >= headingLevel) headings.removeLast();
            if (header.hasMatch()) label = header.captured(2).remove(endHashes).trimmed();
        } else if (listItem.hasMatch()) {
            for (const auto ch : listItem.captured(1)) indent = ch == '\t' ? (indent / 4 + 1) * 4 : indent + 1;
            while (!items.isEmpty() && items.last().first >= indent) items.removeLast();
            label = listItem.captured(2).trimmed();
            if (!items.isEmpty()) parent = items.last().second;
        } else {
            items.clear();
        }
        if (parent.isEmpty()) parent = headings.isEmpty() ? next.root : headings.last().second;
        if (next.nodes.size() >= Document::maxNodes || label.size() > Document::maxText ||
            headings.size() + items.size() >= Document::maxDepth)
            return fail(error, QStringLiteral("Markdown 的节点数、层级或主题文字超过限制"));
        const QString id = appendTopic(next, parent, label);
        if (headingLevel) headings.append(qMakePair(headingLevel, id));
        else if (listItem.hasMatch()) items.append(qMakePair(indent, id));
    }
    if (next.nodes.size() < 2) return fail(error, QStringLiteral("Markdown 没有可转换的大纲内容"));
    useSingleTopLevelTopic(next);
    *out = std::move(next);
    return true;
}

bool parseCanvas(const QByteArray &bytes, const QString &title, Document *out,
                 QStringList *warnings, QString *error) {
    const auto canvas = QJsonDocument::fromJson(bytes).object();
    if (!canvas.value("nodes").isArray() || !canvas.value("edges").isArray())
        return fail(error, QStringLiteral("JSON Canvas 文件结构无效"));
    ExchangePage page;
    page.title = title;
    for (const auto &value : canvas.value("nodes").toArray()) {
        const auto node = value.toObject();
        const QString type = node.value("type").toString();
        QString label;
        if (type == "text") label = node.value("text").toString();
        else if (type == "file") label = node.value("file").toString() + node.value("subpath").toString();
        else if (type == "link") label = node.value("url").toString();
        else if (type == "group") label = node.value("label").toString();
        else return fail(error, QStringLiteral("JSON Canvas 包含无法识别的节点类型"));
        if (!page.add(node.value("id").toString(), label, error)) return false;
    }
    const auto edges = canvas.value("edges").toArray();
    if (edges.size() > tokenLimit) return fail(error, QStringLiteral("JSON Canvas 连线数量超过限制"));
    for (const auto &value : edges) {
        const auto edge = value.toObject();
        page.links.append(qMakePair(edge.value("fromNode").toString(), edge.value("toNode").toString()));
    }
    warnings->append(QStringLiteral("JSON Canvas 转换文字和可映射的连接；文件和网址只保留引用文字，原位置和样式未转换。"));
    return pagesToDocument({page}, title, out, warnings, error);
}

bool parseOpml(const QByteArray &bytes, const QString &fileTitle, Document *out,
               QStringList *warnings, QString *error) {
    XmlGuard reader(bytes);
    auto &xml = reader.xml;
    Document next;
    appendTopic(next, QString(), fileTitle);
    QStringList active;
    QString title;
    bool explicitRoot = false;
    bool bodySeen = false, inBody = false, titleSeen = false, inTitle = false;
    while (!xml.atEnd()) {
        if (!reader.advance(error)) return false;
        const int depth = reader.path.size();
        const bool core = xml.namespaceUri().isEmpty();
        const auto name = xml.name();
        if (xml.isStartElement()) {
            if (depth == 1) {
                const auto version = xml.attributes().value("version");
                if (!core || name != "opml" || (version != "1.0" && version != "1.1" && version != "2.0"))
                    return fail(error, QStringLiteral("请选择 OPML 1.0、1.1 或 2.0 文件"));
                explicitRoot = xml.attributes().value("urn:zhili:exchange:1", "center") == "outline";
            }
            if (core && depth == 2 && name == "body") {
                if (bodySeen) return fail(error, QStringLiteral("OPML 文件含有多个正文"));
                bodySeen = inBody = true;
            }
            if (core && depth == 3 && name == "title" && reader.path[1] == "head"
                && reader.namespaces[1].isEmpty()) {
                if (titleSeen) return fail(error, QStringLiteral("OPML 文件含有多个标题"));
                titleSeen = inTitle = true;
            }
            if (core && inBody && name == "outline" && depth == active.size() + 3) {
                if (next.nodes.size() >= Document::maxNodes || active.size() >= Document::maxDepth)
                    return fail(error, QStringLiteral("导图节点数量或层级超过限制"));
                const auto attributes = xml.attributes();
                if (!attributes.hasAttribute("text")) return fail(error, QStringLiteral("OPML 条目缺少主题文字"));
                const QString text = attributes.value("text").toString();
                if (text.size() > Document::maxText) return fail(error, QStringLiteral("主题文字超过长度限制"));
                active.append(appendTopic(next, active.isEmpty() ? next.root : active.last(), text));
            }
        } else if (xml.isCharacters() && inTitle) {
            if (title.size() + xml.text().size() > Document::maxText)
                return fail(error, QStringLiteral("主题文字超过长度限制"));
            title += xml.text();
        } else if (xml.isEndElement()) {
            if (depth == 3 && inTitle) inTitle = false;
            if (depth == 2 && inBody) inBody = false;
            if (!active.isEmpty() && depth == active.size() + 2) active.removeLast();
        }
        reader.endToken();
    }
    if (xml.hasError() || !bodySeen || next.nodes.size() < 2)
        return fail(error, QStringLiteral("OPML 文件损坏或没有可导入的条目"));
    if (explicitRoot) {
        const QString oldRoot = next.root;
        const auto roots = next.nodes.value(oldRoot).children;
        if (roots.size() != 1) return fail(error, QStringLiteral("OPML 中心主题结构无效"));
        next.root = roots.first();
        next.nodes.remove(oldRoot);
    } else if (!title.trimmed().isEmpty()) {
        next.nodes[next.root].text = title.trimmed();
    }
    warnings->append(QStringLiteral("OPML 以文档标题或文件名作为中心主题，保留条目文字和顺序。链接、备注和扩展字段未转换，条目中的 HTML 标记按文字保留。"));
    *out = std::move(next);
    return true;
}

bool readRichTopic(XmlGuard &reader, QString *text, QString *error) {
    auto &xml = reader.xml;
    const int richDepth = reader.path.size();
    QString plain;
    int skipDepth = 0;
    bool space = false;
    while (!xml.atEnd()) {
        if (!reader.advance(error)) return false;
        const int depth = reader.path.size();
        const QString name = xml.name().toString().toLower();
        if (xml.isEndElement() && depth == richDepth) {
            reader.endToken();
            *text = plain.trimmed();
            return true;
        }
        if (skipDepth) {
            if (xml.isEndElement() && depth == skipDepth) skipDepth = 0;
        } else if (xml.isStartElement() && (name == "head" || name == "script" || name == "style")) {
            skipDepth = depth;
        } else if ((xml.isStartElement() && (name == "br" || name == "p" || name == "div" || name == "li"))
                   || (xml.isEndElement() && (name == "p" || name == "div" || name == "li"))) {
            if (!plain.isEmpty() && !plain.endsWith('\n')) plain += '\n';
            space = false;
        } else if (xml.isCharacters()) {
            for (const QChar character : xml.text()) {
                if (character == ' ' || character == '\t' || character == '\n' || character == '\r') {
                    space = true;
                    continue;
                }
                if (space && !plain.isEmpty() && !plain.endsWith('\n')) plain += ' ';
                space = false;
                plain += character;
                if (plain.size() > Document::maxText) return fail(error, QStringLiteral("主题文字超过长度限制"));
            }
        }
        if (plain.size() > Document::maxText) return fail(error, QStringLiteral("主题文字超过长度限制"));
        reader.endToken();
    }
    return fail(error, QStringLiteral("FreeMind / Freeplane 富文本主题损坏"));
}

bool readLegacyRichTopic(const QString &html, QString *text) {
    // Older maps store Swing HTML in TEXT. Its void tags are not XML elements
    // until closed. Keep the original string if the legacy markup cannot be read.
    static const QRegularExpression voidTag(QStringLiteral(
        "<(br|img|hr|meta|link|input|base|area|col|embed|param|source|track|wbr)"
        "(\\b(?:[^<>'\"]|'[^']*'|\"[^\"]*\")*?)(?<!/)>"), QRegularExpression::CaseInsensitiveOption);
    QString xml = html.trimmed();
    xml.replace(voidTag, QStringLiteral("<\\1\\2/>"));
    xml.replace(QStringLiteral("&nbsp;"), QStringLiteral("&#160;"));
    XmlGuard reader(xml.toUtf8());
    while (!reader.xml.atEnd()) {
        if (!reader.advance(nullptr)) return false;
        if (reader.xml.isStartElement()) break;
    }
    if (!reader.xml.isStartElement() || !readRichTopic(reader, text, nullptr)) return false;
    while (!reader.xml.atEnd()) {
        if (!reader.advance(nullptr)) return false;
        reader.endToken();
    }
    return !reader.xml.hasError();
}

bool parseMindMeister(const QByteArray &data, Document *out, QStringList *warnings, QString *error) {
    const auto map = QJsonDocument::fromJson(data).object();
    const auto root = map.value("root").toObject();
    struct Topic { QJsonObject fields; QString parent; int depth; };
    QVector<Topic> pending{Topic{root, {}, 0}};
    Document next;
    while (!pending.isEmpty()) {
        const auto topic = pending.takeLast();
        const auto title = topic.fields.value("title"), children = topic.fields.value("children");
        if (!title.isString() || (!children.isUndefined() && !children.isArray()))
            return fail(error, QStringLiteral("MindMeister 主题结构无效"));
        const auto text = title.toString();
        if (topic.depth > Document::maxDepth || next.nodes.size() >= Document::maxNodes || text.size() > Document::maxText)
            return fail(error, QStringLiteral("MindMeister 节点数、层级或主题文字超过限制"));
        const auto id = appendTopic(next, topic.parent, text);
        const auto branches = children.toArray();
        if (branches.size() > Document::maxNodes - next.nodes.size() - pending.size())
            return fail(error, QStringLiteral("MindMeister 节点数量超过限制"));
        for (int i = branches.size() - 1; i >= 0; --i)
            pending.append(Topic{branches.at(i).toObject(), id, topic.depth + 1});
    }
    warnings->append(QStringLiteral("MindMeister 保留主题文字、层级和顺序。备注、附件、关系线和原始样式未转换。"));
    *out = std::move(next);
    return true;
}

bool parseMindomo(const QByteArray &data, const QString &title, Document *out,
                  QStringList *warnings, QString *error) {
    const auto nodes = QJsonDocument::fromJson(data).array();
    if (nodes.isEmpty() || nodes.size() > Document::maxNodes)
        return fail(error, QStringLiteral("Mindomo 节点列表为空、损坏或数量超限"));
    Document next;
    QStringList roots;
    QHash<QString, QString> ids;
    struct Source { QString id, parent; int order; };
    QVector<Source> sources;
    bool usedPlaceholder = false;
    for (const auto &entry : nodes) {
        const auto fields = entry.toObject();
        if (fields.value("deleted") == QJsonValue(true) ||
            fields.value("isTemplate") == QJsonValue(true) || fields.value("isTemplate").toInt() == 1) continue;
        const QString sourceId = fields.value("id").toString();
        const auto label = fields.value("title");
        if (sourceId.isEmpty() || ids.contains(sourceId) || !label.isString())
            return fail(error, QStringLiteral("Mindomo 包含无效或重复的主题"));
        QString text = label.toString();
        if (text.isEmpty()) {
            const auto placeholder = fields.value("template").toObject().value("placeholder").toString();
            if (!placeholder.isEmpty()) {
                text = placeholder;
                usedPlaceholder = true;
            }
        }
        if (text.size() > Document::maxText) return fail(error, QStringLiteral("主题文字超过长度限制"));
        const QString id = appendTopic(next, {}, text);
        next.nodes[id].collapsed = fields.value("style").toObject().value("expanded") == QJsonValue(false);
        ids.insert(sourceId, id);
        sources.append(Source{id, fields.value("parentId").toString(), fields.value("index").toInt()});
    }
    std::stable_sort(sources.begin(), sources.end(), [](const Source &a, const Source &b) {
        return a.order < b.order;
    });
    for (const auto &entry : sources) {
        if (entry.parent.isEmpty()) {
            roots.append(entry.id);
        } else {
            const QString parent = ids.value(entry.parent);
            if (parent.isEmpty()) return fail(error, QStringLiteral("Mindomo 主题的父级不存在"));
            next.nodes[parent].children.append(entry.id);
        }
    }
    if (roots.isEmpty()) return fail(error, QStringLiteral("Mindomo 缺少中心主题"));
    if (roots.size() == 1) next.root = roots.first();
    else {
        next.nodes[appendTopic(next, {}, title)].children = roots;
    }
    warnings->append(QStringLiteral("Mindomo 保留主题文字、层级、顺序和折叠状态。附件、样式、备注及修改历史未转换。"));
    if (usedPlaceholder) warnings->append(QStringLiteral("模板空白主题使用其可见占位文字。"));
    *out = std::move(next);
    return true;
}

bool parseMindNode(const QByteArray &data, const QString &title, Document *out,
                   QStringList *warnings, QString *error) {
    BinaryPlist plist(data);
    if (!plist.open()) return fail(error, QStringLiteral("MindNode 文件不含受支持的二进制属性列表"));
    const auto root = plist.rootObject();
    const auto canvas = plist.value(root, "canvas");
    QVector<int> roots;
    if (canvas != BinaryPlist::missing) {
        QVector<int> maps;
        if (!plist.array(plist.value(canvas, "mindMaps"), maps))
            return fail(error, QStringLiteral("MindNode 画布结构无效"));
        for (const int map : maps) {
            const int id = plist.value(map, "mainNode");
            if (!plist.dictionary(id)) return fail(error, QStringLiteral("MindNode 缺少中心主题"));
            roots.append(id);
        }
    } else {
        const auto map = plist.value(root, "mindMap");
        if (!plist.array(plist.value(map, "mainNodes"), roots))
            return fail(error, QStringLiteral("MindNode 文件缺少受支持的主题列表"));
    }
    if (roots.isEmpty()) return fail(error, QStringLiteral("MindNode 文件中没有主题"));
    Document next;
    if (roots.size() > 1) appendTopic(next, {}, title);
    struct Topic { int reference; QString parent; int depth; };
    QVector<Topic> pending;
    for (auto it = roots.crbegin(); it != roots.crend(); ++it)
        pending.append(Topic{*it, next.root, roots.size() > 1 ? 1 : 0});
    QSet<int> seen;
    bool markupKept = false;
    while (!pending.isEmpty()) {
        const auto topic = pending.takeLast();
        if (topic.depth > Document::maxDepth || next.nodes.size() >= Document::maxNodes || seen.contains(topic.reference))
            return fail(error, QStringLiteral("MindNode 节点数、层级超过限制，或包含重复主题"));
        seen.insert(topic.reference);
        const int field = plist.value(topic.reference, "title");
        QString label;
        if (!plist.string(field, label) && !plist.string(plist.value(field, "text"), label))
            return fail(error, QStringLiteral("MindNode 主题标题使用了不受支持的编码"));
        if (label.startsWith('<')) {
            QString plain;
            if (readLegacyRichTopic("<html>" + label + "</html>", &plain)) label = plain;
            else markupKept = true;
        }
        if (label.size() > Document::maxText) return fail(error, QStringLiteral("主题文字超过长度限制"));
        const auto id = appendTopic(next, topic.parent, label);
        next.nodes[id].collapsed = plist.flag(plist.value(topic.reference, "hasFoldedSubnodes"));
        const int childList = plist.value(topic.reference, "subnodes");
        QVector<int> children;
        if (childList != BinaryPlist::missing && !plist.array(childList, children))
            return fail(error, QStringLiteral("MindNode 子主题结构无效"));
        if (children.size() > Document::maxNodes - next.nodes.size() - pending.size())
            return fail(error, QStringLiteral("MindNode 节点数量超过限制"));
        for (auto it = children.crbegin(); it != children.crend(); ++it)
            pending.append(Topic{*it, id, topic.depth + 1});
    }
    warnings->append(QStringLiteral("MindNode 保留主题文字、层级、顺序和折叠状态。多张导图合入一图；附件、备注、关系线和样式未转换。"));
    if (markupKept) warnings->append(QStringLiteral("部分主题的富文本标记无法解析，已保留为原文字。"));
    *out = std::move(next);
    return true;
}

bool readDrawioModel(XmlGuard &reader, ExchangePage &page, QString *error) {
    auto &xml = reader.xml;
    const int modelDepth = reader.path.size();
    QString wrapperId, wrapperLabel;
    bool hasRoot = false;
    QVector<QPair<QString, QString>> containers;
    QSet<QString> linked;
    while (!xml.atEnd()) {
        if (!reader.advance(error)) return false;
        const int depth = reader.path.size();
        const auto attributes = xml.attributes();
        const auto name = xml.name();
        if (xml.isEndElement() && depth == modelDepth) {
            reader.endToken();
            for (const auto &link : containers)
                if (page.labels.contains(link.first) && !linked.contains(link.second)) page.links.append(link);
            return hasRoot || fail(error, QStringLiteral("draw.io 页面缺少图形根元素"));
        }
        if (xml.isStartElement() && depth == modelDepth + 1 && name == "root") hasRoot = true;
        if (xml.isStartElement() && depth == modelDepth + 2 && (name == "object" || name == "UserObject")) {
            wrapperId = attributes.value("id").toString();
            wrapperLabel = attributes.value("label").toString();
        }
        const bool cell = depth == modelDepth + 2 || (depth == modelDepth + 3 && !wrapperId.isEmpty());
        if (xml.isStartElement() && cell && hasRoot && name == "mxCell") {
            if (attributes.value("edge") == "1") {
                const QString from = attributes.value("source").toString(), to = attributes.value("target").toString();
                page.links.append(qMakePair(from, to));
                if (!from.isEmpty()) linked.insert(to);
            } else if (attributes.value("vertex") == "1") {
                const QString id = wrapperId.isEmpty() ? attributes.value("id").toString() : wrapperId;
                QString label = wrapperId.isEmpty() ? attributes.value("value").toString() : wrapperLabel;
                if (label.size() > Document::maxText) return fail(error, QStringLiteral("主题文字超过长度限制"));
                if (attributes.value("style").toString().split(';').contains("html=1")) {
                    QString plain;
                    if (readLegacyRichTopic("<html>" + label + "</html>", &plain)) label = plain;
                }
                if (!page.add(id, label, error)) return false;
                containers.append(qMakePair(attributes.value("parent").toString(), id));
            }
        }
        if (xml.isEndElement() && depth == modelDepth + 2) wrapperId.clear();
        reader.endToken();
    }
    return fail(error, QStringLiteral("draw.io 图形 XML 不完整"));
}

bool readCompressedDrawio(const QByteArray &encoded, ExchangePage &page, quint64 &total, QString *error) {
    const QByteArray compressed = QByteArray::fromBase64(encoded);
    if (compressed.isEmpty() || compressed.toBase64() != encoded)
        return fail(error, QStringLiteral("draw.io 压缩页面的 Base64 数据无效"));
    QByteArray decoded;
    const int limit = int(qMin<quint64>(Document::maxBytes, expandedLimit - total));
    if (limit == 0) return fail(error, QStringLiteral("draw.io 解压数据总量超过限制"));
    for (int capacity = qMin(65536, limit);; capacity = qMin(capacity * 2, limit)) {
        decoded.resize(capacity);
        tinfl_decompressor state;
        tinfl_init(&state);
        size_t read = compressed.size(), written = capacity;
        auto *buffer = reinterpret_cast<mz_uint8 *>(decoded.data());
        const auto status = tinfl_decompress(&state, reinterpret_cast<const mz_uint8 *>(compressed.constData()),
            &read, buffer, buffer, &written, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        if (status == TINFL_STATUS_DONE && read == size_t(compressed.size())) {
            decoded.resize(int(written));
            break;
        }
        if (status != TINFL_STATUS_HAS_MORE_OUTPUT || capacity == limit)
            return fail(error, QStringLiteral("draw.io 压缩内容损坏或解压数据超过限制"));
    }
    total += decoded.size();
    XmlGuard reader(QByteArray::fromPercentEncoding(decoded));
    bool seen = false;
    while (!reader.xml.atEnd()) {
        if (!reader.advance(error)) return false;
        if (reader.xml.isStartElement()) {
            if (seen || reader.xml.name() != "mxGraphModel")
                return fail(error, QStringLiteral("压缩页面不含有效的 draw.io 图形"));
            if (!readDrawioModel(reader, page, error)) return false;
            seen = true;
            continue;
        }
        reader.endToken();
    }
    return (seen && !reader.xml.hasError()) || fail(error, QStringLiteral("压缩页面 XML 损坏"));
}

bool parseDrawio(const QByteArray &bytes, const QString &title, Document *out,
                 QStringList *warnings, QString *error) {
    XmlGuard reader(bytes);
    auto &xml = reader.xml;
    QVector<ExchangePage> pages;
    ExchangePage page;
    QByteArray encoded;
    int nodes = 0;
    quint64 expanded = 0;
    bool hasModel = false;
    while (!xml.atEnd()) {
        if (!reader.advance(error)) return false;
        const int depth = reader.path.size();
        const auto name = xml.name();
        if (xml.isStartElement()) {
            if (depth == 1 && name != "mxfile" && name != "mxGraphModel")
                return fail(error, QStringLiteral("请选择 draw.io XML 文件"));
            if (depth == 2 && name == "diagram") {
                if (pages.size() >= entryLimit) return fail(error, QStringLiteral("draw.io 页面数量超过限制"));
                page = ExchangePage();
                page.title = xml.attributes().value("name").toString();
                if (page.title.isEmpty()) page.title = QStringLiteral("第 %1 页").arg(pages.size() + 1);
                hasModel = false;
                encoded.clear();
            }
            if (name == "mxGraphModel" && (depth == 1 || (depth == 3 && reader.path[1] == "diagram"))) {
                if (hasModel) return fail(error, QStringLiteral("draw.io 页面包含重复图形"));
                if (!readDrawioModel(reader, page, error)) return false;
                hasModel = true;
                if (depth == 1) {
                    page.title = title;
                    pages.append(std::move(page));
                }
                continue;
            }
        } else if (xml.isCharacters() && depth == 2 && reader.path[1] == "diagram" && !xml.isWhitespace()) {
            encoded += xml.text().toLatin1().simplified().replace(" ", "");
        } else if (xml.isEndElement() && depth == 2 && name == "diagram") {
            if (!hasModel && !readCompressedDrawio(encoded, page, expanded, error)) return false;
            nodes += page.order.size();
            if (nodes > Document::maxNodes) return fail(error, QStringLiteral("导图节点数量超过限制"));
            pages.append(std::move(page));
        }
        reader.endToken();
    }
    if (xml.hasError()) return fail(error, QStringLiteral("draw.io 文件 XML 损坏"));
    warnings->append(QStringLiteral("draw.io 的形状文字、页面和可映射的连接已转换。原位置、形状样式、边标签及图片未转换。"));
    return pagesToDocument(pages, title, out, warnings, error);
}

QString relationshipId(const QXmlStreamAttributes &attributes) {
    for (const auto &attribute : attributes)
        if (attribute.name() == "id" && attribute.namespaceUri().toString().endsWith("/relationships"))
            return attribute.value().toString();
    return {};
}

struct PagePart { QString relationship, title; };

bool readPageParts(const Archive &archive, const QString &index, bool visio,
                   QVector<QPair<QString, QString>> &parts, QString *error) {
    QByteArray data;
    const QFileInfo file(index);
    const QString folder = file.path();
    if (!archive.read(index, &data, error)) return false;
    XmlGuard manifest(data);
    QVector<PagePart> references;
    const QString root = visio ? "Pages" : "presentation";
    while (!manifest.xml.atEnd()) {
        if (!manifest.advance(error)) return false;
        const auto &xml = manifest.xml;
        if (xml.isStartElement()) {
            const int depth = manifest.path.size();
            if (depth == 1 && xml.name() != root) return fail(error, QStringLiteral("文件的页面索引无效"));
            if (visio && depth == 2 && xml.name() == "Page") {
                const auto attributes = xml.attributes();
                references.append(PagePart{QString(), attributes.value("Name").toString()});
            } else if (visio && depth == 3 && xml.name() == "Rel" && manifest.path[1] == "Page") {
                references.last().relationship = relationshipId(xml.attributes());
            } else if (!visio && depth == 3 && xml.name() == "sldId" && manifest.path[1] == "sldIdLst") {
                references.append(PagePart{relationshipId(xml.attributes()), QString()});
            }
            if (references.size() > entryLimit) return fail(error, QStringLiteral("文件的页面数量超过限制"));
        }
        manifest.endToken();
    }
    if (manifest.xml.hasError() || references.isEmpty()) return fail(error, QStringLiteral("找不到有效的页面索引"));
    if (!archive.read(folder + "/_rels/" + file.fileName() + ".rels", &data, error)) return false;
    XmlGuard reader(data);
    QHash<QString, QString> targets;
    const QString kind = visio ? "/page" : "/slide";
    while (!reader.xml.atEnd()) {
        if (!reader.advance(error)) return false;
        const auto &xml = reader.xml;
        const auto attributes = xml.attributes();
        if (xml.isStartElement() && reader.path.size() == 2 && xml.name() == "Relationship" &&
            attributes.value("Type").toString().endsWith(kind) && attributes.value("TargetMode") != "External") {
            const QString id = attributes.value("Id").toString();
            const QString target = QUrl::fromPercentEncoding(attributes.value("Target").toString().toUtf8());
            const QString path = QDir::cleanPath(target.startsWith('/') ? target.mid(1) : folder + '/' + target);
            if (id.isEmpty() || target.isEmpty() || targets.contains(id) || !archive.entries.contains(path))
                return fail(error, QStringLiteral("页面引用无效或指向包外内容"));
            targets.insert(id, path);
        }
        reader.endToken();
    }
    if (reader.xml.hasError()) return fail(error, QStringLiteral("页面关系文件损坏"));
    QSet<QString> used;
    for (const auto &reference : references) {
        const QString path = targets.value(reference.relationship);
        if (path.isEmpty() || used.contains(path)) return fail(error, QStringLiteral("页面引用缺失或重复"));
        parts.append(qMakePair(path, reference.title));
        used.insert(path);
    }
    return true;
}

bool readVisioPage(const QByteArray &data, ExchangePage &page, QString *error) {
    XmlGuard reader(data);
    auto &xml = reader.xml;
    struct Shape { QString id, text; int depth, textDepth; };
    QVector<Shape> active;
    QStringList connectorOrder;
    QHash<QString, QPair<QString, QString>> connectors;
    while (!xml.atEnd()) {
        if (!reader.advance(error)) return false;
        const int depth = reader.path.size();
        const auto name = xml.name();
        const auto attributes = xml.attributes();
        if (xml.isStartElement()) {
            if (depth == 1 && name != "PageContents") return fail(error, QStringLiteral("Visio 页面 XML 无效"));
            if (depth > 1 && name == "Shape" && reader.path[depth - 2] == "Shapes") {
                const QString id = attributes.value("ID").toString();
                if (!page.add(id, {}, error)) return false;
                active.append(Shape{id, {}, depth, 0});
            } else if (!active.isEmpty() && name == "Text" && depth == active.last().depth + 1) {
                active.last().textDepth = depth;
            } else if (name == "Connect" && depth == 3 && reader.path[1] == "Connects") {
                const auto cell = attributes.value("FromCell");
                if (cell == "BeginX" || cell == "EndX") {
                    const QString id = attributes.value("FromSheet").toString();
                    if (!connectors.contains(id)) connectorOrder.append(id);
                    auto &link = connectors[id];
                    const QString target = attributes.value("ToSheet").toString();
                    if (cell == "BeginX") link.first = target;
                    else link.second = target;
                }
            }
        } else if (xml.isCharacters() && !active.isEmpty() && active.last().textDepth) {
            auto &text = active.last().text;
            if (text.size() + xml.text().size() > Document::maxText)
                return fail(error, QStringLiteral("主题文字超过长度限制"));
            text += xml.text();
        } else if (xml.isEndElement() && !active.isEmpty()) {
            if (depth == active.last().textDepth) active.last().textDepth = 0;
            if (depth == active.last().depth) {
                const auto shape = active.takeLast();
                page.labels[shape.id] = shape.text;
            }
        }
        reader.endToken();
    }
    if (xml.hasError()) return fail(error, QStringLiteral("Visio 页面 XML 损坏"));
    for (const auto &id : connectorOrder) {
        page.labels.remove(id);
        page.links.append(connectors.value(id));
    }
    page.order.erase(std::remove_if(page.order.begin(), page.order.end(),
        [&](const QString &id) { return connectors.contains(id); }), page.order.end());
    return true;
}

bool readPptxSlide(const QByteArray &data, ExchangePage &page, QString *error) {
    XmlGuard reader(data);
    auto &xml = reader.xml;
    QString id, text, from, to;
    int shapeDepth = 0;
    bool connector = false, paragraph = false;
    while (!xml.atEnd()) {
        if (!reader.advance(error)) return false;
        const int depth = reader.path.size();
        const auto name = xml.name();
        const auto attributes = xml.attributes();
        if (xml.isStartElement()) {
            if (depth == 1 && name != "sld") return fail(error, QStringLiteral("PowerPoint 幻灯片 XML 无效"));
            if (depth == 2 && name == "cSld" && !attributes.value("name").isEmpty())
                page.title = attributes.value("name").toString();
            if (!shapeDepth && (name == "sp" || name == "cxnSp" || name == "graphicFrame") &&
                (reader.path.contains("spTree"))) {
                shapeDepth = depth;
                connector = name == "cxnSp";
                paragraph = false;
                id.clear();
                text.clear();
                from.clear();
                to.clear();
            } else if (shapeDepth && name == "cNvPr" && depth == shapeDepth + 2) {
                id = attributes.value("id").toString();
            } else if (connector && shapeDepth && name == "stCxn") {
                from = attributes.value("id").toString();
            } else if (connector && shapeDepth && name == "endCxn") {
                to = attributes.value("id").toString();
            } else if (shapeDepth && !connector && (name == "p" || name == "br") &&
                       xml.namespaceUri().toString().contains("drawingml")) {
                if (paragraph || name == "br") text += '\n';
                paragraph = true;
            }
        } else if (xml.isCharacters() && shapeDepth && !connector && reader.path.last() == "t" &&
                   reader.namespaces.last().contains("drawingml")) {
            text += xml.text();
        } else if (xml.isEndElement() && shapeDepth && depth == shapeDepth) {
            if (connector) page.links.append(qMakePair(from, to));
            else if (!page.add(id, text, error)) return false;
            shapeDepth = 0;
        }
        if (text.size() > Document::maxText) return fail(error, QStringLiteral("主题文字超过长度限制"));
        reader.endToken();
    }
    return !xml.hasError() || fail(error, QStringLiteral("PowerPoint 幻灯片 XML 损坏"));
}

bool parseOfficeDiagram(const Archive &archive, const QString &title, bool visio,
                        Document *out, QStringList *warnings, QString *error) {
    QVector<QPair<QString, QString>> parts;
    if (!readPageParts(archive, visio ? "visio/pages/pages.xml" : "ppt/presentation.xml", visio, parts, error)) return false;
    QVector<ExchangePage> pages;
    int nodes = 0;
    for (const auto &part : parts) {
        QByteArray data;
        if (!archive.read(part.first, &data, error)) return false;
        ExchangePage page;
        page.title = part.second.isEmpty() ? (visio ? QStringLiteral("第 %1 页") : QStringLiteral("幻灯片 %1")).arg(pages.size() + 1)
                                           : part.second;
        if (visio ? !readVisioPage(data, page, error) : !readPptxSlide(data, page, error)) return false;
        nodes += page.order.size();
        if (nodes > Document::maxNodes) return fail(error, QStringLiteral("导图节点数量超过限制"));
        pages.append(std::move(page));
    }
    warnings->append(visio ? QStringLiteral("Visio 转换保留页面、形状文字和可映射的连接。原位置、模具、样式和图片未转换。")
                           : QStringLiteral("PowerPoint 转换保留幻灯片顺序、可读取的文字和可映射的连接。版面、图片、图表、SmartArt、备注和动画未转换。"));
    return pagesToDocument(pages, title, out, warnings, error);
}

bool parseXmindXml(const QByteArray &data, const QString &title, Document *out,
                   QStringList *warnings, QString *error) {
    XmlGuard reader(data);
    auto &xml = reader.xml;
    const QString ns = QStringLiteral("urn:xmind:xmap:xmlns:content:2.0");
    QVector<ExchangePage> pages;
    ExchangePage page;
    QStringList active;
    int nodes = 0, titleDepth = 0;
    while (!xml.atEnd()) {
        if (!reader.advance(error)) return false;
        const int depth = reader.path.size();
        const auto name = xml.name();
        const bool core = xml.namespaceUri() == ns;
        if (xml.isStartElement()) {
            if (depth == 1 && (!core || name != "xmap-content"))
                return fail(error, QStringLiteral("Xmind XML 文件结构无效"));
            if (core && depth == 2 && name == "sheet") {
                if (pages.size() >= entryLimit) return fail(error, QStringLiteral("Xmind 工作表数量超过限制"));
                page = ExchangePage();
            }
            const bool nestedTopic = !active.isEmpty() && depth == active.size() * 3 + 3 &&
                reader.path[depth - 2] == "topics" && reader.path[depth - 3] == "children" &&
                reader.namespaces[depth - 2] == ns && reader.namespaces[depth - 3] == ns;
            if (core && name == "topic" && (nestedTopic || (depth == 3 && reader.path[1] == "sheet"))) {
                if (++nodes > Document::maxNodes || active.size() > Document::maxDepth)
                    return fail(error, QStringLiteral("Xmind 导图节点数量或层级超过限制"));
                const QString id = xml.attributes().value("id").toString();
                if (!page.add(id, {}, error)) return false;
                if (!active.isEmpty()) page.links.append(qMakePair(active.last(), id));
                active.append(id);
            } else if (core && name == "title" &&
                (active.isEmpty() ? depth == 3 && reader.path[1] == "sheet" : depth == active.size() * 3 + 1)) {
                titleDepth = depth;
            }
        } else if (xml.isCharacters() && titleDepth) {
            auto &text = active.isEmpty() ? page.title : page.labels[active.last()];
            if (text.size() + xml.text().size() > Document::maxText)
                return fail(error, QStringLiteral("主题文字超过长度限制"));
            text += xml.text();
        } else if (xml.isEndElement()) {
            if (depth == titleDepth) titleDepth = 0;
            if (!active.isEmpty() && depth == active.size() * 3) active.removeLast();
            if (core && depth == 2 && name == "sheet") {
                if (page.title.isEmpty()) page.title = QStringLiteral("工作表 %1").arg(pages.size() + 1);
                pages.append(std::move(page));
            }
        }
        reader.endToken();
    }
    if (xml.hasError() || nodes == 0) return fail(error, QStringLiteral("Xmind XML 文件损坏或没有主题"));
    return pagesToDocument(pages, title, out, warnings, error);
}

bool parseXmindJson(const QByteArray &data, const QString &title, Document *out, QString *error) {
    const auto sheets = QJsonDocument::fromJson(data).array();
    if (sheets.isEmpty() || sheets.size() > entryLimit) return fail(error, QStringLiteral("Xmind 工作表为空、损坏或数量超限"));
    Document next;
    if (sheets.size() > 1) appendTopic(next, {}, title);
    struct Topic { QJsonObject value; QString parent; int depth; };
    for (const auto &sheet : sheets) {
        const auto root = sheet.toObject().value("rootTopic").toObject();
        QVector<Topic> pending{Topic{root, next.root, sheets.size() > 1 ? 1 : 0}};
        while (!pending.isEmpty()) {
            const auto topic = pending.takeLast();
            const auto label = topic.value.value("title");
            if (topic.value.isEmpty() || (!label.isUndefined() && !label.isString()))
                return fail(error, QStringLiteral("Xmind 主题结构无效"));
            const QString text = label.toString();
            if (topic.depth > Document::maxDepth || next.nodes.size() >= Document::maxNodes || text.size() > Document::maxText)
                return fail(error, QStringLiteral("Xmind 节点数、层级或主题文字超过限制"));
            const QString id = appendTopic(next, topic.parent, text);
            const auto children = topic.value.value("children").toObject();
            for (const auto &key : {QString("detached"), QString("attached")}) {
                const auto branches = children.value(key).toArray();
                if (branches.size() > Document::maxNodes - pending.size() - next.nodes.size())
                    return fail(error, QStringLiteral("Xmind 节点数量超过限制"));
                for (int i = branches.size() - 1; i >= 0; --i)
                    pending.append(Topic{branches.at(i).toObject(), id, topic.depth + 1});
            }
        }
    }
    *out = std::move(next);
    return true;
}

bool parseXmind(const Archive &archive, const QString &title, Document *out,
                QStringList *warnings, QString *error) {
    QByteArray data;
    const bool json = archive.entries.contains("content.json");
    if (!archive.read(json ? "content.json" : "content.xml", &data, error)) return false;
    if (json ? !parseXmindJson(data, title, out, error) : !parseXmindXml(data, title, out, warnings, error))
        return false;
    warnings->append(QStringLiteral("Xmind 主题文字和层级已转换。多个工作表并入同一导图，浮动主题归入所属主题；关系线、样式、备注和附件未转换。"));
    return true;
}

bool parseFreeMind(const QByteArray &bytes, Document *out, QStringList *warnings, QString *error) {
    XmlGuard reader(bytes);
    auto &xml = reader.xml;
    Document next;
    struct Active { QString id; bool rich = false; };
    QVector<Active> active;
    bool legacyMarkupPreserved = false;
    while (!xml.atEnd()) {
        if (!reader.advance(error)) return false;
        const int depth = reader.path.size();
        const auto name = xml.name();
        const bool core = xml.namespaceUri().isEmpty();
        if (xml.isStartElement()) {
            if (depth == 1 && (!core || name != "map"))
                return fail(error, QStringLiteral("不是受支持的 FreeMind / Freeplane XML 导图"));
            if (core && name == "node" && depth == active.size() + 2) {
                if (active.isEmpty() && !next.root.isEmpty())
                    return fail(error, QStringLiteral("FreeMind / Freeplane 文件含有多个中心主题"));
                if (next.nodes.size() >= Document::maxNodes || active.size() > Document::maxDepth)
                    return fail(error, QStringLiteral("导图节点数量或层级超过限制"));
                if (xml.attributes().hasAttribute("ENCRYPTED_CONTENT"))
                    return fail(error, QStringLiteral("FreeMind / Freeplane 导图含有加密分支，请先在原软件解锁并导出副本。"));
                QString text = xml.attributes().value("TEXT").toString();
                if (text.size() > Document::maxText) return fail(error, QStringLiteral("主题文字超过长度限制"));
                if (text.trimmed().startsWith(QStringLiteral("<html>"), Qt::CaseInsensitive)) {
                    QString plain;
                    if (readLegacyRichTopic(text, &plain)) text = plain;
                    else legacyMarkupPreserved = true;
                }
                Active item;
                item.id = appendTopic(next, active.isEmpty() ? QString() : active.last().id, text);
                next.nodes[item.id].collapsed = xml.attributes().value("FOLDED") == "true";
                active.append(item);
            } else if (core && name == "richcontent" && !active.isEmpty() && depth == active.size() + 2
                       && xml.attributes().value("TYPE") == "NODE") {
                if (active.last().rich) return fail(error, QStringLiteral("FreeMind / Freeplane 主题含有重复的富文本内容"));
                active.last().rich = true;
                QString richText;
                if (!readRichTopic(reader, &richText, error)) return false;
                if (!richText.isEmpty()) next.nodes[active.last().id].text = richText;
                continue;
            }
        } else if (xml.isEndElement() && !active.isEmpty() && depth == active.size() + 1) {
            active.removeLast();
        }
        reader.endToken();
    }
    if (xml.hasError() || next.root.isEmpty())
        return fail(error, QStringLiteral("FreeMind / Freeplane 文件损坏或没有中心主题"));
    warnings->append(QStringLiteral("FreeMind / Freeplane 富文本主题按纯文字导入。备注、详情、图片、附件、链接、图标、关系线及其他样式未转换。"));
    if (legacyMarkupPreserved)
        warnings->append(QStringLiteral("部分旧版主题的 HTML 无法转换，已保留其原始文字。"));
    *out = std::move(next);
    return true;
}

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
                if (active.isEmpty() && !next.root.isEmpty())
                    return fail(error, QStringLiteral("暂不支持多个中心主题的 MindManager 文件"));
                Active item;
                item.id = appendTopic(next, active.isEmpty() ? QString() : active.last().id, QString());
                item.depth = depth;
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

bool parseBinaryEmmx(const Archive &archive, const QStringList &entries, const QString &title,
                     Document *out, QStringList *warnings, QString *error) {
    QByteArray data;
    if (!archive.entries.contains("mm.bkiwi"))
        return fail(error, QStringLiteral("MindMaster 二进制文件缺少页面格式描述"));
    if (!archive.read("mm.bkiwi", &data, error)) return false;
    const QByteArray schema = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
    if (schema != "c8393d391947ff5775201d6f60aab9677ecd0803de1274b0d1df43ff2ebbb984" &&
        schema != "424d86a021fa772b0a3f973f827bd76584828d37476876aa596d5e4dd753bc69")
        return fail(error, QStringLiteral("此 MindMaster 二进制文件使用尚未验证的页面版本"));
    if (!archive.read("document.xml", &data, error)) return false;
    XmlGuard manifest(data);
    QStringList order;
    while (!manifest.xml.atEnd()) {
        if (!manifest.advance(error)) return false;
        const auto &xml = manifest.xml;
        if (xml.isStartElement()) {
            if (manifest.path.size() == 1 && xml.name() != "Document")
                return fail(error, QStringLiteral("MindMaster 文件的页面索引无效"));
            if (manifest.path.size() == 3 && manifest.path[1] == "Properties" && xml.name() == "Pages")
                order = xml.attributes().value("V").toString().split(';', QString::SkipEmptyParts);
        }
        manifest.endToken();
    }
    if (manifest.xml.hasError() || order.size() != entries.size())
        return fail(error, QStringLiteral("MindMaster 页面索引缺失或与文件内容不一致"));
    Document next;
    if (order.size() > 1) appendTopic(next, {}, title);
    QSet<QString> visited;
    for (const auto &name : order) {
        const QString path = "mmpage/" + name + ".bin";
        if (visited.contains(path) || !entries.contains(path))
            return fail(error, QStringLiteral("MindMaster 页面索引含重复或缺失页面"));
        visited.insert(path);
        Document page;
        if (!archive.read(path, &data, error) || !readBinaryEmmxPage(data, &page, error)) return false;
        if (order.size() == 1) {
            next = std::move(page);
            break;
        }
        if (page.nodes.size() > Document::maxNodes - next.nodes.size())
            return fail(error, QStringLiteral("MindMaster 导图节点数量超过限制"));
        const auto prefix = QString::number(visited.size()) + '-';
        next.nodes[next.root].children.append(prefix + page.root);
        for (auto node : qAsConst(page.nodes)) {
            node.id.prepend(prefix);
            for (auto &child : node.children) child.prepend(prefix);
            next.nodes.insert(node.id, node);
        }
    }
    warnings->append(QStringLiteral("MindMaster 二进制导入保留中心、主分支与子主题的文字、层级和顺序。标注、浮动对象、图片、附件及样式未转换。"));
    *out = std::move(next);
    return true;
}
}

bool importMarkdownText(const QString &text, Document *out, QString *error) {
    if (!out || text.size() > Document::maxBytes || text.toUtf8().size() > Document::maxBytes)
        return fail(error, QStringLiteral("Markdown 超过文件大小限制"));
    for (int i = 0; i < text.size(); ++i) {
        if (text[i].isHighSurrogate()) {
            if (++i >= text.size() || !text[i].isLowSurrogate()) return fail(error, QStringLiteral("Markdown 包含无效字符"));
        } else if (text[i].isLowSurrogate()) return fail(error, QStringLiteral("Markdown 包含无效字符"));
    }
    QString normalized = text; normalized.replace("\r\n", "\n"); normalized.replace('\r', '\n');
    Document next, checked;
    if (!parseMarkdown(normalized.split('\n'), QStringLiteral("粘贴内容"), &next, error) ||
        !checked.parse(next.serialize(), error)) return false;
    *out = std::move(checked); return true;
}

bool importMindMap(const QString &path, Document *out, QStringList *warnings, QString *error) {
    if (!out) return fail(error, QStringLiteral("没有导入目标"));
    const QFileInfo source(path);
    const bool bundle = source.isDir() && source.suffix().compare("mindnode", Qt::CaseInsensitive) == 0;
    const bool bundleContents = source.fileName() == "contents.xml" &&
        source.dir().dirName().endsWith(".mindnode", Qt::CaseInsensitive);
    const QString extension = bundleContents ? QStringLiteral("mindnode") : source.suffix().toLower();
    const QStringList supported{"mmap", "emmx", "mind", "mindnode", "mom", "pmom", "opml", "mm", "xmind", "drawio", "xml", "vsdx", "pptx",
                                "md", "markdown", "txt", "mmd", "mermaid", "canvas"};
    if (!supported.contains(extension))
        return fail(error, QStringLiteral("无法识别此文件类型。请使用导入窗口列出的格式。"));
    if (!bundle && !source.isFile()) return fail(error, QStringLiteral("文件不存在或不是普通文件"));
    QFile file(bundle ? QDir(path).filePath("contents.xml") : path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, file.errorString());
    if (file.size() > Document::maxBytes) return fail(error, QStringLiteral("文件超过 50 MiB"));
    QByteArray bytes = file.read(Document::maxBytes + 1);
    if (file.error() != QFileDevice::NoError) return fail(error, file.errorString());
    if (bytes.size() > Document::maxBytes) return fail(error, QStringLiteral("文件超过 50 MiB"));
    Document next; QStringList messages;
    if (extension == "opml") {
        if (!parseOpml(bytes, source.completeBaseName(), &next, &messages, error)) return false;
    } else if (extension == "mm") {
        if (!parseFreeMind(bytes, &next, &messages, error)) return false;
    } else if (extension == "mindnode" && !bytes.startsWith("PK")) {
        const QString title = bundleContents ? QFileInfo(source.dir().dirName()).completeBaseName() : source.completeBaseName();
        if (!parseMindNode(bytes, title, &next, &messages, error)) return false;
    } else if (extension == "drawio" || extension == "xml") {
        if (!parseDrawio(bytes, source.completeBaseName(), &next, &messages, error)) return false;
    } else if (extension == "canvas") {
        if (!parseCanvas(bytes, source.completeBaseName(), &next, &messages, error)) return false;
    } else if (extension == "txt" || extension == "md" || extension == "markdown" || extension == "mmd" || extension == "mermaid") {
        QString text;
        if (!decodeOutline(bytes, text, error)) return false;
        const QString title = source.completeBaseName();
        if (extension == "txt") {
            if (!BranchClipboard::parseOutline(text, title, &next, error)) return false;
            useSingleTopLevelTopic(next);
            messages.append(QStringLiteral("纯文本按行转换，缩进决定父子层级。"));
        } else if (extension == "md" || extension == "markdown") {
            if (!parseMarkdown(text.split('\n'), title, &next, error)) return false;
            messages.append(QStringLiteral("Markdown 按标题、列表或内嵌 Mermaid 思维导图转换；代码块和排版样式未转换。"));
        } else {
            if (!parseMermaid(text.split('\n'), title, &next, error)) return false;
            messages.append(QStringLiteral("Mermaid 思维导图保留节点文字和缩进层级，形状、图标和样式未转换。"));
        }
    } else {
        Archive archive;
        archive.bytes = std::move(bytes);
        if (!archive.parse(error)) return false;
        QByteArray xml;
        if (extension == "mmap") {
            if (!archive.read(QStringLiteral("Document.xml"), &xml, error) || !parseMmap(xml, &next, &messages, error)) return false;
        } else if (extension == "mind") {
            if (!archive.read("map.json", &xml, error) || !parseMindMeister(xml, &next, &messages, error)) return false;
        } else if (extension == "mindnode") {
            if (archive.entries.contains("Contents.data")) {
                if (!archive.read("Metadata.json", &xml, error)) return false;
                const auto metadata = QJsonDocument::fromJson(xml).object();
                if (metadata.value("fileFormatVersion").toInt() != 1 ||
                    metadata.value("minFileFormatVersionForReading").toInt() != 1)
                    return fail(error, QStringLiteral("此 MindNode Next 文件使用尚未验证的格式版本"));
                if (!archive.read("Contents.data", &xml, error) ||
                    !readMindNodeNext(xml, source.completeBaseName(), &next, error)) return false;
                messages.append(QStringLiteral("MindNode Next 保留主题文字、层级和顺序。样式、备注、图片和关系线未转换。"));
            } else if (!archive.read("contents.xml", &xml, error) ||
                       !parseMindNode(xml, source.completeBaseName(), &next, &messages, error)) return false;
        } else if (extension == "mom" || extension == "pmom") {
            if (!archive.read("nodes.json", &xml, error) ||
                !parseMindomo(xml, source.completeBaseName(), &next, &messages, error)) return false;
        } else if (extension == "xmind") {
            if (!parseXmind(archive, source.completeBaseName(), &next, &messages, error)) return false;
        } else if (extension == "vsdx" || extension == "pptx") {
            if (!parseOfficeDiagram(archive, source.completeBaseName(), extension == "vsdx", &next, &messages, error)) return false;
        } else {
            QStringList pages, binaryPages;
            for (const QString &name : archive.entries.keys()) {
                if (name.startsWith("mmpage/") && name.endsWith(".bin"))
                    binaryPages.append(name);
                if (name.startsWith("page/") && name.endsWith(".xml")) pages.append(name);
            }
            if (!binaryPages.isEmpty()) {
                if (!pages.isEmpty()) return fail(error, QStringLiteral("MindMaster 文件混用了不受支持的页面类型"));
                if (!parseBinaryEmmx(archive, binaryPages, source.completeBaseName(), &next, &messages, error)) return false;
            } else {
                if (!archive.entries.contains("document.xml") || pages.size() != 1)
                    return fail(error, QStringLiteral("旧版 MindMaster 文件须含一个 XML 页面，多页或缺页的文件暂不支持"));
                if (!archive.read(pages.first(), &xml, error) || !parseEmmx(xml, &next, &messages, error)) return false;
            }
        }
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
