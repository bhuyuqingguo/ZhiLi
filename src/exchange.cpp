#include "exchange.h"
#include "clipboard.h"
#include "layout.h"
#include "package_writer.h"
#include <QtMath>
#include <cmath>
#include <functional>

namespace {
void writeOutline(QXmlStreamWriter &xml, const Document &document, bool opml) {
    if (opml) {
        xml.writeStartElement("opml");
        xml.writeAttribute("version", "2.0");
        xml.writeNamespace("urn:zhili:exchange:1", "zhili");
        xml.writeAttribute("zhili:center", "outline");
        xml.writeStartElement("head");
        xml.writeTextElement("title", document.nodes.value(document.root).text);
        xml.writeEndElement();
        xml.writeStartElement("body");
    } else {
        xml.writeStartElement("map");
        xml.writeAttribute("version", "1.0.1");
    }
    std::function<void(const QString &)> write = [&](const QString &id) {
        const auto &node = document.nodes[id];
        xml.writeStartElement(opml ? "outline" : "node");
        xml.writeAttribute(opml ? "text" : "TEXT", node.text);
        if (!opml && node.collapsed) xml.writeAttribute("FOLDED", "true");
        for (const auto &child : node.children) write(child);
        xml.writeEndElement();
    };
    write(document.root);
    if (opml) xml.writeEndElement();
    xml.writeEndElement();
}

struct DiagramLayout {
    QMap<QString, QSizeF> sizes;
    QMap<QString, QString> ids;
    QMap<QString, QPointF> positions;
    QRectF bounds;
    int nextId = 2;

