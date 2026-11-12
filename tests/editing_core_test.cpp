#include "../src/editing.h"
#include "../src/importer.h"

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); app.setApplicationName("ZhiliEditingTest"); QStandardPaths::setTestModeEnabled(true);
#define CHECK(x) do { if(!(x)) { qCritical()<<__LINE__<<#x<<error; return 1; } } while(false)
    QString error; Document doc; Node root; root.id="r"; root.text=QStringLiteral("项目"); doc.root="r"; doc.nodes.insert("r",root);
    QStringList inserted;
    CHECK(pasteContent(&doc,"r",{},QStringLiteral("# 计划\n## 开发\n- 编辑\n  - 图片\n## 验证"),PasteContent::Original,true,true,&inserted,&error));
    CHECK(inserted.size()==1 && doc.nodes[inserted.first()].text==QStringLiteral("计划"));
    CHECK(doc.nodes[inserted.first()].children.size()==2 && doc.nodes.size()==6);
    CHECK(pasteContent(&doc,"r",{},QStringLiteral("- 甲\n- 乙"),PasteContent::Markdown,true,true,&inserted,&error));
    CHECK(inserted.size()==2);
    CHECK(pasteContent(&doc,"r",{},QStringLiteral("# 粘贴内容\n- 子项"),PasteContent::Markdown,true,true,&inserted,&error));
    CHECK(inserted.size()==1 && doc.nodes[inserted.first()].text==QStringLiteral("粘贴内容"));
    doc.nodes["r"].notes="keep notes"; doc.nodes["r"].style.italic=true;
    const auto native=BranchClipboard::encode(doc,"r",&error);
    CHECK(pasteContent(&doc,"r",native,"ignored",PasteContent::Original,false,false,&inserted,&error));
    CHECK(doc.nodes[inserted.first()].notes.isEmpty() && !doc.nodes[inserted.first()].style.italic);
    const auto before=doc.serialize();
    CHECK(!pasteContent(&doc,"r","broken","fallback forbidden",PasteContent::Original,true,true,&inserted,&error));
    CHECK(doc.serialize()==before);
    CHECK(pasteContent(&doc,"r",{},"one\ntwo",PasteContent::TextOnly,true,true,&inserted,&error));
    CHECK(inserted.size()==1 && doc.nodes[inserted.first()].text=="one\ntwo");
    const QString invalid(1,QChar(0xd800));
    CHECK(!pasteContent(&doc,"r",{},invalid,PasteContent::TextOnly,true,true,&inserted,&error));
    doc.nodes["r"].collapsed=true;
    FindOptions options; options.hidden=false;
    CHECK(findTopics(doc,QStringLiteral("开发"),options).isEmpty());
    options.hidden=true; CHECK(findTopics(doc,QStringLiteral("开发"),options).size()==2);
    options.root=doc.nodes["r"].children.first(); CHECK(findTopics(doc,QStringLiteral("开发"),options).size()==1);
    options=FindOptions(); options.notes=true;
    int count=0; CHECK(replaceTopics(&doc,{"r","r"},"KEEP","saved",options,false,&count,&error));
    CHECK(count==1 && doc.nodes["r"].notes=="saved notes");
    doc.nodes["r"].text="aaa";
    options.notes=false;
    CHECK(replaceTopics(&doc,{"r"},"a","aa",options,true,&count,&error)); CHECK(count==1 && doc.nodes["r"].text=="aaaa");
    CHECK(replaceTopics(&doc,{"r"},"a","",options,false,&count,&error)); CHECK(count==4 && doc.nodes["r"].text.isEmpty());
    doc.nodes["r"].text="keep";
    const auto unchanged=doc.serialize();
    CHECK(!replaceTopics(&doc,{"r"},"keep",QString(Document::maxText+1,'a'),options,false,&count,&error)); CHECK(doc.serialize()==unchanged);
    CHECK(!replaceTopics(&doc,{"r"},"keep",invalid,options,false,&count,&error)); CHECK(doc.serialize()==unchanged);
    doc.nodes["r"].links=QStringList{QStringLiteral("https://example.com/中文"),"file:///tmp/report.pdf"};
    Document reopened; CHECK(reopened.parse(doc.serialize(),&error)); CHECK(reopened.nodes["r"].links==doc.nodes["r"].links);
    CHECK(QJsonDocument::fromJson(doc.serialize()).object()["version"].toInt()==3);
    doc.nodes["r"].links=QStringList{"javascript:alert(1)"}; CHECK(!reopened.parse(doc.serialize(),&error)); doc.nodes["r"].links.clear();
    QTemporaryDir temp; CHECK(temp.isValid()); const auto path=temp.filePath("test.smap");
    CHECK(LocalHistory::record(path,doc.serialize(),&error)); CHECK(LocalHistory::record(path,doc.serialize(),&error)); CHECK(LocalHistory::versions(path).size()==1);
    for(int i=0;i<34;++i) { doc.nodes["r"].text=QString::number(i); CHECK(LocalHistory::record(path,doc.serialize(),&error)); QThread::msleep(2); }
    CHECK(LocalHistory::versions(path).size()==30); CHECK(reopened.readFile(LocalHistory::versions(path).first(),&error)); CHECK(reopened.nodes["r"].text=="33");
    CHECK(!LocalHistory::record(path,"broken",&error)); CHECK(LocalHistory::versions(path).size()==30);
    const auto directory=LocalHistory::directory(path);
    CHECK(directory.startsWith(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/history/"));
    QDir(directory).removeRecursively();
    qInfo()<<"Selective paste, Markdown, scoped literal replace, media versioning and history retention passed.";
    return 0;
}
