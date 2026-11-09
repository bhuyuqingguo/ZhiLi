#include <QtTest/QTest>

int runSmokeTest() {
#define CHECK(condition) do { if (!(condition)) { qCritical() << __LINE__ << #condition; return 1; } } while (false)
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir temp;
    CHECK(temp.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, temp.path());
    qInfo() << "Smoke platform:" << QApplication::platformName();
    const QStringList embedded = embeddedFontFamilies();
    CHECK(embedded.size() == 2);
    for (const auto &family : embedded) {
        const QRawFont raw = QRawFont::fromFont(fontFor({family, 12, false}));
        CHECK(raw.isValid() && raw.familyName() == family && raw.supportsCharacter(QChar(0x679D)));
    }
    Window window(false);
    window.show();
    window.activateWindow();
    window.canvas->setFocus();
    QTest::qWait(60);
    QScreen *screen = QApplication::primaryScreen();
    CHECK(screen);
    const QSize available = screen->availableGeometry().size();
    CHECK(window.width() <= qMax(available.width(), window.minimumWidth()));
    CHECK(window.height() <= qMax(available.height(), window.minimumHeight()));
    const qreal scale = window.devicePixelRatioF();
    bool scaleSpecified = false;
    const qreal expectedScale = qEnvironmentVariable("ZHILI_EXPECT_SCALE").toDouble(&scaleSpecified);
    qInfo() << "Display:" << available << "logical pixels, device pixel ratio" << scale;
    if (scaleSpecified) CHECK(qAbs(scale - expectedScale) < 0.01);
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
    const QString sibling = add(first, QStringLiteral("明确节点与责任分工"));
    const QString secondMatch = add(second, QStringLiteral("运行环境与输入法"));
    const QString lastSearchMatch = add(second, QStringLiteral("数据安全与离线验证"));
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

    // Reorder by dropping a sibling on the leading edge in the structure view.
    window.fit();
    const auto edgeTarget = window.items.value(child).body->sceneBoundingRect();
    const QPoint beforeChild = window.canvas->mapFromScene(QPointF(
        edgeTarget.left() + edgeTarget.width() * 0.15, edgeTarget.center().y()));
    QTest::mousePress(window.canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                      window.canvas->mapFromScene(window.items.value(sibling).body->pos()));
    QTest::mouseRelease(window.canvas->viewport(), Qt::LeftButton, Qt::NoModifier, beforeChild);
    CHECK(window.doc.nodes.value(first).children == (QStringList{sibling, child}));
    window.history(true);
    CHECK(window.doc.nodes.value(first).children == (QStringList{child, sibling}));

    // Search visits matches in tree order, expands a collapsed path, and wraps.
    window.selectNode(first);
    window.toggleCollapse();
    CHECK(!window.items.contains(child));
    QTimer::singleShot(0, &window, [] {
        if (auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
            dialog->setTextValue(QStringLiteral(" 与 "));
            dialog->accept();
        }
    });
    QTest::keyClick(window.canvas, Qt::Key_F, Qt::ControlModifier);
    CHECK(window.selected == child && window.items.contains(child));
    CHECK(!window.doc.nodes.value(first).collapsed);
    window.activateWindow();
    window.canvas->setFocus();
    QTest::qWait(60);
    QTest::keyClick(window.canvas, Qt::Key_F3);
    CHECK(window.selected == sibling);
    QTest::keyClick(window.canvas, Qt::Key_F3);
    CHECK(window.selected == secondMatch);
    QTest::keyClick(window.canvas, Qt::Key_F3);
    CHECK(window.selected == lastSearchMatch);
    QTest::keyClick(window.canvas, Qt::Key_F3);
    CHECK(window.selected == child);
    window.history(true);
    CHECK(window.doc.nodes.value(first).collapsed && !window.items.contains(child));
    window.history(true);
    CHECK(!window.doc.nodes.value(first).collapsed && window.items.contains(child));

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
    // Representative original 0.1 output, independent of the current serializer.
    const QByteArray original = QStringLiteral(R"JSON({
        "format": "simplemap-json", "version": 1, "root": "root", "layout": "organization",
        "nodes": [
            {"id": "root", "text": "旧版项目", "children": ["b", "a"], "collapsed": false},
            {"id": "a", "text": "另一个分支", "children": [], "collapsed": false},
            {"id": "b", "text": "已折叠分支", "children": ["child"], "collapsed": true},
            {"id": "child", "text": "旧版子级", "children": [], "collapsed": false}
        ]
    })JSON").toUtf8();
    CHECK(writeAtomically(legacy, original, &error));
    CHECK(window.loadFrom(legacy, &error));
    CHECK(!window.doc.mindMap && window.doc.root == "root");
    CHECK(window.doc.nodes.value("root").text == QStringLiteral("旧版项目"));
    CHECK(window.doc.nodes.value("root").children == (QStringList{"b", "a"}));
    CHECK(window.doc.nodes.value("b").collapsed && !window.items.contains("child"));
    CHECK(QFile::remove(legacy));
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
    const auto toolbar = window.findChild<QToolBar *>("wideToolbar");
    const auto compact = window.findChild<QToolBar *>("compactToolbar");
    const auto zoomLevel = window.findChild<QLabel *>("zoomLevel");
    const auto zoomIn = window.findChild<QToolButton *>("zoomIn");
    const auto zoomOut = window.findChild<QToolButton *>("zoomOut");
    CHECK(toolbar && compact && zoomLevel && zoomIn && zoomOut);

    window.resize(1160, 760);
    QTest::qWait(60);
    CHECK(toolbar->isVisible() && !compact->isVisible());
    window.fit();
    const qreal wideZoom = window.canvas->transform().m11();
    window.resize(360, 280);
    QTest::qWait(60);
    CHECK(!toolbar->isVisible() && compact->isVisible());
    QMenu *compactLayouts = nullptr;
    for (auto *button : compact->findChildren<QToolButton *>()) {
        if (button->isVisible() && button->menu() && button->menu()->actions().contains(window.classicAction))
            compactLayouts = button->menu();
    }
    CHECK(compactLayouts && compactLayouts->actions().contains(window.hierarchyAction));
    CHECK(zoomIn->isVisible() && zoomOut->isVisible() && zoomLevel->isVisible());
    qInfo() << "Compact window:" << window.size() << "viewport:" << window.canvas->viewport()->size();
    CHECK(window.canvas->viewport()->width() >= 300 && window.canvas->viewport()->height() >= 120);
    const qreal narrowZoom = window.canvas->transform().m11();
    CHECK(narrowZoom < wideZoom);
    CHECK(zoomLevel->text() == QStringLiteral("%1%").arg(qMax(1, qRound(narrowZoom * 100))));
    const QRectF visibleScene = window.canvas->mapToScene(window.canvas->viewport()->rect()).boundingRect();
    CHECK(visibleScene.adjusted(-1, -1, 1, 1).contains(window.scene->itemsBoundingRect()));
    QTest::mouseClick(zoomIn, Qt::LeftButton);
    CHECK(window.canvas->transform().m11() > narrowZoom);
    QTest::mouseClick(zoomOut, Qt::LeftButton);
    CHECK(qAbs(window.canvas->transform().m11() - narrowZoom) < 0.001);
    QTest::mouseClick(zoomIn, Qt::LeftButton);
    window.activateWindow();
    window.canvas->setFocus();
    QTest::qWait(60);
    QTest::keyClick(window.canvas, Qt::Key_0, Qt::ControlModifier);
    QTest::qWait(60);
    CHECK(window.fitMode);
    CHECK(qAbs(window.canvas->transform().m11() - narrowZoom) < 0.02);
    window.selectNode(root);
    QTest::keyClick(window.canvas, Qt::Key_Down);
    CHECK(window.selected == first);
    QTest::keyClick(window.canvas, Qt::Key_Right);
    CHECK(window.selected == second);
    QTest::keyClick(window.canvas, Qt::Key_Up);
    CHECK(window.selected == root);
    compactLayouts->actions().first()->trigger();
    CHECK(window.doc.mindMap && window.classicAction->isChecked());
    QTest::keyClick(window.canvas, Qt::Key_Right);
    CHECK(window.selected == first);
    QTest::keyClick(window.canvas, Qt::Key_Left);
    CHECK(window.selected == root);
    compactLayouts->actions().last()->trigger();
    CHECK(!window.doc.mindMap && window.hierarchyAction->isChecked());
    QTest::keyClick(window.canvas, Qt::Key_F2);
    CHECK(window.editor && window.editor->isVisible());
    const QRect editRect = window.canvas->mapFromScene(window.editorProxy->sceneBoundingRect()).boundingRect();
    CHECK(window.canvas->viewport()->rect().contains(editRect.center()));
    window.finishEdit(false);

    bool dialogOperated = false;
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("appearanceDialog");
        if (!dialog) return;
        auto *interfaceFamily = dialog->findChild<QComboBox *>("interfaceFamily");
        auto *interfaceSize = dialog->findChild<QSpinBox *>("interfaceSize");
        auto *topicFamily = dialog->findChild<QComboBox *>("topicFamily");
        auto *topicSize = dialog->findChild<QSpinBox *>("topicSize");
        auto *topicWeight = dialog->findChild<QComboBox *>("topicWeight");
        auto *theme = dialog->findChild<QComboBox *>("appearanceTheme");
        if (!interfaceFamily || !interfaceSize || !topicFamily || !topicSize || !topicWeight || !theme) {
            dialog->reject();
            return;
        }
        interfaceFamily->setCurrentIndex(interfaceFamily->findData(embedded.at(1)));
        interfaceSize->setValue(22);
        topicFamily->setCurrentIndex(topicFamily->findData(embedded.at(1)));
        topicSize->setValue(18);
        topicWeight->setCurrentIndex(1);
        theme->setCurrentIndex(1);
        dialogOperated = true;
        dialog->accept();
    });
    const bool wasDirty = window.dirty;
    window.changeAppearance();
    CHECK(dialogOperated);
    QTest::qWait(60);
    CHECK(window.dirty == wasDirty && compact->isVisible());
    int visibleControls = 0;
    for (auto *button : compact->findChildren<QToolButton *>()) {
        if (button->accessibleName().isEmpty()) continue;
        CHECK(button->isVisible() && compact->rect().contains(button->geometry()));
        ++visibleControls;
    }
    CHECK(visibleControls == 4);
    CHECK(QApplication::font().pointSize() == 22 && QApplication::font().family() == embedded.at(1));
    const QFont labelFont = window.items.value(child).label->font();
    CHECK(labelFont.family() == embedded.at(1) && labelFont.pointSize() == 18 && labelFont.bold());
    CHECK(window.scene->backgroundBrush().color() == Qt::white);
    const Appearance savedAppearance = Appearance::load();
    CHECK(savedAppearance.interfaceFont.points == 22 && savedAppearance.topicFont.bold && savedAppearance.highContrast);
    if (!previewDir.isEmpty()) {
        QDir().mkpath(previewDir);
        CHECK(window.grab().save(QDir(previewDir).filePath("compact-large-type.png")));
    }
    window.appearance = Appearance::defaults();
    window.appearance.save();
    window.applyAppearance();
    QTest::qWait(60);

    if (!previewDir.isEmpty()) {
        QDir().mkpath(previewDir);
        CHECK(window.grab().save(QDir(previewDir).filePath("compact.png")));
    }

    window.resize(1160, 760);
    QTest::qWait(60);
    CHECK(toolbar->isVisible() && !compact->isVisible());
    if (!previewDir.isEmpty()) {
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