    explicit DiagramLayout(Document document) {
        for (auto &node : document.nodes) {
            node.collapsed = false;
            int lines = 0;
            for (const auto &line : node.text.split('\n')) lines += qMax(1, (line.size() + 14) / 15);
            sizes.insert(node.id, QSizeF(230, qMax(54, 20 + lines * 22)));
            ids.insert(node.id, QString::number(nextId++));
        }
        positions = layoutNodes(document, sizes);
        for (auto it = positions.cbegin(); it != positions.cend(); ++it) {
            const auto size = sizes.value(it.key());
            bounds |= QRectF(it.value() - QPointF(size.width() / 2, size.height() / 2), size);
        }
    }
};

void writeDrawio(QXmlStreamWriter &xml, const Document &document) {
    DiagramLayout layout(document);
    xml.writeStartElement("mxfile");
    xml.writeStartElement("diagram");
    xml.writeAttribute("id", "zhili");
    xml.writeAttribute("name", QStringLiteral("枝理导图"));
    xml.writeStartElement("mxGraphModel");
    xml.writeStartElement("root");
    xml.writeEmptyElement("mxCell");
    xml.writeAttribute("id", "0");
    xml.writeEmptyElement("mxCell");
    xml.writeAttribute("id", "1");
    xml.writeAttribute("parent", "0");
    for (const auto &node : qAsConst(document.nodes)) {
        const bool root = node.id == document.root;
        QString style = root
            ? "rounded=1;whiteSpace=wrap;html=0;fillColor=#225b49;strokeColor=#225b49;fontColor=#ffffff;fontStyle=1;"
            : "rounded=1;whiteSpace=wrap;html=0;fillColor=#f6f8f5;strokeColor=#92afa3;fontColor=#203d36;";
        if (node.style.alignment == TextAlignment::Left) style += "align=left;";
        if (node.style.alignment == TextAlignment::Right) style += "align=right;";
        if (node.style.points) style += QStringLiteral("fontSize=%1;").arg(node.style.points);
        const int fontStyle = (node.style.weight == -1 ? (root ? 1 : 0) : node.style.weight)
            | (node.style.italic ? 2 : 0) | (node.style.underline ? 4 : 0) | (node.style.strikeOut ? 8 : 0);
        style += QStringLiteral("fontStyle=%1;").arg(fontStyle);
        if (!node.style.color.isEmpty()) style += "fontColor=" + node.style.color + ';';
        xml.writeStartElement("mxCell");
        xml.writeAttribute("id", layout.ids.value(node.id));
        xml.writeAttribute("value", node.text);
        xml.writeAttribute("style", style);
        xml.writeAttribute("vertex", "1");
        xml.writeAttribute("parent", "1");
        const auto size = layout.sizes.value(node.id);
        const QPointF point = layout.positions.value(node.id) - layout.bounds.topLeft()
            - QPointF(size.width() / 2 - 24, size.height() / 2 - 24);
        xml.writeEmptyElement("mxGeometry");
        xml.writeAttribute("x", QString::number(point.x()));
        xml.writeAttribute("y", QString::number(point.y()));
        xml.writeAttribute("width", QString::number(size.width()));
        xml.writeAttribute("height", QString::number(size.height()));
        xml.writeAttribute("as", "geometry");
        xml.writeEndElement();
    }
    QString style = "endArrow=none;strokeColor=#63887c;";
    if (document.connectorStyle == ConnectorStyle::Curved ||
        (document.connectorStyle == ConnectorStyle::Automatic && document.mindMap))
        style += "edgeStyle=orthogonalEdgeStyle;curved=1;";
    else if (document.connectorStyle != ConnectorStyle::Straight)
        style += "edgeStyle=orthogonalEdgeStyle;";
    for (const auto &node : qAsConst(document.nodes)) {
        for (const auto &child : node.children) {
            if (document.floating.contains(child)) continue;
            xml.writeStartElement("mxCell");
            xml.writeAttribute("id", QString::number(layout.nextId++));
            xml.writeAttribute("style", style);
            xml.writeAttribute("edge", "1");
            xml.writeAttribute("parent", "1");
            xml.writeAttribute("source", layout.ids.value(node.id));
            xml.writeAttribute("target", layout.ids.value(child));
            xml.writeEmptyElement("mxGeometry");
            xml.writeAttribute("relative", "1");
            xml.writeAttribute("as", "geometry");
            xml.writeEndElement();
        }
    }
    for (const auto &link : document.relationships) {
        xml.writeStartElement("mxCell");
        xml.writeAttribute("id", QString::number(layout.nextId++));
        xml.writeAttribute("value", link.text);
        xml.writeAttribute("edge", "1"); xml.writeAttribute("parent", "1");
        xml.writeAttribute("source", layout.ids.value(link.from)); xml.writeAttribute("target", layout.ids.value(link.to));
        xml.writeAttribute("style", link.straight ? "endArrow=classic;dashed=1;strokeColor=#90704b;" : "endArrow=classic;curved=1;dashed=1;strokeColor=#90704b;");
        xml.writeStartElement("mxGeometry"); xml.writeAttribute("relative", "1"); xml.writeAttribute("as", "geometry");
        if (!link.straight) {
            const QPointF point = (layout.positions.value(link.from) + layout.positions.value(link.to)) / 2 + link.bend - layout.bounds.topLeft() + QPointF(24,24);
            xml.writeStartElement("Array"); xml.writeAttribute("as", "points");
            xml.writeEmptyElement("mxPoint"); xml.writeAttribute("x", QString::number(point.x())); xml.writeAttribute("y", QString::number(point.y()));
            xml.writeEndElement();
        }
        xml.writeEndElement(); xml.writeEndElement();
    }
    for (const auto &frame : document.boundaries) {
        QRectF bounds; QStringList pending = frame.members; QSet<QString> seen;
        while (!pending.isEmpty()) {
            const auto id = pending.takeLast(); if (seen.contains(id)) continue; seen.insert(id);
            const auto size = layout.sizes.value(id);
            bounds |= QRectF(layout.positions.value(id) - QPointF(size.width()/2, size.height()/2), size);
            pending += document.nodes.value(id).children;
        }
        bounds.adjust(-20,-20,20,20); bounds.translate(-layout.bounds.topLeft() + QPointF(24,24));
        xml.writeStartElement("mxCell"); xml.writeAttribute("id", QString::number(layout.nextId++));
        xml.writeAttribute("value", frame.text); xml.writeAttribute("vertex", "1"); xml.writeAttribute("parent", "1");
        xml.writeAttribute("style", "rounded=1;fillColor=none;strokeColor=#71a48b;dashed=1;verticalAlign=top;pointerEvents=0;");
        xml.writeEmptyElement("mxGeometry"); xml.writeAttribute("as", "geometry");
        xml.writeAttribute("x", QString::number(bounds.x())); xml.writeAttribute("y", QString::number(bounds.y()));
        xml.writeAttribute("width", QString::number(bounds.width())); xml.writeAttribute("height", QString::number(bounds.height()));
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndElement();
}

QByteArray writeCanvas(const Document &document) {
    const DiagramLayout layout(document);
    QJsonArray nodes, edges;
    int nextEdge = layout.nextId;
    for (const auto &node : document.nodes) {
        const auto size = layout.sizes.value(node.id);
        const auto point = layout.positions.value(node.id) - QPointF(size.width() / 2, size.height() / 2);
        nodes.append(QJsonObject{{"id", layout.ids.value(node.id)}, {"type", "text"}, {"text", node.text},
                                 {"x", qRound(point.x())}, {"y", qRound(point.y())},
                                 {"width", qCeil(size.width())}, {"height", qCeil(size.height())}});
        for (const auto &child : node.children)
            edges.append(QJsonObject{{"id", QString::number(nextEdge++)}, {"fromNode", layout.ids.value(node.id)},
                                     {"toNode", layout.ids.value(child)}, {"toEnd", "none"}});
    }
    return QJsonDocument(QJsonObject{{"nodes", nodes}, {"edges", edges}}).toJson(QJsonDocument::Compact);
}

const char *visioNamespace = "http://schemas.microsoft.com/office/visio/2012/main";

void visioCell(QXmlStreamWriter &xml, const QString &name, const QString &value, const QString &formula = {}) {
    xml.writeEmptyElement("Cell");
    xml.writeAttribute("N", name);
    xml.writeAttribute("V", value);
    if (!formula.isEmpty()) xml.writeAttribute("F", formula);
}

void visioNumber(QXmlStreamWriter &xml, const QString &name, qreal value, const QString &formula = {}) {
    visioCell(xml, name, QString::number(value, 'g', 15), formula);
}

void visioGeometry(QXmlStreamWriter &xml, qreal width, qreal height, bool connector) {
    xml.writeStartElement("Section");
    xml.writeAttribute("N", "Geometry");
    xml.writeAttribute("IX", "0");
    if (connector) visioCell(xml, "NoFill", "1");
    const QVector<QPointF> points = connector ? QVector<QPointF>{{0, 0}, {width, 0}}
        : QVector<QPointF>{{0, 0}, {width, 0}, {width, height}, {0, height}, {0, 0}};
    for (int i = 0; i < points.size(); ++i) {
        const auto point = points.at(i);
        xml.writeStartElement("Row");
        xml.writeAttribute("T", i == 0 ? "MoveTo" : "LineTo");
        xml.writeAttribute("IX", QString::number(i + 1));
        visioNumber(xml, "X", point.x(), point.x() != 0 ? "Width" : QString());
        visioNumber(xml, "Y", point.y(), point.y() != 0 ? "Height" : QString());
        xml.writeEndElement();
    }
    xml.writeEndElement();
}

bool writeVisio(const Document &document, QByteArray *bytes) {
    const DiagramLayout layout(document);
    const qreal scale = 96;
    const qreal pageWidth = (layout.bounds.width() + 48) / scale;
    const qreal pageHeight = (layout.bounds.height() + 48) / scale;
    // Visio's drawing page has a finite physical size. Keep extreme maps in SVG/draw.io.
    if (pageWidth > 240 || pageHeight > 240) return false;
    const auto position = [&](const QString &id) {
        const auto p = layout.positions.value(id) - layout.bounds.topLeft() + QPointF(24, 24);
        return QPointF(p.x() / scale, pageHeight - p.y() / scale);
    };
    QByteArray page;
    QXmlStreamWriter xml(&page);
    xml.writeStartDocument();
    xml.writeStartElement("PageContents");
    xml.writeDefaultNamespace(visioNamespace);
    xml.writeAttribute("xml:space", "preserve");
    xml.writeStartElement("Shapes");
    struct Link { QString id, from, to; };
    QVector<Link> links;
    int nextId = layout.nextId;
    for (const auto &node : document.nodes) {
        for (const auto &child : node.children) {
            const Link link{QString::number(nextId++), layout.ids.value(node.id), layout.ids.value(child)};
            links.append(link);
            const auto a = position(node.id), b = position(child), delta = b - a;
            const qreal length = std::hypot(delta.x(), delta.y());
            xml.writeStartElement("Shape");
            xml.writeAttribute("ID", link.id);
            xml.writeAttribute("Type", "Shape");
            visioNumber(xml, "BeginX", a.x(), "Sheet." + link.from + "!PinX");
            visioNumber(xml, "BeginY", a.y(), "Sheet." + link.from + "!PinY");
            visioNumber(xml, "EndX", b.x(), "Sheet." + link.to + "!PinX");
            visioNumber(xml, "EndY", b.y(), "Sheet." + link.to + "!PinY");
            visioNumber(xml, "PinX", (a.x() + b.x()) / 2, "GUARD((BeginX+EndX)/2)");
            visioNumber(xml, "PinY", (a.y() + b.y()) / 2, "GUARD((BeginY+EndY)/2)");
            visioNumber(xml, "Width", length, "GUARD(SQRT((EndX-BeginX)^2+(EndY-BeginY)^2))");
            visioNumber(xml, "Height", 0);
            visioNumber(xml, "LocPinX", length / 2, "Width*0.5");
            visioNumber(xml, "LocPinY", 0);
            visioNumber(xml, "Angle", std::atan2(delta.y(), delta.x()), "GUARD(ATAN2(EndY-BeginY,EndX-BeginX))");
            visioCell(xml, "LineColor", "#63887c");
            visioGeometry(xml, length, 0, true);
            xml.writeEndElement();
        }
    }
    for (const auto &node : document.nodes) {
        const auto point = position(node.id);
        const auto size = layout.sizes.value(node.id) / scale;
        xml.writeStartElement("Shape");
        xml.writeAttribute("ID", layout.ids.value(node.id));
        xml.writeAttribute("Type", "Shape");
        visioNumber(xml, "PinX", point.x());
        visioNumber(xml, "PinY", point.y());
        visioNumber(xml, "Width", size.width());
        visioNumber(xml, "Height", size.height());
        visioNumber(xml, "LocPinX", size.width() / 2, "Width*0.5");
        visioNumber(xml, "LocPinY", size.height() / 2, "Height*0.5");
        visioCell(xml, "FillForegnd", node.id == document.root ? "#e0ede4" : "#f6f8f5");
        visioCell(xml, "FillPattern", "1");
        visioCell(xml, "LineColor", "#63887c");
        visioCell(xml, "LinePattern", "1");
        visioGeometry(xml, size.width(), size.height(), false);
        xml.writeTextElement("Text", node.text);
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeStartElement("Connects");
    for (const auto &link : links) {
        for (const bool end : {false, true}) {
            xml.writeEmptyElement("Connect");
            xml.writeAttribute("FromSheet", link.id);
            xml.writeAttribute("FromCell", end ? "EndX" : "BeginX");
            xml.writeAttribute("FromPart", end ? "12" : "9");
            xml.writeAttribute("ToSheet", end ? link.to : link.from);
            xml.writeAttribute("ToCell", "PinX");
            xml.writeAttribute("ToPart", "3");
        }
    }
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndDocument();
    if (xml.hasError() || page.size() > Document::maxBytes) return false;

    const QByteArray types = "<Types xmlns='http://schemas.openxmlformats.org/package/2006/content-types'>"
        "<Default Extension='rels' ContentType='application/vnd.openxmlformats-package.relationships+xml'/>"
        "<Default Extension='xml' ContentType='application/xml'/>"
        "<Override PartName='/visio/document.xml' ContentType='application/vnd.ms-visio.drawing.main+xml'/>"
        "<Override PartName='/visio/pages/pages.xml' ContentType='application/vnd.ms-visio.pages+xml'/>"
        "<Override PartName='/visio/pages/page1.xml' ContentType='application/vnd.ms-visio.page+xml'/></Types>";
    const auto relationship = [](const QByteArray &kind, const QByteArray &target) -> QByteArray {
        return "<Relationships xmlns='http://schemas.openxmlformats.org/package/2006/relationships'>"
            "<Relationship Id='rId1' Type='http://schemas.microsoft.com/visio/2010/relationships/"
            + kind + "' Target='" + target + "'/></Relationships>";
    };
    const QByteArray ns(visioNamespace);
    const QByteArray pages = "<Pages xmlns='" + ns + "' xmlns:r='http://schemas.openxmlformats.org/officeDocument/2006/relationships'>"
        "<Page ID='0' Name='ZhiLi' NameU='ZhiLi'><PageSheet>"
        "<Cell N='PageWidth' V='" + QByteArray::number(pageWidth) + "'/>"
        "<Cell N='PageHeight' V='" + QByteArray::number(pageHeight) + "'/>"
        "<Cell N='PageScale' V='1'/><Cell N='DrawingScale' V='1'/>"
        "</PageSheet><Rel r:id='rId1'/></Page></Pages>";
    const QVector<PackagePart> parts{
        {"[Content_Types].xml", types},
        {"_rels/.rels", relationship("document", "visio/document.xml")},
        {"visio/document.xml", "<VisioDocument xmlns='" + ns + "'><DocumentSettings TopPage='0'/></VisioDocument>"},
        {"visio/_rels/document.xml.rels", relationship("pages", "pages/pages.xml")},
        {"visio/pages/pages.xml", pages},
        {"visio/pages/_rels/pages.xml.rels", relationship("page", "page1.xml")},
        {"visio/pages/page1.xml", page},
    };
    return buildStoredPackage(parts, Document::maxBytes, bytes);
}
}

bool exportExchange(const Document &document, const QString &path,
                    const QString &format, QString *error) {
    if (format != "drawio" && (!document.floating.isEmpty() || !document.relationships.isEmpty() || !document.boundaries.isEmpty())) {
        if (error) *error = QStringLiteral("此导图含关系线、外框或浮动主题，当前目标格式尚不能完整保留。请使用 SMAP、draw.io、SVG、PNG 或 PDF。");
        return false;
    }
    if (format == "canvas" || format == "txt") {
        const QByteArray bytes = format == "canvas" ? writeCanvas(document) :
            BranchClipboard::outline(document, document.root).toUtf8() + '\n';
        if (bytes.size() <= Document::maxBytes) return writeAtomically(path, bytes, error);
        if (error) *error = QStringLiteral("转换结果超过 50 MiB");
        return false;
    }
    if (format == "vsdx") {
        QByteArray bytes;
        if (writeVisio(document, &bytes)) return writeAtomically(path, bytes, error);
        if (error) *error = QStringLiteral("Visio 结果超出页面、文件大小限制，或主题含 XML 不支持的字符。");
        return false;
    }
    if (format != "drawio" && format != "opml" && format != "mm") {
        if (error) *error = QStringLiteral("不支持此转换目标");
        return false;
    }
    QByteArray bytes;
    QXmlStreamWriter xml(&bytes);
    xml.writeStartDocument();
    if (format == "drawio") writeDrawio(xml, document);
    else writeOutline(xml, document, format == "opml");
    xml.writeEndDocument();
    if (xml.hasError() || bytes.size() > Document::maxBytes) {
        if (error) *error = QStringLiteral("文字无法写入 XML，或转换结果超过 50 MiB");
        return false;
    }
    return writeAtomically(path, bytes, error);
}
