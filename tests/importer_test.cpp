#include "../src/importer.h"

namespace {
// All fixtures are synthetic and built in memory. No vendor/customer files.
void put16(QByteArray &b, quint16 value) {
    b.append(char(value)); b.append(char(value >> 8));
}
void put32(QByteArray &b, quint32 value) { put16(b, quint16(value)); put16(b, quint16(value >> 16)); }
void set16(QByteArray &b, int at, quint16 value) {
    b[at] = char(value); b[at + 1] = char(value >> 8);
}
void set32(QByteArray &b, int at, quint32 value) { set16(b, at, quint16(value)); set16(b, at + 2, quint16(value >> 16)); }
quint32 crc32(const QByteArray &b) {
    quint32 crc = 0xffffffffu;
    for (unsigned char c : b) {
        crc ^= c;
        for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
struct Part {
    QByteArray name, contents;
    quint16 method, flags;
    Part(QByteArray n, QByteArray c, quint16 m = 0, quint16 f = 0)
        : name(n), contents(c), method(m), flags(f) {}
};
QByteArray zip(const QList<Part> &parts) {
    QByteArray local, directory;
    for (const Part &part : parts) {
        const quint32 offset = quint32(local.size()), crc = crc32(part.contents);
        QByteArray data = part.contents;
        if (part.method == 8) {
            // qCompress is Qt's public API: four-byte size, zlib header,
            // raw DEFLATE payload, Adler32. Store only raw DEFLATE in ZIP.
            data = qCompress(data, 9); data = data.mid(6, data.size() - 10);
        }
        put32(local, 0x04034b50); put16(local, 20); put16(local, part.flags);
        put16(local, part.method); put32(local, 0);
        put32(local, (part.flags & 8) ? 0 : crc);
        put32(local, (part.flags & 8) ? 0 : quint32(data.size()));
        put32(local, (part.flags & 8) ? 0 : quint32(part.contents.size()));
        put16(local, quint16(part.name.size())); put16(local, 0);
        local += part.name; local += data;
        if (part.flags & 8) {
            put32(local, 0x08074b50); put32(local, crc);
            put32(local, quint32(data.size())); put32(local, quint32(part.contents.size()));
        }
        put32(directory, 0x02014b50); put16(directory, 20); put16(directory, 20);
        put16(directory, part.flags); put16(directory, part.method); put32(directory, 0);
        put32(directory, crc); put32(directory, quint32(data.size())); put32(directory, quint32(part.contents.size()));
        put16(directory, quint16(part.name.size())); put16(directory, 0); put16(directory, 0);
        put16(directory, 0); put16(directory, 0); put32(directory, 0); put32(directory, offset);
        directory += part.name;
    }
    const quint32 start = quint32(local.size());
    local += directory;
    put32(local, 0x06054b50); put16(local, 0); put16(local, 0);
    put16(local, quint16(parts.size())); put16(local, quint16(parts.size()));
    put32(local, quint32(directory.size())); put32(local, start); put16(local, 0);
    return local;
}
QByteArray map(const QByteArray &topics) {
    return QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\"?><m:Map xmlns:m=\"http://schemas.mindjet.com/MindManager/Application/2003\"><m:OneTopic>")
        + topics + "</m:OneTopic></m:Map>";
}
QByteArray topic(const QByteArray &id, const QByteArray &text, const QByteArray &children = QByteArray()) {
    return "<m:Topic OId=\"" + id + "\"><m:Text PlainText=\"" + text + "\"/>"
        + (children.isEmpty() ? QByteArray() : "<m:SubTopics>" + children + "</m:SubTopics>") + "</m:Topic>";
}
QByteArray mmap(const QByteArray &xml, quint16 method = 0, quint16 flags = 0) {
    return zip({Part("Document.xml", xml, method, flags)});
}
QByteArray shape(const QByteArray &id, const QByteArray &type, const QByteArray &text,
                 const QByteArray &parent, const QByteArray &order) {
    return "<Shape ID=\"" + id + "\" Type=\"" + type + "\"><Text><TextBlock><Text>"
        + text + "</Text></TextBlock></Text><LevelData><Super V=\"" + parent
        + "\"/><SubLevel V=\"" + order + "\"/></LevelData></Shape>";
}
QByteArray emmx(const QByteArray &shapes) {
    return zip({Part("document.xml", "<Document Version=\"6.2\"/>"), Part("page/page.xml", "<Page>" + shapes + "</Page>", 8)});
}
bool write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.flush();
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
#define CHECK(condition) do { if (!(condition)) { qCritical() << __LINE__ << #condition; return 1; } } while (false)
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString path = dir.filePath("测试 🌿.mmap"), emmxPath = dir.filePath("legacy.emmx");
    const QByteArray basicXml = map(topic("root", "项目 &amp; 🌿&#10; 第二行 ",
        topic("second", "第二分支", topic("grandchild", "细节")) + topic("first", "")));
    const QByteArray basicZip = mmap(basicXml, 8);
    Document doc; QString error; QStringList warnings;
    CHECK(write(path, basicZip));
    CHECK(importMindMap(path, &doc, &warnings, &error));
    CHECK(error.isEmpty() && !warnings.isEmpty() && doc.nodes.size() == 4);
    const Node root = doc.nodes.value(doc.root);
    CHECK(root.text == QStringLiteral("项目 & 🌿\n 第二行 "));
    CHECK(root.children.size() == 2);
    CHECK(doc.nodes.value(root.children[0]).text == QStringLiteral("第二分支"));
    CHECK(doc.nodes.value(root.children[1]).text.isEmpty());
    CHECK(doc.nodes.value(doc.nodes.value(root.children[0]).children[0]).text == QStringLiteral("细节"));
    Document reopened;
    CHECK(reopened.parse(doc.serialize(), &error) && reopened.serialize() == doc.serialize());
    QFile source(path); CHECK(source.open(QIODevice::ReadOnly) && source.readAll() == basicZip); source.close();
    const QByteArray saved = doc.serialize();
    const QStringList savedWarnings = warnings;
    const auto rejects = [&](const QByteArray &bytes, const QString &target) {
        if (!write(target, bytes)) return false;
        return !importMindMap(target, &doc, &warnings, &error) && !error.isEmpty()
            && doc.serialize() == saved && warnings == savedWarnings;
    };
    // Stored payloads, alternate namespace prefixes and data descriptors.
    for (quint16 method : {quint16(0), quint16(8)}) {
        CHECK(write(path, mmap(basicXml, method, 8)));
        CHECK(importMindMap(path, &reopened, nullptr, &error) && reopened.serialize() == saved);
    }
    QByteArray withOmissions = basicXml;
    withOmissions.replace("</m:OneTopic>", "<m:NotesGroup/><m:OneImage/><m:Hyperlink/><m:Relationship/><m:Boundary/><m:FloatingTopics/><m:Task/><m:Custom/></m:OneTopic>");
    CHECK(write(path, mmap(withOmissions)));
    QStringList omissionWarnings;
    CHECK(importMindMap(path, &reopened, &omissionWarnings, &error));
    CHECK(omissionWarnings.join('\n').contains(QStringLiteral("备注")));
    CHECK(omissionWarnings.join('\n').contains(QStringLiteral("浮动主题")));
    CHECK(omissionWarnings.join('\n').contains(QStringLiteral("超链接")));
    CHECK(omissionWarnings.join('\n').contains(QStringLiteral("图片")));

    // ZIP safety, malformed directories, flags and bounded expansion.
    CHECK(rejects(QByteArray(), path));
    CHECK(rejects(basicZip.left(basicZip.size() - 1), path));
    CHECK(rejects(basicZip + "junk", path));
    CHECK(rejects(zip({Part("other.xml", basicXml)}), path));
    CHECK(rejects(zip({Part("Document.xml", basicXml), Part("document.XML", basicXml)}), path));
    CHECK(rejects(zip({Part("Document.xml", basicXml), Part("Document.xml", basicXml)}), path));
    for (const QByteArray &badName : {QByteArray("../escape"), QByteArray("/absolute"), QByteArray("a/../escape"),
             QByteArray("a\\..\\escape"), QByteArray("C:/escape"), QByteArray("a//escape"), QByteArray("./escape")})
        CHECK(rejects(zip({Part("Document.xml", basicXml), Part(badName, "ignored")}), path));
    CHECK(rejects(mmap(basicXml, 0, 1), path));
    CHECK(error.contains(QStringLiteral("加密")));
    CHECK(rejects(mmap(basicXml, 0, 0x20), path));
    CHECK(rejects(mmap(basicXml, 99), path));
    QByteArray changed = mmap(basicXml);
    changed.data()[30 + int(QByteArray("Document.xml").size())] ^= 1;
    CHECK(rejects(changed, path));
    int central = basicZip.indexOf(QByteArray("PK\1\2", 4));
    CHECK(central > 0);
    changed = basicZip; set32(changed, central + 24, Document::maxBytes + 1);
    CHECK(rejects(changed, path));
    changed = basicZip; set32(changed, central + 24, quint32(basicXml.size() - 1)); set32(changed, 22, quint32(basicXml.size() - 1));
    CHECK(rejects(changed, path));
    changed = basicZip; set32(changed, central + 42, 0xffffffffu);
    CHECK(rejects(changed, path));
    changed = basicZip; set16(changed, 8, 0);
    CHECK(rejects(changed, path));
    changed = basicZip; set16(changed, central + 34, 1);
    CHECK(rejects(changed, path));
    changed = basicZip; set32(changed, changed.size() - 6, 0xffffffffu);
    CHECK(rejects(changed, path));
    changed = zip({Part("Document.xml", basicXml), Part("extra", "ignored")});
    const int secondCentral = changed.indexOf(QByteArray("PK\1\2", 4), changed.indexOf(QByteArray("PK\1\2", 4)) + 4);
    set32(changed, secondCentral + 42, 0); // overlapping/mismatched local entry
    CHECK(rejects(changed, path));
    CHECK(rejects(basicZip, dir.filePath("wrong.smap")));
    CHECK(!importMindMap(dir.filePath("missing.mmap"), &doc, &warnings, &error));
    CHECK(doc.serialize() == saved && warnings == savedWarnings);
    CHECK(!importMindMap(path, nullptr, &warnings, &error));
    QFile huge(dir.filePath("large.mmap")); CHECK(huge.open(QIODevice::WriteOnly));
    CHECK(huge.resize(Document::maxBytes + 1)); huge.close();
    CHECK(!importMindMap(huge.fileName(), &doc, &warnings, &error));
    CHECK(doc.serialize() == saved && warnings == savedWarnings);

    // XML is untrusted: no DTD, namespaces/IDs/text must match known format.
    CHECK(rejects(mmap(basicXml.left(basicXml.size() - 10)), path));
    changed = basicXml; changed.replace("<m:Map", "<!DOCTYPE m:Map [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><m:Map");
    CHECK(rejects(mmap(changed), path));
    changed = basicXml; changed.replace("<m:Map", "<!DOCTYPE m:Map [<!ENTITY x 'expanded'>]><m:Map");
    CHECK(rejects(mmap(changed), path));
    changed = basicXml; changed.replace("Application/2003", "Application/2999");
    CHECK(rejects(mmap(changed), path));
    changed = basicXml; changed.replace("<m:OneTopic>", "<m:OneTopic xmlns:m=\"urn:spoof\">");
    CHECK(rejects(mmap(changed), path));
    CHECK(rejects(mmap(map(topic("a", "root", topic("a", "duplicate")))), path));
    CHECK(rejects(mmap(map(topic("", "no ID"))), path));
    CHECK(rejects(mmap(map(topic("a", "one") + topic("b", "two"))), path));
    CHECK(rejects(mmap(map("<m:Topic OId=\"a\"/>")), path));
    CHECK(rejects(mmap(map("<m:Topic OId=\"a\"><m:Text/><m:Text PlainText=\"a\"/></m:Topic>")), path));
    CHECK(rejects(mmap(map("<m:Topic OId=\"a\"><m:Text PlainText=\"a\"/><m:Text PlainText=\"b\"/></m:Topic>")), path));
    CHECK(rejects(mmap(map(topic("a", QByteArray(Document::maxText + 1, 'x')))), path));
    changed = topic("last", "last");
    for (int i = 0; i < Document::maxDepth; ++i) changed = topic(QByteArray::number(i), "x", changed);
    CHECK(write(path, mmap(map(changed), 8)));
    CHECK(importMindMap(path, &reopened, nullptr, &error) && reopened.nodes.size() == Document::maxDepth + 1);
    CHECK(rejects(mmap(map(topic("too-deep", "x", changed)), 8), path));
    changed = basicXml; changed.replace("🌿", QByteArray("\xc0\xaf", 2));
    CHECK(rejects(mmap(changed), path));

    // Legacy EMMX: graph ordering comes from SubLevel, not flat Shape order.
    const QByteArray rootShape = shape("r", "MainIdea", "<pp><tp>项目 </tp><tp>🌿</tp></pp><pp><tp>下一行</tp></pp>", "", "b;a");
    const QByteArray aShape = shape("a", "MainTopic", "<pp><tp>甲</tp></pp>", "r", "");
    const QByteArray bShape = shape("b", "SubTopic", "<pp><tp>乙 &amp; 二</tp></pp>", "r", "");
    const QByteArray emmxShapes = aShape + rootShape + "<Shape ID=\"line\" Type=\"MMConnector\"/>" + bShape;
    const QByteArray emmxZip = emmx(emmxShapes);
    CHECK(write(emmxPath, emmxZip));
    CHECK(importMindMap(emmxPath, &reopened, &omissionWarnings, &error));
    CHECK(reopened.nodes.size() == 3 && reopened.root == "r");
    CHECK(reopened.nodes.value("r").children == (QStringList{"b", "a"}));
    CHECK(reopened.nodes.value("r").text == QStringLiteral("项目 🌿\n下一行"));
    CHECK(reopened.nodes.value("b").text == QStringLiteral("乙 & 二"));
    changed = emmxShapes; changed.replace("<SubLevel V=\"b;a\"/>", "");
    CHECK(write(emmxPath, emmx(changed)));
    CHECK(importMindMap(emmxPath, &reopened, &omissionWarnings, &error));
    CHECK(reopened.nodes.value("r").children == (QStringList{"a", "b"}));
    CHECK(omissionWarnings.join('\n').contains(QStringLiteral("出现顺序")));
    CHECK(rejects(emmx(emmxShapes + aShape), emmxPath));
    CHECK(rejects(emmx(emmxShapes + shape("r2", "MainIdea", "<pp/>", "", "")), emmxPath));
    changed = emmxShapes; changed.replace("<Super V=\"r\"/>", "<Super V=\"missing\"/>");
    CHECK(rejects(emmx(changed), emmxPath));
    changed = emmxShapes; changed.replace("<SubLevel V=\"b;a\"/>", "<SubLevel V=\"a;a\"/>");
    CHECK(rejects(emmx(changed), emmxPath));
    changed = emmxShapes; changed.replace("<SubLevel V=\"b;a\"/>", "<SubLevel V=\"a\"/>");
    CHECK(rejects(emmx(changed), emmxPath));
    changed = emmxShapes; changed.replace("<Super V=\"r\"/>", "<Super V=\"r\"/><Super V=\"r\"/>");
    CHECK(rejects(emmx(changed), emmxPath));
    changed = emmxShapes; changed.replace("<SubLevel V=\"b;a\"/>", "<SubLevel V=\"b;a\"/><SubLevel V=\"b;a\"/>");
    CHECK(rejects(emmx(changed), emmxPath));
    CHECK(rejects(emmx(shape("r", "MainIdea", "", "", "")), emmxPath));
    CHECK(rejects(emmx(shape("r", "MainIdea", "<pp/>", "", "")
        + shape("a", "MainTopic", "<pp/>", "b", "b") + shape("b", "MainTopic", "<pp/>", "a", "a")), emmxPath));
    CHECK(rejects(zip({Part("document.xml", "<Document/>"), Part("mmpage/page.bin", "binary")}), emmxPath));
    CHECK(error.contains(QStringLiteral("二进制")));
    CHECK(rejects(zip({Part("document.xml", "<Document/>"), Part("page/a.xml", "<Page/>"), Part("page/b.xml", "<Page/>")}), emmxPath));
    CHECK(error.contains(QStringLiteral("多页")));
    CHECK(rejects(zip({Part("page/a.xml", "<Page/>" )}), emmxPath));
    CHECK(rejects(zip({Part("document.xml", "<Document/>"), Part("page/a.xml", "<!DOCTYPE Page [<!ENTITY x 'x'>]><Page/>")}), emmxPath));

    // Deterministic corruption smoke test, useful under ASan/UBSan too.
    quint32 random = 0xace12345u;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        random = random * 1664525u + 1013904223u;
        changed = basicZip; changed.data()[int(random % quint32(changed.size()))] ^= char((random >> 24) | 1);
        CHECK(write(path, changed));
        Document candidate = doc; QStringList candidateWarnings = warnings;
        if (!importMindMap(path, &candidate, &candidateWarnings, &error)) {
            CHECK(!error.isEmpty() && candidate.serialize() == saved && candidateWarnings == savedWarnings);
        } else CHECK(reopened.parse(candidate.serialize(), &error));
    }
    // Optional fixture output for the UI integration smoke test.
    if (argc == 2) {
        QDir output(QString::fromLocal8Bit(argv[1])); CHECK(QDir().mkpath(output.absolutePath()));
        CHECK(write(output.filePath("synthetic.mmap"), basicZip));
        CHECK(write(output.filePath("synthetic-legacy.emmx"), emmxZip));
    }
    qInfo() << "Importer synthetic tests passed";
    return 0;
#undef CHECK
}
