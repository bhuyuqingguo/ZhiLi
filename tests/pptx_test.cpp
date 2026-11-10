#include "../src/pptx.h"

namespace {
quint16 u16(const QByteArray &bytes, int at) {
    return quint8(bytes.at(at)) | (quint16(quint8(bytes.at(at + 1))) << 8);
}
quint32 u32(const QByteArray &bytes, int at) {
    return u16(bytes, at) | (quint32(u16(bytes, at + 2)) << 16);
}

QMap<QString, QByteArray> unpack(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = file.readAll();
    QMap<QString, QByteArray> parts;
    int offset = 0;
    while (offset + 30 <= bytes.size() && u32(bytes, offset) == 0x04034b50) {
        if (u16(bytes, offset + 8) != 0) return {};
        const quint32 size = u32(bytes, offset + 18);
        const int nameLength = u16(bytes, offset + 26), extraLength = u16(bytes, offset + 28);
        const qint64 end = qint64(offset) + 30 + nameLength + extraLength + size;
        if (end > bytes.size()) return {};
        const QString name = QString::fromUtf8(bytes.mid(offset + 30, nameLength));
        if (parts.contains(name)) return {};
        parts.insert(name, bytes.mid(offset + 30 + nameLength + extraLength, int(size)));
        offset = int(end);
    }
    if (offset + 4 > bytes.size() || u32(bytes, offset) != 0x02014b50) return {};
    return parts;
}

Document tree(int count, bool star = false) {
    Document doc;
    doc.root = "0";
    for (int i = 0; i < count; ++i) {
        Node node;
        node.id = QString::number(i);
        node.text = QStringLiteral("topic-%1 中文主题").arg(i);
        if (star && i == 0) {
            for (int child = 1; child < count; ++child) node.children.append(QString::number(child));
        } else if (!star) {
            for (int child = i * 4 + 1; child <= i * 4 + 4 && child < count; ++child)
                node.children.append(QString::number(child));
        }
        node.collapsed = i % 3 == 0;
        doc.nodes.insert(node.id, node);
    }
    return doc;
}

bool inspect(const QMap<QString, QByteArray> &parts, int expectedEdges, int *slideCount = nullptr, QString *allText = nullptr, bool allowPictures = false) {
    if (!parts.contains("[Content_Types].xml") || !parts.contains("ppt/presentation.xml") || !parts.contains("ppt/theme/theme1.xml")) return false;
    int slides = 0, edges = 0;
    QString texts;
    for (auto it = parts.cbegin(); it != parts.cend(); ++it) {
        if (it.key().startsWith("ppt/media/")) continue;
        QXmlStreamReader reader(it.value());
        const bool slide = it.key().startsWith("ppt/slides/slide") && it.key().endsWith(".xml");
        if (slide) ++slides;
        QSet<QString> ids;
        QStringList connections;
        while (!reader.atEnd()) {
            reader.readNext();
            if (slide && reader.isEndElement() && reader.name() == "p") texts += '\n';
            if (!reader.isStartElement()) continue;
            if (slide && reader.name() == "cNvPr") {
                const QString id = reader.attributes().value("id").toString();
                if (ids.contains(id)) return false;
                ids.insert(id);
            }
            if (slide && reader.name() == "cxnSp") ++edges;
            if (slide && (reader.name() == "stCxn" || reader.name() == "endCxn"))
                connections.append(reader.attributes().value("id").toString());
            if (slide && reader.name() == "pic" && !allowPictures) return false;
            if (slide && reader.name() == "t") texts += reader.readElementText();
        }
        if (reader.hasError()) {
            qCritical() << it.key() << reader.errorString();
            return false;
        }
        for (const QString &target : connections) if (!ids.contains(target)) return false;
    }
    if (slideCount) *slideCount = slides;
    if (allText) *allText = texts;
    return edges == expectedEdges && slides > 0;
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
#define CHECK(condition) do { if (!(condition)) { qCritical() << __LINE__ << #condition; return 1; } } while (false)
    QTemporaryDir temp;
    CHECK(temp.isValid());
    QString error;
    Document doc = tree(7);
    doc.nodes["1"].text = QStringLiteral("中文 & <研发> \"目标\"\n第二行 🌿");
    const QByteArray original = doc.serialize();
    const QString small = temp.filePath("small.pptx");
    CHECK(exportPptx(doc, small, &error) && error.isEmpty());
    CHECK(doc.serialize() == original);
    auto parts = unpack(small);
    int slides = 0;
    QString text;
    CHECK(inspect(parts, 6, &slides, &text) && slides == 1);
    CHECK(text.contains(QStringLiteral("中文 & <研发> \"目标\"")) && text.contains(QStringLiteral("第二行 🌿")));
    CHECK(parts.value("docProps/custom.xml").contains("Include all collapsed descendants"));
    CHECK(!parts.value("ppt/slides/slide1.xml").contains("<p:pic"));

    Document large = tree(101);
    large.mindMap = false;
    const QString largePath = temp.filePath("large.pptx");
    CHECK(exportPptx(large, largePath, &error));
    parts = unpack(largePath);
    CHECK(inspect(parts, 100, &slides, &text) && slides > 1);
    for (int i = 0; i < 101; ++i) CHECK(text.contains(QStringLiteral("topic-%1 中文主题").arg(i)));

    Document deep = tree(20);
    for (int i = 0; i < 20; ++i) {
        deep.nodes[QString::number(i)].children.clear();
        if (i < 19) deep.nodes[QString::number(i)].children.append(QString::number(i + 1));
    }
    CHECK(exportPptx(deep, temp.filePath("deep.pptx"), &error));
    CHECK(inspect(unpack(temp.filePath("deep.pptx")), 19, &slides, &text) && slides > 1);
    for (int i = 0; i < 20; ++i) CHECK(text.contains(QStringLiteral("topic-%1 中文主题").arg(i)));
    CHECK(parts.value("docProps/custom.xml").contains("101"));

    Document wide = tree(101, true);
    CHECK(exportPptx(wide, temp.filePath("wide.pptx"), &error));
    CHECK(inspect(unpack(temp.filePath("wide.pptx")), 100, &slides, &text));
    for (int i = 0; i < 101; ++i) CHECK(text.contains(QStringLiteral("topic-%1 中文主题").arg(i)));

    Document longText = tree(3);
    longText.nodes["1"].text = QStringLiteral("完整长主题 🌿\n") + QString(1500, QChar(0x679D)) + QStringLiteral("全文末尾-END");
    longText.nodes["2"].text = QStringLiteral("多行\n").repeated(40) + QStringLiteral("最后一行");
    const QString longPath = temp.filePath("long-text.pptx");
    CHECK(exportPptx(longText, longPath, &error));
    parts = unpack(longPath);
    CHECK(inspect(parts, 2, &slides, &text) && slides > 3);
    CHECK(text.contains(QStringLiteral("全文见第")) && text.contains("END") && text.contains(QStringLiteral("最后一行")));
    int repeated = 0;
    for (const QChar ch : text) if (ch == QChar(0x679D)) ++repeated;
    CHECK(repeated >= 1500);

    const QString protectedPath = temp.filePath("protected.pptx");
    CHECK(writeAtomically(protectedPath, "preserve original", &error));
    Document invalid = doc;
    invalid.nodes["1"].children.append("0");
    CHECK(!exportPptx(invalid, protectedPath, &error) && !error.isEmpty());
    invalid = doc;
    invalid.nodes["1"].text = QString(QChar(1));
    CHECK(!exportPptx(invalid, protectedPath, &error) && !error.isEmpty());
    const Document tooWide = tree(8000, true);
    CHECK(!exportPptx(tooWide, protectedPath, &error) && error.contains("500"));
    QFile preserved(protectedPath);
    CHECK(preserved.open(QIODevice::ReadOnly) && preserved.readAll() == "preserve original");
    CHECK(!exportPptx(doc, temp.path(), &error) && !error.isEmpty());

    const QByteArray png = QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAYAAABzenr0AAAANUlEQVR4nO3OQQEAMAjEsGNq5mgK8C9hyOCTGmjq9vtZ7GzOAQAAAAAAAAAAAAAAAAAAkmQAfG0CI8JciVoAAAAASUVORK5CYII=");
    QMap<QString, QByteArray> icons{{"0", png}, {"1", png}};
    const QString iconPath = temp.filePath("icons.pptx");
    CHECK(exportPptx(doc, iconPath, icons, &error));
    parts = unpack(iconPath);
    CHECK(inspect(parts, 6, &slides, &text, true));
    CHECK(parts.contains("ppt/media/image1.png") && !parts.contains("ppt/media/image2.png"));
    CHECK(parts.value("ppt/media/image1.png") == png);
    CHECK(parts.value("ppt/slides/slide1.xml").count("<p:pic>") == 2);
    CHECK(parts.value("ppt/slides/_rels/slide1.xml.rels").contains("../media/image1.png"));
    CHECK(parts.value("docProps/custom.xml").contains("separate PNG pictures"));
    icons["1"] = "not PNG";
    CHECK(!exportPptx(doc, protectedPath, icons, &error));
    icons["1"] = png;
    icons["1"][50] = char(icons["1"].at(50) ^ 1);
    CHECK(!exportPptx(doc, protectedPath, icons, &error));
    icons = {{"missing-node", png}};
    CHECK(!exportPptx(doc, protectedPath, icons, &error));
    preserved.seek(0);
    CHECK(preserved.readAll() == "preserve original");

    const QString sampleDir = qEnvironmentVariable("ZHILI_PPTX_SAMPLE_DIR");
    if (!sampleDir.isEmpty()) {
        CHECK(QDir().mkpath(sampleDir));
        CHECK(exportPptx(doc, QDir(sampleDir).filePath("editable-small.pptx"), &error));
        CHECK(exportPptx(large, QDir(sampleDir).filePath("editable-101-topics.pptx"), &error));
        CHECK(exportPptx(longText, QDir(sampleDir).filePath("editable-long-text.pptx"), &error));
        CHECK(exportPptx(doc, QDir(sampleDir).filePath("editable-symbols.pptx"), QMap<QString, QByteArray>{{"0", png}, {"1", png}}, &error));
    }
    qInfo() << "ZhiLi editable PPTX tests passed";
    return 0;
#undef CHECK
}
