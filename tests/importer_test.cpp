#include "../src/importer.h"
#include "../src/emmx_binary.h"
#include "../src/exchange.h"
#include "../src/pptx.h"

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

QByteArray protoBytes(int tag, const QByteArray &data) {
    QByteArray bytes(1, char(tag * 8 + 2));
    int size = data.size();
    while (size >= 128) {
        bytes.append(char((size & 127) | 128));
        size >>= 7;
    }
    bytes.append(char(size));
    return bytes + data;
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
    const QByteArray binaryMap = QByteArray::fromHex(
        "01016e030165021b050601040104526f6f74000000000e026768000168021c0865000167021c08650000");
    CHECK(readBinaryEmmxPage(binaryMap, &reopened, &error));
    CHECK(reopened.nodes.value(reopened.root).text == "Root");
    CHECK(reopened.nodes.value(reopened.root).children == (QStringList{"103", "104"}));
    CHECK(!readBinaryEmmxPage(binaryMap.left(binaryMap.size() - 5), &reopened, &error));

    const QString meisterPath = dir.filePath("test.mind");
    CHECK(write(meisterPath, zip({Part("map.json", "{\"root\":{\"title\":\"计划\",\"children\":["
        "{\"title\":\"第一项\"},{\"title\":\"第二项\",\"children\":[]}]}}", 8)})));
    CHECK(importMindMap(meisterPath, &reopened, nullptr, &error));
    CHECK(reopened.nodes.size() == 3 && reopened.nodes.value(reopened.root).text == QStringLiteral("计划"));
    CHECK(reopened.nodes.value(reopened.nodes.value(reopened.root).children.first()).text == QStringLiteral("第一项"));
    CHECK(rejects(zip({Part("map.json", "{\"root\":{\"title\":4}}")}), meisterPath));

    const QString mindomoPath = dir.filePath("test.pmom");
    CHECK(write(mindomoPath, zip({Part("nodes.json", "[{\"id\":\"child2\",\"title\":\"乙\",\"parentId\":\"root\",\"index\":2},"
        "{\"id\":\"root\",\"title\":\"计划\",\"style\":{\"expanded\":false}},"
        "{\"id\":\"child1\",\"title\":\"甲\",\"parentId\":\"root\",\"index\":1},"
        "{\"id\":\"hidden\",\"title\":\"模板\",\"isTemplate\":1}]"), Part("attachments/payload.bin", "ignored")})));
    CHECK(importMindMap(mindomoPath, &reopened, nullptr, &error));
    CHECK(reopened.nodes.size() == 3 && reopened.nodes.value(reopened.root).collapsed);
    CHECK(reopened.nodes.value(reopened.nodes.value(reopened.root).children.first()).text == QStringLiteral("甲"));

    const QByteArray docId(16, 'd'), rootId(16, 'r'), childId(16, 'c');
    const auto span = [](const QByteArray &label) { return protoBytes(1, protoBytes(4, protoBytes(1, label))); };
    const QByteArray rootNode = protoBytes(1, rootId) + protoBytes(2, protoBytes(11, span("Ro") + span("ot\nLabel")));
    const QByteArray childNode = protoBytes(1, childId) + protoBytes(2, protoBytes(11, span("孩子")));
    const QByteArray relations = protoBytes(1, docId)
        + protoBytes(2, protoBytes(1, rootId) + protoBytes(2, docId))
        + protoBytes(2, protoBytes(1, childId) + protoBytes(2, rootId));
    const QByteArray nextDoc = protoBytes(2, rootNode) + protoBytes(2, childNode) + protoBytes(3, relations);
    const QByteArray nextFile = protoBytes(1, protoBytes(1, docId) + protoBytes(2, protoBytes(1, nextDoc)));
    const QString nextPath = dir.filePath("next.mindnode");
    CHECK(write(nextPath, zip({Part("Metadata.json", "{\"fileFormatVersion\":1,\"minFileFormatVersionForReading\":1}"),
                               Part("Contents.data", nextFile)})));
    CHECK(importMindMap(nextPath, &reopened, nullptr, &error));
    CHECK(reopened.nodes.size() == 2 && reopened.nodes.value(reopened.root).text == "Root\nLabel");
    CHECK(reopened.nodes.value(reopened.nodes.value(reopened.root).children.first()).text == QStringLiteral("孩子"));
    CHECK(rejects(zip({Part("page/a.xml", "<Page/>" )}), emmxPath));
    CHECK(rejects(zip({Part("document.xml", "<Document/>"), Part("page/a.xml", "<!DOCTYPE Page [<!ENTITY x 'x'>]><Page/>")}), emmxPath));

    const QString opmlPath = dir.filePath("迁移.大纲.OPML"), mmPath = dir.filePath("freeplane.mm");
    const QByteArray outline = "<?xml version='1.0' encoding='UTF-8'?>"
        "<opml version='2.0' xmlns:ext='urn:extension'><head><title> 项目 &amp; 🌿 </title></head><body>"
        "<outline text='甲&#10;第二行'><outline text='叶子'/></outline>"
        "<ext:outline text='扩展'><outline text='忽略'/></ext:outline>"
        "<outline text='&lt;b&gt;乙&lt;/b&gt;' type='include' url='https://example.org/map.opml'/></body></opml>";
    CHECK(write(opmlPath, outline));
    CHECK(importMindMap(opmlPath, &reopened, &omissionWarnings, &error));
    const Node opmlRoot = reopened.nodes.value(reopened.root);
    CHECK(reopened.nodes.size() == 4 && opmlRoot.text == QStringLiteral("项目 & 🌿") && opmlRoot.children.size() == 2);
    const Node firstEntry = reopened.nodes.value(opmlRoot.children[0]);
    CHECK(firstEntry.text == QStringLiteral("甲\n第二行") && firstEntry.children.size() == 1);
    CHECK(reopened.nodes.value(firstEntry.children[0]).text == QStringLiteral("叶子"));
    CHECK(reopened.nodes.value(opmlRoot.children[1]).text == QStringLiteral("<b>乙</b>"));
    CHECK(omissionWarnings.join('\n').contains(QStringLiteral("链接")));
    for (const QByteArray &version : {QByteArray("1.0"), QByteArray("1.1")}) {
        CHECK(write(opmlPath, "<opml version='" + version + "'><body><outline text=''/></body></opml>"));
        CHECK(importMindMap(opmlPath, &reopened, nullptr, &error));
        CHECK(reopened.nodes.size() == 2 && reopened.nodes.value(reopened.root).text == QStringLiteral("迁移.大纲"));
    }
    CHECK(rejects(outline.left(outline.size() - 1), opmlPath));
    CHECK(rejects("<!DOCTYPE opml SYSTEM 'file:///private'><opml version='2.0'><body><outline text='x'/></body></opml>", opmlPath));
    CHECK(rejects("<opml version='2.0'><body><outline/></body></opml>", opmlPath));
    CHECK(rejects("<opml version='2.0'><body><outline text='x'/></body><body/></opml>", opmlPath));
    CHECK(rejects("<opml version='2.0'><body/></opml>", opmlPath));

    const QByteArray freeplane = "<map version='freeplane 1.12.15'>"
        "<node TEXT='中心 🌿' ID='ID_1'><hook><node TEXT='忽略的样式'/></hook>"
        "<node ID='ID_2'><richcontent TYPE='NODE'><html><head><title>忽略的页头</title></head><body>"
        "\n  <p>甲 <b>&amp; 乙</b></p>\n  <p>第三行<br/>第四行</p>\n</body></html></richcontent>"
        "<richcontent TYPE='NOTE'><html><body>忽略的备注</body></html></richcontent><node TEXT='叶子'/></node>"
        "<node TEXT='第二分支'/></node></map>";
    CHECK(write(mmPath, freeplane));
    CHECK(importMindMap(mmPath, &reopened, &omissionWarnings, &error));
    const Node mmRoot = reopened.nodes.value(reopened.root);
    CHECK(reopened.nodes.size() == 4 && mmRoot.text == QStringLiteral("中心 🌿") && mmRoot.children.size() == 2);
    const Node richTopic = reopened.nodes.value(mmRoot.children[0]);
    CHECK(richTopic.text == QStringLiteral("甲 & 乙\n第三行\n第四行") && richTopic.children.size() == 1);
    CHECK(reopened.nodes.value(richTopic.children[0]).text == QStringLiteral("叶子"));
    CHECK(reopened.nodes.value(mmRoot.children[1]).text == QStringLiteral("第二分支"));
    CHECK(omissionWarnings.join('\n').contains(QStringLiteral("备注")));
    CHECK(write(mmPath, "<map version='1.0.1'><node TEXT='FreeMind'><node TEXT='下一级'/></node></map>"));
    CHECK(importMindMap(mmPath, &reopened, nullptr, &error) && reopened.nodes.size() == 2);
    CHECK(write(mmPath, "<map version='freeplane 1.5.6'><node TEXT='&lt;html&gt;&lt;head/&gt;&lt;body&gt;"
                        "&lt;b&gt;富文本&lt;/b&gt;&lt;br&gt;第二行 &amp;#160; &lt;/body&gt;&lt;/html&gt;'/></map>"));
    CHECK(importMindMap(mmPath, &reopened, nullptr, &error));
    CHECK(reopened.nodes.value(reopened.root).text == QStringLiteral("富文本\n第二行"));
    CHECK(rejects(freeplane.left(freeplane.size() - 10), mmPath));
    CHECK(rejects("<!DOCTYPE map [<!ENTITY x SYSTEM 'file:///private'>]><map><node TEXT='&x;'/></map>", mmPath));
    CHECK(rejects("<map><node TEXT='one'/><node TEXT='two'/></map>", mmPath));
    CHECK(rejects("<map><node TEXT='locked' ENCRYPTED_CONTENT='secret'/></map>", mmPath));
    CHECK(rejects("<map><node TEXT='" + QByteArray(Document::maxText + 1, 'x') + "'/></map>", mmPath));
    changed = "<node TEXT='leaf'/>";
    for (int i = 0; i < Document::maxDepth; ++i) changed = "<node TEXT='x'>" + changed + "</node>";
    CHECK(write(mmPath, "<map>" + changed + "</map>"));
    CHECK(importMindMap(mmPath, &reopened, nullptr, &error) && reopened.nodes.size() == Document::maxDepth + 1);
    CHECK(rejects("<map><node TEXT='too deep'>" + changed + "</node></map>", mmPath));

    const QByteArray graph = "<mxGraphModel><root><mxCell id='0'/><mxCell id='1' parent='0'/>"
        "<object id='r' label='中心 &amp; 计划'><mxCell vertex='1' parent='1'/></object>"
        "<mxCell id='b' vertex='1' value='&lt;b&gt;第二行&lt;/b&gt;&lt;br&gt;更多' style='html=1;' parent='1'/>"
        "<mxCell id='a' vertex='1' value='三' parent='1'/>"
        "<mxCell id='e1' edge='1' source='r' target='b'/>"
        "<mxCell id='e2' edge='1' source='r' target='a'/>"
        "<mxCell id='e3' edge='1' source='a' target='r'/></root></mxGraphModel>";
    const QString drawioPath = dir.filePath("graph.drawio");
    CHECK(write(drawioPath, graph));
    CHECK(importMindMap(drawioPath, &reopened, &omissionWarnings, &error));
    CHECK(reopened.nodes.size() == 3 && reopened.nodes.value(reopened.root).text == QStringLiteral("中心 & 计划"));
    CHECK(reopened.nodes.value(reopened.nodes.value(reopened.root).children.first()).text == QStringLiteral("第二行\n更多"));
    CHECK(omissionWarnings.join('\n').contains(QStringLiteral("1 条连线")));
    const QByteArray compressed = qCompress(QUrl::toPercentEncoding(QString::fromUtf8(graph)), 9);
    const QByteArray encoded = compressed.mid(6, compressed.size() - 10).toBase64();
    const QByteArray multiple = "<mxfile><diagram name='一'>" + graph
        + "</diagram><diagram name='二'>" + encoded + "</diagram></mxfile>";
    CHECK(write(drawioPath, multiple));
    CHECK(importMindMap(drawioPath, &reopened, &omissionWarnings, &error));
    CHECK(reopened.nodes.size() == 9 && reopened.nodes.value(reopened.root).children.size() == 2);
    CHECK(reopened.nodes.value(reopened.nodes.value(reopened.root).children.last()).text == QStringLiteral("二"));
    CHECK(rejects("<mxfile><diagram>broken</diagram></mxfile>", drawioPath));
    CHECK(rejects("<!DOCTYPE mxGraphModel>" + graph, drawioPath));

    const QString xmindPath = dir.filePath("sample.xmind");
    CHECK(write(xmindPath, zip({Part("content.json",
        "[{\"rootTopic\":{\"title\":\"计划\",\"children\":{\"attached\":[{\"title\":\"任务\"}]}}},"
        "{\"rootTopic\":{\"title\":\"记录\"}}]", 8)})));
    CHECK(importMindMap(xmindPath, &reopened, &omissionWarnings, &error));
    CHECK(reopened.nodes.size() == 4 && reopened.nodes.value(reopened.root).children.size() == 2);
    CHECK(reopened.nodes.value(reopened.nodes.value(reopened.root).children.last()).text == QStringLiteral("记录"));
    CHECK(write(xmindPath, zip({Part("content.xml",
        "<xmap-content xmlns='urn:xmind:xmap:xmlns:content:2.0'><sheet><title>旧工作表</title>"
        "<topic id='r'><title>旧导图</title><children><topics type='attached'>"
        "<topic id='c'><title>下一级</title></topic></topics></children></topic></sheet></xmap-content>", 8)})));
    CHECK(importMindMap(xmindPath, &reopened, nullptr, &error));
    CHECK(reopened.nodes.size() == 2 && reopened.nodes.value(reopened.root).text == QStringLiteral("旧导图"));
    CHECK(reopened.nodes.value(reopened.nodes.value(reopened.root).children.first()).text == QStringLiteral("下一级"));

    const QString markdownPath = dir.filePath("outline.md"), plainPath = dir.filePath("outline.txt");
    const QString mermaidPath = dir.filePath("diagram.mmd");
    CHECK(write(markdownPath, "\xef\xbb\xbf# 计划\r\n## 研究\r\n- 甲\r\n  1. 甲的子项\r\n- 乙\r\n\n"
                             "```cpp\n# ignored\n```\n## 验证\n"));
    CHECK(importMindMap(markdownPath, &reopened, nullptr, &error));
    CHECK(reopened.nodes.size() == 6 && reopened.nodes.value(reopened.root).text == QStringLiteral("计划"));
    const auto research = reopened.nodes.value(reopened.nodes.value(reopened.root).children.first());
    CHECK(research.children.size() == 2 && reopened.nodes.value(research.children.first()).children.size() == 1);
    CHECK(write(plainPath, "中心\n\t下级\n\t\t细节\n\t第二项\n"));
    CHECK(importMindMap(plainPath, &reopened, nullptr, &error));
    CHECK(reopened.nodes.size() == 4 && reopened.nodes.value(reopened.root).text == QStringLiteral("中心"));
    CHECK(rejects(QByteArray("\xff", 1), plainPath));
    const QByteArray mermaid = "mindmap\n  root((中心))\n    next[后续工作]\n      note(\"细节\")\n"
        "      ::icon(fa fa-book)\n    other{{另一项}}\n";
    CHECK(write(mermaidPath, mermaid));
    CHECK(importMindMap(mermaidPath, &reopened, nullptr, &error));
    CHECK(reopened.nodes.size() == 4 && reopened.nodes.value(reopened.root).text == QStringLiteral("中心"));
    CHECK(reopened.nodes.value(reopened.nodes.value(reopened.root).children.first()).text == QStringLiteral("后续工作"));
    CHECK(write(markdownPath, "# 标题\n```mermaid\n" + mermaid + "```\n"));
    CHECK(importMindMap(markdownPath, &reopened, nullptr, &error) && reopened.nodes.size() == 4);
    CHECK(rejects("flowchart LR\n A --> B\n", mermaidPath));

    Document exportSource = doc;
    exportSource.nodes[exportSource.root].collapsed = true;
    const auto roundTrip = [&](const QString &format, const QString &output) {
        return exportExchange(exportSource, output, format, &error) &&
            importMindMap(output, &reopened, nullptr, &error) &&
            reopened.nodes.size() == doc.nodes.size() &&
            reopened.nodes.value(reopened.root).text == root.text &&
            reopened.nodes.value(reopened.nodes.value(reopened.root).children.first()).text == QStringLiteral("第二分支");
    };
    for (const auto &format : {QString("drawio"), QString("vsdx"), QString("opml"), QString("mm"), QString("canvas")}) {
        const bool matched = roundTrip(format, dir.filePath("converted." + format));
        if (!matched) qCritical() << format << error << reopened.serialize();
        CHECK(matched);
        if (format == "mm") CHECK(reopened.nodes.value(reopened.root).collapsed);
    }
    const QString pptxPath = dir.filePath("presentation.pptx");
    CHECK(exportPptx(exportSource, pptxPath, &error));
    CHECK(importMindMap(pptxPath, &reopened, &omissionWarnings, &error));
    bool linked = false;
    for (const auto &node : qAsConst(reopened.nodes))
        if (node.text == QStringLiteral("第二分支") && !node.children.isEmpty())
            linked = reopened.nodes.value(node.children.first()).text == QStringLiteral("细节");
    CHECK(linked);

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
        CHECK(write(output.filePath("synthetic.opml"), outline));
        CHECK(write(output.filePath("synthetic.mm"), freeplane));
        CHECK(write(output.filePath("synthetic-multipage.drawio"), multiple));
        for (const auto &format : {QString("drawio"), QString("vsdx"), QString("opml"), QString("mm")})
            CHECK(roundTrip(format, output.filePath("converted." + format)));
        CHECK(exportPptx(exportSource, output.filePath("converted.pptx"), &error));
    }
    qInfo() << "Importer synthetic tests passed";
    return 0;
#undef CHECK
}
