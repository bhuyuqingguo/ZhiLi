#include <QtTest/QTest>

int runSmokeTest() {
#define CHECK(condition) do { if (!(condition)) { qCritical() << __LINE__ << #condition; return 1; } } while (false)
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir temp;
    CHECK(temp.isValid());
    Window window(false);
    window.show();
    window.canvas->setFocus();
    QTest::qWait(60);
    const QString root = window.doc.root;
    const QPoint center = window.canvas->mapFromScene(QPointF(0, 0));
    QTest::mouseDClick(window.canvas->viewport(), Qt::LeftButton, Qt::NoModifier, center);
    CHECK(window.editor);
    window.editor->setPlainText(QStringLiteral("研发项目交付"));
    QTest::keyClick(window.editor, Qt::Key_Return);
    CHECK(window.editor && window.editor->toPlainText().contains('\n'));
    window.editor->clear();
    QInputMethodEvent preedit(QStringLiteral("yanfa"), {});
    QCoreApplication::sendEvent(window.editor, &preedit);
    CHECK(window.editor->toPlainText().isEmpty());
    QInputMethodEvent commit;
    commit.setCommitString(QStringLiteral("研发项目交付"));
    QCoreApplication::sendEvent(window.editor, &commit);
    QTest::keyClick(window.editor, Qt::Key_Return, Qt::ControlModifier);
    CHECK(!window.editor && window.doc.nodes.value(root).text == QStringLiteral("研发项目交付"));

    auto add = [&](const QString &parent, const QString &text) {
        window.selectNode(parent);
        window.createNode(true);
        window.editor->setPlainText(text);
        window.finishEdit();
        return window.selected;
    };
    QTest::keyClick(window.canvas, Qt::Key_Tab);
    CHECK(window.editor);
    const QString first = window.selected;
    window.editor->setPlainText(QStringLiteral("项目规划"));
    window.finishEdit();
    QTest::keyClick(window.canvas, Qt::Key_Return);
    CHECK(window.editor);
    const QString second = window.selected;
    window.editor->setPlainText(QStringLiteral("技术验证"));
    window.finishEdit();
    const QString child = add(first, QStringLiteral("梳理需求与验收指标"));
    add(first, QStringLiteral("明确节点与责任分工"));
    add(second, QStringLiteral("运行环境与输入法"));
    add(second, QStringLiteral("数据安全与离线验证"));
    CHECK(window.doc.nodes.size() == 7);

    window.selectNode(first);
    window.toggleCollapse();
    CHECK(!window.items.contains(child));
    window.chooseLayout(false);
    CHECK(!window.items.contains(child));
    window.toggleCollapse();
    CHECK(window.items.contains(child));
    window.fit();
    const QPoint childPoint = window.canvas->mapFromScene(window.items.value(child).body->pos());
    const QPoint targetPoint = window.canvas->mapFromScene(window.items.value(second).body->pos());
    QTest::mousePress(window.canvas->viewport(), Qt::LeftButton, Qt::NoModifier, childPoint);
    QTest::mouseRelease(window.canvas->viewport(), Qt::LeftButton, Qt::NoModifier, targetPoint);
    CHECK(window.parents.value(child) == second);
    window.moveNode(second, child, 0);
    CHECK(window.parents.value(second) == root);
    window.removeSelected();
    CHECK(!window.doc.nodes.contains(child));
    window.history(true);
    CHECK(window.parents.value(child) == second);
    window.history(true);
    CHECK(window.parents.value(child) == first);
    window.history(false);
    CHECK(window.parents.value(child) == second);
    window.history(true);

    const QByteArray structure = window.doc.serialize();
    window.chooseLayout(true);
    CHECK(window.dirty && window.doc.serialize() != structure);
    window.chooseLayout(false);
    CHECK(window.doc.serialize() == structure);

    const QString path = temp.filePath(QStringLiteral("中文 项目.smap"));
    CHECK(window.saveTo(path));
    CHECK(!window.dirty);
    window.chooseLayout(true);
    window.history(true);
    CHECK(!window.dirty);
    QString error;
    CHECK(window.loadFrom(path, &error) && window.doc.serialize() == structure);
    const QString legacy = temp.filePath("legacy.smap.json");
    CHECK(QFile::copy(path, legacy) && window.loadFrom(legacy, &error));
    Document invalid = window.doc;
    invalid.nodes[child].children.append(root);
    CHECK(!invalid.parse(invalid.serialize(), &error));
    CHECK(invalid.nodes.value(child).children.contains(root));
    invalid = window.doc;
    invalid.nodes[root].children.append(first);
    CHECK(!invalid.parse(invalid.serialize(), &error));

    window.recoveryPath = temp.filePath("recovery.json");
    window.startEdit();
    window.editor->setPlainText(QStringLiteral("编辑中的草稿"));
    window.writeRecovery();
    QFile recovery(window.recoveryPath);
    CHECK(recovery.open(QIODevice::ReadOnly));
    auto envelope = QJsonDocument::fromJson(recovery.readAll()).object();
    Document recovered;
    CHECK(recovered.parse(QJsonDocument(envelope.value("document").toObject()).toJson()));
    CHECK(recovered.nodes.value(root).text == QStringLiteral("编辑中的草稿"));
    recovery.close();
    const QString recoveryDir = temp.filePath("recovery-check");
    CHECK(QDir().mkpath(recoveryDir));
    CHECK(QFile::copy(window.recoveryPath, QDir(recoveryDir).filePath("recovery.json")));
    {
        Window restarted(false);
        QTimer::singleShot(0, &restarted, [&restarted] {
            auto *dialog = restarted.findChild<QMessageBox *>();
            if (dialog) dialog->button(QMessageBox::Yes)->click();
        });
        restarted.setupRecovery(recoveryDir);
        CHECK(restarted.dirty && restarted.needsSaveAs && restarted.filePath != legacy);
        CHECK(restarted.doc.serialize() == recovered.serialize());
    }
    window.finishEdit(false);
    CHECK(!QFile::exists(window.recoveryPath));
    CHECK(window.doc.serialize() == structure);

    for (const QString &format : {QString("png"), QString("svg"), QString("pdf")}) {
        const QString target = temp.filePath("output." + format);
        error.clear();
        CHECK(window.exportTo(target, format, &error));
        QFile output(target);
        CHECK(output.open(QIODevice::ReadOnly) && output.size() > 100);
        const QByteArray header = output.read(128);
        CHECK(format == "pdf" ? header.startsWith("%PDF") :
            format == "png" ? header.startsWith("\x89PNG") : header.contains("<svg"));
    }

    const QString previewDir = qEnvironmentVariable("ZHILI_PREVIEW_DIR");
    if (!previewDir.isEmpty()) {
        QDir().mkpath(previewDir);
        CHECK(writeAtomically(QDir(previewDir).filePath(QStringLiteral("研发项目交付.smap")), structure, &error));
        window.fit();
        QTest::qWait(100);
        CHECK(window.grab().save(QDir(previewDir).filePath("hierarchy.png")));
        window.chooseLayout(true);
        window.fit();
        QTest::qWait(100);
        CHECK(window.grab().save(QDir(previewDir).filePath("mindmap.png")));
    }

    for (int count : {100, 500, 1000, 3000}) {
        Document large;
        large.root = "0";
        for (int i = 0; i < count; ++i) {
            Node node;
            node.id = QString::number(i);
            node.text = QStringLiteral("测试主题 %1").arg(i);
            for (int childIndex = i * 4 + 1; childIndex <= i * 4 + 4 && childIndex < count; ++childIndex)
                node.children.append(QString::number(childIndex));
            large.nodes.insert(node.id, node);
        }
        const QString fixture = temp.filePath("large.smap");
        CHECK(writeAtomically(fixture, large.serialize(), &error));
        QElapsedTimer timer;
        timer.start();
        CHECK(window.loadFrom(fixture, &error) && window.items.size() == count);
        const auto openMs = timer.restart();
        window.chooseLayout(false);
        qInfo().nospace() << count << " nodes: open+render " << openMs << " ms, layout switch " << timer.elapsed() << " ms";
        CHECK(window.doc.nodes.size() == count && window.items.size() == count);
    }
    qInfo() << "ZhiLi smoke test passed";
    return 0;
#undef CHECK
}
