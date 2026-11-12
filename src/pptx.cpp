#include "pptx.h"
#include "layout.h"
#include "package_writer.h"

#include <QtMath>
#include <functional>

namespace {
const qint64 slideWidth = 12192000;
const qint64 slideHeight = 6858000;
const int maxSlides = 500;
const int maxPackageBytes = 64 * 1024 * 1024;
const int pageNodes = 12;
const int pageDepth = 2;
const char *drawing = "http://schemas.openxmlformats.org/drawingml/2006/main";
const char *presentation = "http://schemas.openxmlformats.org/presentationml/2006/main";
const char *officeRels = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";

bool fail(QString *error, const QString &message) {
    if (error) *error = message;
    return false;
}

QString safePrefix(const QString &text, int length) {
    if (text.size() <= length) return text;
    if (length > 0 && text.at(length - 1).isHighSurrogate()) --length;
    return text.left(length);
}

bool xmlText(const QString &text) {
    for (const QChar ch : text) {
        const ushort u = ch.unicode();
        if ((u < 32 && u != 9 && u != 10 && u != 13) || u == 0xfffe || u == 0xffff) return false;
    }
    return true;
}

QByteArray xml(const std::function<void(QXmlStreamWriter &)> &body) {
    QByteArray bytes;
    QXmlStreamWriter out(&bytes);
    out.writeStartDocument();
    body(out);
    out.writeEndDocument();
    return bytes;
}

void empty(QXmlStreamWriter &out, const QString &name,
           const QList<QPair<QString, QString>> &attributes = {}) {
    out.writeEmptyElement(name);
    for (const auto &attr : attributes) out.writeAttribute(attr.first, attr.second);
}

void solid(QXmlStreamWriter &out, const QString &color) {
    out.writeStartElement("a:solidFill");
    empty(out, "a:srgbClr", {{"val", color}});
    out.writeEndElement();
}

void groupProperties(QXmlStreamWriter &out) {
    out.writeStartElement("p:nvGrpSpPr");
    empty(out, "p:cNvPr", {{"id", "1"}, {"name", ""}});
    empty(out, "p:cNvGrpSpPr");
    empty(out, "p:nvPr");
    out.writeEndElement();
    out.writeStartElement("p:grpSpPr");
    out.writeStartElement("a:xfrm");
    empty(out, "a:off", {{"x", "0"}, {"y", "0"}});
    empty(out, "a:ext", {{"cx", "0"}, {"cy", "0"}});
    empty(out, "a:chOff", {{"x", "0"}, {"y", "0"}});
    empty(out, "a:chExt", {{"cx", "0"}, {"cy", "0"}});
    out.writeEndElement();
    out.writeEndElement();
}

void shape(QXmlStreamWriter &out, int id, const QString &name, const QString &text,
           const QRectF &rect, int points100, const QString &fill, const QString &color,
           bool rounded, bool bold = false, const QString &description = {}, qint64 leftInset = 63500,
           const TopicStyle *style = nullptr) {
    out.writeStartElement("p:sp");
    out.writeStartElement("p:nvSpPr");
    out.writeEmptyElement("p:cNvPr");
    out.writeAttribute("id", QString::number(id));
    out.writeAttribute("name", name);
    if (!description.isEmpty()) out.writeAttribute("descr", description);
    empty(out, "p:cNvSpPr");
    empty(out, "p:nvPr");
    out.writeEndElement();
    out.writeStartElement("p:spPr");
    out.writeStartElement("a:xfrm");
    empty(out, "a:off", {{"x", QString::number(qRound64(rect.x()))}, {"y", QString::number(qRound64(rect.y()))}});
    empty(out, "a:ext", {{"cx", QString::number(qMax<qint64>(1, qRound64(rect.width())))},
                         {"cy", QString::number(qMax<qint64>(1, qRound64(rect.height())))}});
    out.writeEndElement();
    out.writeStartElement("a:prstGeom");
    out.writeAttribute("prst", rounded ? "roundRect" : "rect");
    empty(out, "a:avLst");
    out.writeEndElement();
    if (fill.isEmpty()) empty(out, "a:noFill"); else solid(out, fill);
    out.writeStartElement("a:ln");
    out.writeAttribute("w", "12700");
    if (rounded) solid(out, "92AFA3"); else empty(out, "a:noFill");
    out.writeEndElement();
    out.writeEndElement();
    out.writeStartElement("p:txBody");
    out.writeStartElement("a:bodyPr");
    out.writeAttribute("wrap", "square");
    out.writeAttribute("anchor", rounded ? "ctr" : "t");
    for (const QString &key : {QString("lIns"), QString("rIns"), QString("tIns"), QString("bIns")})
        out.writeAttribute(key, rounded ? QString::number(key == "lIns" ? leftInset : 63500) : QString("0"));
    empty(out, "a:normAutofit");
    out.writeEndElement();
    empty(out, "a:lstStyle");
    QString normalized = text;
    normalized.replace("\r\n", "\n");
    normalized.replace('\r', '\n');
    const QString alignment = style && style->alignment == TextAlignment::Left ? "l" :
        style && style->alignment == TextAlignment::Right ? "r" : rounded ? "ctr" : "l";
    const QString textColor = style && !style->color.isEmpty() ? style->color.mid(1).toUpper() : color;
    const QString family = style ? style->family : QString();
    const bool textBold = style && style->weight != -1 ? style->weight == 1 : bold;
    for (const QString &line : normalized.split('\n')) {
        out.writeStartElement("a:p");
        empty(out, "a:pPr", {{"algn", alignment}});
        QList<QPair<QString, bool>> runs;
        for (int offset = 0; offset < line.size();) {
            const int count = line.at(offset).isHighSurrogate() && offset + 1 < line.size() ? 2 : 1;
            const uint code = count == 2 ? QChar::surrogateToUcs4(line.at(offset), line.at(offset + 1)) : line.at(offset).unicode();
            const bool emoji = (code >= 0x1f000 && code <= 0x1faff) || (code >= 0x2600 && code <= 0x27bf) || code == 0xfe0f || code == 0x200d;
            if (runs.isEmpty() || runs.last().second != emoji) runs.append(qMakePair(QString(), emoji));
            runs.last().first += line.mid(offset, count);
            offset += count;
        }
        for (const auto &run : runs) {
            out.writeStartElement("a:r");
            out.writeStartElement("a:rPr");
            out.writeAttribute("lang", "zh-CN");
            out.writeAttribute("sz", QString::number(qMax(100, points100)));
            out.writeAttribute("b", textBold ? "1" : "0");
            if (style && style->italic) out.writeAttribute("i", "1");
            if (style && style->underline) out.writeAttribute("u", "sng");
            if (style && style->strikeOut) out.writeAttribute("strike", "sngStrike");
            solid(out, textColor);
            const QString latin = run.second ? "Segoe UI Emoji" : family.isEmpty() ? "Arial" : family;
            const QString eastAsian = run.second ? "Segoe UI Emoji" : family.isEmpty() ? "Source Han Sans CN" : family;
            empty(out, "a:latin", {{"typeface", latin}});
            empty(out, "a:ea", {{"typeface", eastAsian}});
            empty(out, "a:cs", {{"typeface", latin}});
            out.writeEndElement();
            out.writeStartElement("a:t");
            out.writeAttribute("xml:space", "preserve");
            out.writeCharacters(run.first);
            out.writeEndElement();
            out.writeEndElement();
        }
        empty(out, "a:endParaRPr", {{"lang", "zh-CN"}, {"sz", QString::number(qMax(100, points100))}});
        out.writeEndElement();
    }
    out.writeEndElement();
    out.writeEndElement();
}

void picture(QXmlStreamWriter &out, int id, int relationship, const QRectF &rect) {
    out.writeStartElement("p:pic");
    out.writeStartElement("p:nvPicPr");
    empty(out, "p:cNvPr", {{"id", QString::number(id)}, {"name", QStringLiteral("主题符号 %1").arg(id)}});
    out.writeStartElement("p:cNvPicPr");
    empty(out, "a:picLocks", {{"noChangeAspect", "1"}});
    out.writeEndElement();
    empty(out, "p:nvPr");
    out.writeEndElement();
    out.writeStartElement("p:blipFill");
    empty(out, "a:blip", {{"r:embed", QStringLiteral("rId%1").arg(relationship)}});
    out.writeStartElement("a:stretch");
    empty(out, "a:fillRect");
    out.writeEndElement();
    out.writeEndElement();
    out.writeStartElement("p:spPr");
    out.writeStartElement("a:xfrm");
    empty(out, "a:off", {{"x", QString::number(qRound64(rect.x()))}, {"y", QString::number(qRound64(rect.y()))}});
    empty(out, "a:ext", {{"cx", QString::number(qRound64(rect.width()))}, {"cy", QString::number(qRound64(rect.height()))}});
    out.writeEndElement();
    out.writeStartElement("a:prstGeom");
    out.writeAttribute("prst", "rect");
    empty(out, "a:avLst");
    out.writeEndElement();
    out.writeEndElement();
    out.writeEndElement();
}

void connector(QXmlStreamWriter &out, int id, int parent, int child, QPointF a, QPointF b,
               int startSite, int endSite, ConnectorStyle style, bool horizontal) {
    out.writeStartElement("p:cxnSp");
    out.writeStartElement("p:nvCxnSpPr");
    empty(out, "p:cNvPr", {{"id", QString::number(id)}, {"name", QStringLiteral("连接线 %1").arg(id)}});
    out.writeStartElement("p:cNvCxnSpPr");
    empty(out, "a:stCxn", {{"id", QString::number(parent)}, {"idx", QString::number(startSite)}});
    empty(out, "a:endCxn", {{"id", QString::number(child)}, {"idx", QString::number(endSite)}});
    out.writeEndElement();
    empty(out, "p:nvPr");
    out.writeEndElement();
    out.writeStartElement("p:spPr");
    out.writeStartElement("a:xfrm");
    const bool rotate = !horizontal && style != ConnectorStyle::Straight;
    const QPointF delta = rotate ? QPointF(b.y() - a.y(), a.x() - b.x()) : b - a;
    const QPointF extent(qAbs(delta.x()), qAbs(delta.y()));
    const QPointF origin = (a + b - extent) / 2;
    if (rotate) out.writeAttribute("rot", "5400000");
    if (delta.x() < 0) out.writeAttribute("flipH", "1");
    if (delta.y() < 0) out.writeAttribute("flipV", "1");
    empty(out, "a:off", {{"x", QString::number(qRound64(origin.x()))},
                         {"y", QString::number(qRound64(origin.y()))}});
    empty(out, "a:ext", {{"cx", QString::number(qMax<qint64>(1, qRound64(extent.x())))},
                         {"cy", QString::number(qMax<qint64>(1, qRound64(extent.y())))}});
    out.writeEndElement();
    out.writeStartElement("a:prstGeom");
    out.writeAttribute("prst", style == ConnectorStyle::Orthogonal ? "bentConnector3" :
                               style == ConnectorStyle::Curved ? "curvedConnector3" : "line");
    empty(out, "a:avLst");
    out.writeEndElement();
    out.writeStartElement("a:ln");
    out.writeAttribute("w", "19050");
    solid(out, "92AFA3");
    out.writeEndElement();
    out.writeEndElement();
    out.writeEndElement();
}

struct Page {
    Document tree;
    QString title;
    QString text;
    QString sourceId;
};

bool longTopic(const Node &node) {
    return node.text.size() > 240 || node.text.count('\n') > 6 || node.text.count('\r') > 6;
}

int topicCost(const Node &node) {
    if (longTopic(node)) return 5;
    int cost = 0;
    for (const QString &line : node.text.split('\n')) cost += qMax(1, qCeil(line.size() / 14.0));
    return qBound(1, cost, 18);
}

void addNode(Page &page, const Document &source, const QString &id) {
    Node node = source.nodes.value(id);
    node.children.clear();
    node.collapsed = false;
    page.tree.nodes.insert(id, node);
}

bool plan(const Document &source, QVector<Page> *pages, QMap<QString, int> *textPages, QString *error) {
    const auto append = [&](const Page &page) {
        if (pages->size() >= maxSlides) return false;
        pages->append(page);
        return true;
    };
    const QString title = safePrefix(source.nodes.value(source.root).text.simplified(), 64);
    int allCost = 0;
    for (const Node &node : source.nodes) allCost += topicCost(node);
    int maximumDepth = 0;
    QVector<QPair<QString, int>> depthQueue{{source.root, 0}};
    while (!depthQueue.isEmpty()) {
        const auto item = depthQueue.takeLast();
        maximumDepth = qMax(maximumDepth, item.second);
        for (const QString &child : source.nodes.value(item.first).children) depthQueue.append(qMakePair(child, item.second + 1));
    }
    if (source.nodes.size() <= pageNodes && allCost <= 24 && maximumDepth <= pageDepth) {
        Page page;
        page.tree = source;
        for (auto it = page.tree.nodes.begin(); it != page.tree.nodes.end(); ++it) it->collapsed = false;
        page.title = title;
        append(page);
    } else {
        const QStringList branches = source.nodes.value(source.root).children;
        for (int first = 0; first < branches.size();) {
            Page page;
            page.title = title + QStringLiteral(" · 总览");
            page.tree.root = source.root;
            page.tree.mindMap = source.mindMap;
            page.tree.branchDirection = source.branchDirection;
            page.tree.connectorStyle = source.connectorStyle;
            addNode(page, source, source.root);
            int cost = topicCost(source.nodes.value(source.root));
            while (first < branches.size() && page.tree.nodes.size() < pageNodes) {
                const QString child = branches.at(first);
                const int nextCost = topicCost(source.nodes.value(child));
                if (page.tree.nodes.size() > 1 && cost + nextCost > 24) break;
                addNode(page, source, child);
                page.tree.nodes[source.root].children.append(child);
                cost += nextCost;
                ++first;
            }
            if (!append(page)) return fail(error, QStringLiteral("导图超过 500 页导出上限，请分拆导图"));
        }
        // Every source edge is included once in these bounded branch pages.
        // Boundary nodes recur as the root of a continuation page.
        QVector<QPair<QString, int>> pending;
        for (const QString &branch : branches)
            if (!source.nodes.value(branch).children.isEmpty()) pending.append(qMakePair(branch, 0));
        for (int task = 0; task < pending.size(); ++task) {
            const auto initial = pending.at(task);
            Page page;
            page.title = safePrefix(source.nodes.value(initial.first).text.simplified(), 64) + QStringLiteral(" · 分支");
            page.tree.root = initial.first;
            page.tree.mindMap = source.mindMap;
            page.tree.branchDirection = source.branchDirection;
            page.tree.connectorStyle = source.connectorStyle;
            addNode(page, source, initial.first);
            int cost = topicCost(source.nodes.value(initial.first));
            QMap<QString, int> depths{{initial.first, 0}};
            QVector<QPair<QString, int>> expand{initial};
            for (int index = 0; index < expand.size(); ++index) {
                const auto item = expand.at(index);
                if (depths.value(item.first) >= pageDepth) {
                    pending.append(item);
                    continue;
                }
                const QStringList children = source.nodes.value(item.first).children;
                int next = item.second;
                while (next < children.size() && page.tree.nodes.size() < pageNodes) {
                    const QString child = children.at(next);
                    const int nextCost = topicCost(source.nodes.value(child));
                    if (page.tree.nodes.size() > 1 && cost + nextCost > 24) break;
                    ++next;
                    cost += nextCost;
                    addNode(page, source, child);
                    depths.insert(child, depths.value(item.first) + 1);
                    page.tree.nodes[item.first].children.append(child);
                    if (!source.nodes.value(child).children.isEmpty()) expand.append(qMakePair(child, 0));
                }
                if (next < children.size()) pending.append(qMakePair(item.first, next));
            }
            if (!append(page)) return fail(error, QStringLiteral("导图超过 500 页导出上限，请分拆导图"));
        }
    }
    // Preserve long topics in readable, editable continuation pages. Explicit
    // pointers in diagrams distinguish previews from the complete topic text.
    for (const Node &node : source.nodes) {
        if (!longTopic(node)) continue;
        textPages->insert(node.id, pages->size() + 1);
        int offset = 0;
        int part = 0;
        while (offset < node.text.size()) {
            int length = qMin(360, node.text.size() - offset);
            int newlines = 0;
            for (int i = 0; i < length; ++i) {
                if (node.text.at(offset + i) == '\n' || node.text.at(offset + i) == '\r') ++newlines;
                if (newlines > 10) { length = i; break; }
            }
            if (offset + length < node.text.size() && length > 0 && node.text.at(offset + length - 1).isHighSurrogate()) --length;
            if (length == 0) length = 1;
            Page page;
            page.sourceId = node.id;
            page.title = QStringLiteral("主题全文 · %1").arg(++part);
            page.text = node.text.mid(offset, length);
            if (!append(page)) return fail(error, QStringLiteral("主题全文超过 500 页导出上限，请分拆导图"));
            offset += length;
        }
    }
    return true;
}

QString displayText(const Node &node, const QMap<QString, int> &textPages) {
    if (!textPages.contains(node.id)) return node.text;
    return safePrefix(node.text.simplified(), 100) + QStringLiteral("…\n全文见第 %1 页").arg(textPages.value(node.id));
}

QSizeF topicSize(const QString &text) {
    // Conservative device-independent estimate, so exporting needs no GUI or
    // installed font service. PowerPoint's native autofit handles font changes.
    qreal longest = 0;
    int lines = 0;
    for (const QString &line : text.split('\n')) {
        qreal width = 0;
        for (const QChar ch : line) width += ch.unicode() > 255 ? 16 : 9;
        longest = qMax(longest, width);
        lines += qMax(1, qCeil(width / 224));
    }
    return QSizeF(qBound(104.0, longest + 28, 252.0), qMax(44, lines * 22 + 18));
}

QMap<int, int> slideImages(const Page &page, const QMap<QString, int> &nodeImages) {
    QMap<int, int> relationships;
    for (const Node &node : page.tree.nodes) {
        if (!nodeImages.contains(node.id)) continue;
        const int asset = nodeImages.value(node.id);
        if (!relationships.contains(asset)) relationships.insert(asset, relationships.size() + 2);
    }
    return relationships;
}

QByteArray slideXml(const Page &page, int number, int total, const QMap<QString, int> &textPages,
                    const QMap<QString, int> &nodeImages) {
    return xml([&](QXmlStreamWriter &out) {
        out.writeStartElement("p:sld");
        out.writeNamespace(presentation, "p");
        out.writeNamespace(drawing, "a");
        out.writeNamespace(officeRels, "r");
        out.writeStartElement("p:cSld");
        out.writeStartElement("p:bg");
        out.writeStartElement("p:bgPr");
        solid(out, "F7F7F1");
        empty(out, "a:effectLst");
        out.writeEndElement();
        out.writeEndElement();
        out.writeStartElement("p:spTree");
        groupProperties(out);
        shape(out, 2, "Title", page.title, QRectF(457200, 274320, 11277600, 548640), 2400, {}, "203D36", false, true);
        shape(out, 3, "Page", QStringLiteral("枝理 · %1 / %2").arg(number).arg(total),
              QRectF(457200, 6400800, 11277600, 228600), 900, {}, "58746A", false);
        if (!page.sourceId.isEmpty()) {
            shape(out, 4, "Full topic text", page.text, QRectF(609600, 1066800, 10972800, 5029200),
                  1800, {}, "203D36", false);
        } else {
            QMap<QString, QSizeF> sizes;
            for (const Node &node : page.tree.nodes) {
                QSizeF size = topicSize(displayText(node, textPages));
                if (node.style.points > 12) size *= node.style.points / 12.0;
                if (nodeImages.contains(node.id)) size.rwidth() += 30;
                sizes.insert(node.id, size);
            }
            const QRectF area(457200, 1005840, 11277600, 5143500);
            const auto extent = [&](const QMap<QString, QPointF> &points) {
                QRectF bounds;
                bool first = true;
                for (auto it = points.cbegin(); it != points.cend(); ++it) {
                    const QSizeF size = sizes.value(it.key());
                    const QRectF rect(it.value() - QPointF(size.width() / 2, size.height() / 2), size);
                    bounds = first ? rect : bounds.united(rect);
                    first = false;
                }
                return bounds;
            };
            const auto fitScale = [&](const QRectF &bounds) {
                return qMin(19050.0, qMin(area.width() / qMax(1.0, bounds.width()), area.height() / qMax(1.0, bounds.height())));
            };
            Document rendered = page.tree;
            auto positions = layoutNodes(rendered, sizes);
            QRectF bounds = extent(positions);
            qreal scale = fitScale(bounds);
            Document alternative = rendered;
            alternative.mindMap = !alternative.mindMap;
            const auto alternatePositions = layoutNodes(alternative, sizes);
            const QRectF alternateBounds = extent(alternatePositions);
            if (rendered.branchDirection == BranchDirection::Both && fitScale(alternateBounds) > scale * 1.1) {
                rendered = alternative;
                positions = alternatePositions;
                bounds = alternateBounds;
                scale = fitScale(bounds);
            }
            const QPointF offset = area.center() - bounds.center() * scale;
            QMap<QString, QRectF> boxes;
            QMap<QString, int> ids;
            int id = 4;
            for (auto it = positions.cbegin(); it != positions.cend(); ++it) {
                const QSizeF size = sizes.value(it.key()) * scale;
                boxes.insert(it.key(), QRectF(it.value() * scale + offset - QPointF(size.width() / 2, size.height() / 2), size));
                ids.insert(it.key(), id++);
            }
            const ConnectorStyle edgeStyle = rendered.connectorStyle == ConnectorStyle::Automatic
                ? (rendered.mindMap ? ConnectorStyle::Curved : ConnectorStyle::Orthogonal) : rendered.connectorStyle;
            for (const Node &node : page.tree.nodes) {
                for (const QString &child : node.children) {
                    const QRectF parentBox = boxes.value(node.id), childBox = boxes.value(child);
                    const bool right = childBox.center().x() > parentBox.center().x();
                    const QPointF a = rendered.mindMap ? QPointF(right ? parentBox.right() : parentBox.left(), parentBox.center().y())
                                                       : QPointF(parentBox.center().x(), parentBox.bottom());
                    const QPointF b = rendered.mindMap ? QPointF(right ? childBox.left() : childBox.right(), childBox.center().y())
                                                       : QPointF(childBox.center().x(), childBox.top());
                    connector(out, id++, ids.value(node.id), ids.value(child), a, b,
                              rendered.mindMap ? (right ? 3 : 1) : 2, rendered.mindMap ? (right ? 1 : 3) : 0,
                              edgeStyle, rendered.mindMap);
                }
            }
            const auto images = slideImages(page, nodeImages);
            for (const Node &node : page.tree.nodes) {
                const bool root = node.id == page.tree.root;
                const bool hasImage = nodeImages.contains(node.id);
                shape(out, ids.value(node.id), QStringLiteral("主题 %1").arg(ids.value(node.id)), displayText(node, textPages),
                      boxes.value(node.id), qBound(500, qRound((node.style.points ? node.style.points : 12) * 100 * scale / 9525),
                                                   node.style.points ? 7200 : 2400),
                      root ? "286557" : "E0EDE4", root ? "FFFFFF" : "203D36", true, root,
                      textPages.contains(node.id) ? QStringLiteral("完整主题文字见第 %1 页起").arg(textPages.value(node.id)) : QString(),
                      hasImage ? qRound64(34 * scale) : 63500, &node.style);
                if (hasImage) {
                    const QRectF box = boxes.value(node.id);
                    picture(out, id++, images.value(nodeImages.value(node.id)),
                            QRectF(box.left() + 5 * scale, box.center().y() - 12 * scale, 24 * scale, 24 * scale));
                }
            }
        }
        out.writeEndElement();
        out.writeEndElement();
        out.writeStartElement("p:clrMapOvr");
        empty(out, "a:masterClrMapping");
        out.writeEndElement();
        out.writeEndElement();
    });
}

using Part = PackagePart;

QByteArray relationships(const QList<QPair<QString, QString>> &items) {
    return xml([&](QXmlStreamWriter &out) {
        out.writeStartElement("Relationships");
        out.writeDefaultNamespace("http://schemas.openxmlformats.org/package/2006/relationships");
        for (int i = 0; i < items.size(); ++i)
            empty(out, "Relationship", {{"Id", QStringLiteral("rId%1").arg(i + 1)},
                  {"Type", QString::fromLatin1(officeRels) + "/" + items.at(i).first}, {"Target", items.at(i).second}});
        out.writeEndElement();
    });
}

QByteArray themeXml() {
    return xml([](QXmlStreamWriter &out) {
        out.writeStartElement("a:theme");
        out.writeNamespace(drawing, "a");
        out.writeAttribute("name", "ZhiLi");
        out.writeStartElement("a:themeElements");
        out.writeStartElement("a:clrScheme"); out.writeAttribute("name", "ZhiLi");
        const QStringList names{"dk1", "lt1", "dk2", "lt2", "accent1", "accent2", "accent3", "accent4", "accent5", "accent6", "hlink", "folHlink"};
        const QStringList colors{"203D36", "FFFFFF", "286557", "F7F7F1", "286557", "B97035", "92AFA3", "E0EDE4", "58746A", "D7C7A3", "0563C1", "954F72"};
        for (int i = 0; i < names.size(); ++i) {
            out.writeStartElement("a:" + names.at(i)); empty(out, "a:srgbClr", {{"val", colors.at(i)}}); out.writeEndElement();
        }
        out.writeEndElement();
        out.writeStartElement("a:fontScheme"); out.writeAttribute("name", "ZhiLi");
        for (const QString &kind : {QString("majorFont"), QString("minorFont")}) {
            out.writeStartElement("a:" + kind);
            empty(out, "a:latin", {{"typeface", "Arial"}});
            empty(out, "a:ea", {{"typeface", "Source Han Sans CN"}});
            empty(out, "a:cs", {{"typeface", "Arial"}});
            empty(out, "a:font", {{"script", "Hans"}, {"typeface", "Source Han Sans CN"}});
            out.writeEndElement();
        }
        out.writeEndElement();
        out.writeStartElement("a:fmtScheme"); out.writeAttribute("name", "ZhiLi");
        out.writeStartElement("a:fillStyleLst"); for (int i = 0; i < 3; ++i) solid(out, "FFFFFF"); out.writeEndElement();
        out.writeStartElement("a:lnStyleLst");
        for (int i = 0; i < 3; ++i) { out.writeStartElement("a:ln"); out.writeAttribute("w", "12700"); solid(out, "92AFA3"); out.writeEndElement(); }
        out.writeEndElement();
        out.writeStartElement("a:effectStyleLst");
        for (int i = 0; i < 3; ++i) { out.writeStartElement("a:effectStyle"); empty(out, "a:effectLst"); out.writeEndElement(); }
        out.writeEndElement();
        out.writeStartElement("a:bgFillStyleLst"); for (int i = 0; i < 3; ++i) solid(out, "FFFFFF"); out.writeEndElement();
        out.writeEndElement(); out.writeEndElement(); out.writeEndElement();
    });
}

quint32 crc32(const QByteArray &bytes) {
    quint32 crc = 0xffffffff;
    for (const char ch : bytes) {
        crc ^= quint8(ch);
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

bool validPng(const QByteArray &png) {
    if (png.size() < 33 || png.size() > 1024 * 1024 || !png.startsWith(QByteArray("\x89PNG\r\n\x1a\n", 8))) return false;
    const auto be32 = [&](int at) {
        return (quint32(quint8(png.at(at))) << 24) | (quint32(quint8(png.at(at + 1))) << 16)
            | (quint32(quint8(png.at(at + 2))) << 8) | quint8(png.at(at + 3));
    };
    bool hasPixels = false;
    for (int at = 8; at + 12 <= png.size();) {
        const quint32 length = be32(at);
        if (length > quint32(png.size() - at - 12)) return false;
        const QByteArray kind = png.mid(at + 4, 4);
        if (crc32(png.mid(at + 4, int(length) + 4)) != be32(at + 8 + int(length))) return false;
        if (at == 8) {
            if (kind != "IHDR" || length != 13 || be32(16) == 0 || be32(20) == 0 || be32(16) > 4096 || be32(20) > 4096) return false;
        } else if (kind == "IHDR") return false;
        if (kind == "IDAT" && length > 0) hasPixels = true;
        at += 12 + int(length);
        if (kind == "IEND") return length == 0 && hasPixels && at == png.size();
    }
    return false;
}

void word(QByteArray *out, quint16 value) {
    out->append(char(value & 0xff)); out->append(char((value >> 8) & 0xff));
}
void dword(QByteArray *out, quint32 value) {
    word(out, quint16(value)); word(out, quint16(value >> 16));
}

}

bool buildStoredPackage(const QVector<PackagePart> &parts, int byteLimit, QByteArray *bytes) {
    QByteArray directory;
    for (const PackagePart &part : parts) {
        const QByteArray name = part.name.toUtf8();
        const quint32 crc = crc32(part.bytes), offset = quint32(bytes->size()), size = quint32(part.bytes.size());
        if (qint64(bytes->size()) + part.bytes.size() + directory.size() + 2 * name.size() + 100 > byteLimit)
            return false;
        dword(bytes, 0x04034b50); word(bytes, 20); word(bytes, 0x800); word(bytes, 0);
        word(bytes, 0); word(bytes, 0x21); dword(bytes, crc); dword(bytes, size); dword(bytes, size);
        word(bytes, quint16(name.size())); word(bytes, 0); bytes->append(name); bytes->append(part.bytes);
        dword(&directory, 0x02014b50); word(&directory, 20); word(&directory, 20); word(&directory, 0x800); word(&directory, 0);
        word(&directory, 0); word(&directory, 0x21); dword(&directory, crc); dword(&directory, size); dword(&directory, size);
        word(&directory, quint16(name.size())); word(&directory, 0); word(&directory, 0); word(&directory, 0);
        word(&directory, 0); dword(&directory, 0); dword(&directory, offset); directory.append(name);
    }
    const quint32 offset = quint32(bytes->size());
    bytes->append(directory);
    dword(bytes, 0x06054b50); word(bytes, 0); word(bytes, 0); word(bytes, quint16(parts.size())); word(bytes, quint16(parts.size()));
    dword(bytes, quint32(directory.size())); dword(bytes, offset); word(bytes, 0);
    return true;
}

bool exportPptx(const Document &document, const QString &path, QString *error) {
    return exportPptx(document, path, {}, error);
}

bool exportPptx(const Document &document, const QString &path,
                const QMap<QString, QByteArray> &nodePngs, QString *error) {
    for(const auto &node:document.nodes) if(!node.imagePng.isEmpty() || !node.links.isEmpty())
        return fail(error,QStringLiteral("PPTX 分页暂不能完整保留主题图片与链接。请保存为 SMAP 或导出 PNG、SVG、PDF。"));
    if (!document.floating.isEmpty() || !document.relationships.isEmpty() || !document.boundaries.isEmpty())
        return fail(error, QStringLiteral("当前 PPTX 分页尚不能完整保留关系线、外框和浮动主题。请先使用 SVG、PDF、PNG、draw.io 或 SMAP 导出。"));
    Document checked;
    if (!checked.parse(document.serialize(), error)) return false;
    for (const Node &node : document.nodes)
        if (!xmlText(node.text)) return fail(error, QStringLiteral("主题文字包含 PowerPoint 不支持的控制字符，请清理后重试"));
    QMap<QString, int> nodeImages;
    QMap<QByteArray, int> imageIds;
    QVector<QByteArray> images;
    qint64 imageBytes = 0;
    for (auto it = nodePngs.cbegin(); it != nodePngs.cend(); ++it) {
        const QByteArray &png = it.value();
        if (!document.nodes.contains(it.key()) || !validPng(png))
            return fail(error, QStringLiteral("主题符号必须为有效的小尺寸 PNG 图片"));
        if (!imageIds.contains(png)) {
            imageBytes += png.size();
            if (imageBytes > maxPackageBytes) return fail(error, QStringLiteral("符号图片超过 64 MiB 导出上限"));
            images.append(png);
            imageIds.insert(png, images.size());
        }
        nodeImages.insert(it.key(), imageIds.value(png));
    }
    QVector<Page> pages;
    QMap<QString, int> textPages;
    if (!plan(document, &pages, &textPages, error)) return false;
    QVector<Part> parts;
    const auto add = [&](const QString &name, const QByteArray &data) { parts.append({name, data}); };
    add("[Content_Types].xml", xml([&](QXmlStreamWriter &out) {
        out.writeStartElement("Types"); out.writeDefaultNamespace("http://schemas.openxmlformats.org/package/2006/content-types");
        empty(out, "Default", {{"Extension", "rels"}, {"ContentType", "application/vnd.openxmlformats-package.relationships+xml"}});
        empty(out, "Default", {{"Extension", "xml"}, {"ContentType", "application/xml"}});
        if (!images.isEmpty()) empty(out, "Default", {{"Extension", "png"}, {"ContentType", "image/png"}});
        const auto type = [&](const QString &name, const QString &content) { empty(out, "Override", {{"PartName", "/" + name}, {"ContentType", content}}); };
        type("ppt/presentation.xml", "application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml");
        type("ppt/slideMasters/slideMaster1.xml", "application/vnd.openxmlformats-officedocument.presentationml.slideMaster+xml");
        type("ppt/slideLayouts/slideLayout1.xml", "application/vnd.openxmlformats-officedocument.presentationml.slideLayout+xml");
        type("ppt/theme/theme1.xml", "application/vnd.openxmlformats-officedocument.theme+xml");
        type("docProps/app.xml", "application/vnd.openxmlformats-officedocument.extended-properties+xml");
        type("docProps/custom.xml", "application/vnd.openxmlformats-officedocument.custom-properties+xml");
        for (int i = 0; i < pages.size(); ++i) type(QStringLiteral("ppt/slides/slide%1.xml").arg(i + 1), "application/vnd.openxmlformats-officedocument.presentationml.slide+xml");
        out.writeEndElement();
    }));
    add("_rels/.rels", relationships({{"officeDocument", "ppt/presentation.xml"}, {"extended-properties", "docProps/app.xml"}, {"custom-properties", "docProps/custom.xml"}}));
    add("docProps/app.xml", xml([&](QXmlStreamWriter &out) {
        out.writeStartElement("Properties"); out.writeDefaultNamespace("http://schemas.openxmlformats.org/officeDocument/2006/extended-properties");
        out.writeTextElement("Application", "ZhiLi"); out.writeTextElement("PresentationFormat", "On-screen Show (16:9)");
        out.writeTextElement("Slides", QString::number(pages.size())); out.writeEndElement();
    }));
    add("docProps/custom.xml", xml([&](QXmlStreamWriter &out) {
        out.writeStartElement("Properties"); out.writeDefaultNamespace("http://schemas.openxmlformats.org/officeDocument/2006/custom-properties");
        out.writeNamespace("http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes", "vt");
        const QList<QPair<QString, QString>> values{{"ZhiLiSourceNodes", QString::number(document.nodes.size())}, {"ZhiLiSlideCount", QString::number(pages.size())},
            {"ZhiLiCollapsedPolicy", "Include all collapsed descendants"}, {"ZhiLiTextPolicy", "Complete topic text in diagrams or explicitly referenced continuation pages"},
            {"ZhiLiLayoutPolicy", "Paginated branch views; orientation may change for readable text without changing tree relationships"},
            {"ZhiLiSymbolPolicy", images.isEmpty() ? "No symbol images supplied" : "Symbols preserved as separate PNG pictures; text, topic shapes and connectors remain editable"},
            {"ZhiLiLimitations", "No application-specific notes; fonts are not embedded; symbol pictures are not editable vector drawings"}};
        for (int i = 0; i < values.size(); ++i) {
            out.writeStartElement("property"); out.writeAttribute("fmtid", "{D5CDD505-2E9C-101B-9397-08002B2CF9AE}");
            out.writeAttribute("pid", QString::number(i + 2)); out.writeAttribute("name", values.at(i).first);
            out.writeTextElement("vt:lpwstr", values.at(i).second); out.writeEndElement();
        }
        out.writeEndElement();
    }));
    add("ppt/presentation.xml", xml([&](QXmlStreamWriter &out) {
        out.writeStartElement("p:presentation"); out.writeNamespace(presentation, "p"); out.writeNamespace(drawing, "a"); out.writeNamespace(officeRels, "r");
        out.writeStartElement("p:sldMasterIdLst"); empty(out, "p:sldMasterId", {{"id", "2147483648"}, {"r:id", "rId1"}}); out.writeEndElement();
        out.writeStartElement("p:sldIdLst");
        for (int i = 0; i < pages.size(); ++i) empty(out, "p:sldId", {{"id", QString::number(256 + i)}, {"r:id", QStringLiteral("rId%1").arg(i + 2)}});
        out.writeEndElement();
        empty(out, "p:sldSz", {{"cx", QString::number(slideWidth)}, {"cy", QString::number(slideHeight)}, {"type", "custom"}});
        empty(out, "p:notesSz", {{"cx", "6858000"}, {"cy", "9144000"}}); empty(out, "p:defaultTextStyle"); out.writeEndElement();
    }));
    QList<QPair<QString, QString>> slideRels{{"slideMaster", "slideMasters/slideMaster1.xml"}};
    for (int i = 0; i < pages.size(); ++i) slideRels.append({"slide", QStringLiteral("slides/slide%1.xml").arg(i + 1)});
    add("ppt/_rels/presentation.xml.rels", relationships(slideRels));
    add("ppt/theme/theme1.xml", themeXml());
    add("ppt/slideMasters/slideMaster1.xml", xml([](QXmlStreamWriter &out) {
        out.writeStartElement("p:sldMaster"); out.writeNamespace(presentation, "p"); out.writeNamespace(drawing, "a"); out.writeNamespace(officeRels, "r");
        out.writeStartElement("p:cSld"); out.writeStartElement("p:spTree"); groupProperties(out); out.writeEndElement(); out.writeEndElement();
        empty(out, "p:clrMap", {{"accent1", "accent1"}, {"accent2", "accent2"}, {"accent3", "accent3"}, {"accent4", "accent4"}, {"accent5", "accent5"}, {"accent6", "accent6"},
            {"bg1", "lt1"}, {"bg2", "lt2"}, {"folHlink", "folHlink"}, {"hlink", "hlink"}, {"tx1", "dk1"}, {"tx2", "dk2"}});
        out.writeStartElement("p:sldLayoutIdLst"); empty(out, "p:sldLayoutId", {{"id", "2147483649"}, {"r:id", "rId1"}}); out.writeEndElement();
        out.writeStartElement("p:txStyles"); empty(out, "p:titleStyle"); empty(out, "p:bodyStyle"); empty(out, "p:otherStyle"); out.writeEndElement(); out.writeEndElement();
    }));
    add("ppt/slideMasters/_rels/slideMaster1.xml.rels", relationships({{"slideLayout", "../slideLayouts/slideLayout1.xml"}, {"theme", "../theme/theme1.xml"}}));
    add("ppt/slideLayouts/slideLayout1.xml", xml([](QXmlStreamWriter &out) {
        out.writeStartElement("p:sldLayout"); out.writeNamespace(presentation, "p"); out.writeNamespace(drawing, "a"); out.writeAttribute("type", "blank"); out.writeAttribute("preserve", "1");
        out.writeStartElement("p:cSld"); out.writeAttribute("name", "Blank"); out.writeStartElement("p:spTree"); groupProperties(out); out.writeEndElement(); out.writeEndElement();
        out.writeStartElement("p:clrMapOvr"); empty(out, "a:masterClrMapping"); out.writeEndElement(); out.writeEndElement();
    }));
    add("ppt/slideLayouts/_rels/slideLayout1.xml.rels", relationships({{"slideMaster", "../slideMasters/slideMaster1.xml"}}));
    for (int i = 0; i < images.size(); ++i) add(QStringLiteral("ppt/media/image%1.png").arg(i + 1), images.at(i));
    for (int i = 0; i < pages.size(); ++i) {
        add(QStringLiteral("ppt/slides/slide%1.xml").arg(i + 1), slideXml(pages.at(i), i + 1, pages.size(), textPages, nodeImages));
        QList<QPair<QString, QString>> rels{{"slideLayout", "../slideLayouts/slideLayout1.xml"}};
        const auto usedImages = slideImages(pages.at(i), nodeImages);
        QMap<int, int> byRelationship;
        for (auto it = usedImages.cbegin(); it != usedImages.cend(); ++it) byRelationship.insert(it.value(), it.key());
        for (int asset : byRelationship) rels.append({"image", QStringLiteral("../media/image%1.png").arg(asset)});
        add(QStringLiteral("ppt/slides/_rels/slide%1.xml.rels").arg(i + 1), relationships(rels));
    }
    QByteArray bytes;
    if (!buildStoredPackage(parts, maxPackageBytes, &bytes))
        return fail(error, QStringLiteral("PowerPoint 文件超过 64 MiB 导出上限，请分拆导图"));
    return writeAtomically(path, bytes, error);
}
