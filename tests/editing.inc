#include <QtTest/QTest>

int runEditingTest() {
#define VERIFY_EDIT(condition) do { if (!(condition)) { qCritical() << __LINE__ << #condition; return 1; } } while (false)
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir temporary;
    VERIFY_EDIT(temporary.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, temporary.path());
    Window editing(false);
    const auto reset = [&] {
        editing.finishEdit(false);
        Document doc;
        doc.root = "r";
        doc.mindMap = true;
        doc.branchDirection = BranchDirection::Right;
        for (const QString &id : {QString("r"), QString("a"), QString("b"), QString("c"), QString("d"), QString("x"), QString("y")}) {
            Node node;
            node.id = id;
            node.text = QStringLiteral("主题 ") + id;
            doc.nodes.insert(id, node);
        }
        doc.nodes["r"].children = QStringList{"a", "b", "c", "d"};
        doc.nodes["a"].children = QStringList{"x", "y"};
        doc.nodes["a"].notes = QStringLiteral("备注保留");
        editing.doc = doc;
        editing.initializeDocument();
        editing.selectNode("a");
        editing.canvas->setFocus();
        if (editing.isVisible()) { editing.activateWindow(); QTest::qWait(40); }
    };
    reset();
    editing.showMaximized();
    editing.activateWindow();
    QTest::qWait(100);
    VERIFY_EDIT(editing.isMaximized());
    int completed = 0;
    QPointer<StartupOverlay> startup = new StartupOverlay(&editing, [&] { ++completed; });
    const auto initial = editing.doc.serialize();
    QTest::keyClick(startup, Qt::Key_N, Qt::ControlModifier);
    VERIFY_EDIT(editing.doc.serialize() == initial);
    QTest::keyClick(startup, Qt::Key_Escape);
    VERIFY_EDIT(completed == 1 && !startup->isVisible());
    startup->finish();
    VERIFY_EDIT(completed == 1);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    VERIFY_EDIT(startup.isNull());
    startup = new StartupOverlay(&editing, [&] { ++completed; });
    QTest::qWait(1850);
    VERIFY_EDIT(completed == 2 && startup.isNull());
    editing.canvas->setFocus();

    const QString artifacts = qEnvironmentVariable("ZHILI_SCREENSHOT_DIR");
    if (!artifacts.isEmpty()) {
        VERIFY_EDIT(QDir().mkpath(artifacts));
        QImage frame(QSize(1280, 800), QImage::Format_ARGB32_Premultiplied);
        frame.fill(Qt::transparent);
        QPainter painter(&frame);
        StartupOverlay::paintFrame(painter, frame.size(), 0.68, editing.font());
        painter.end();
        VERIFY_EDIT(frame.save(QDir(artifacts).filePath("startup.png")));
    }

    // Space appends, Shift+Space prepends, F2 selects all. Modified Enter
    // inserts a newline and never dispatches a structural canvas command.
    QTest::keyClick(editing.canvas, Qt::Key_Space);
    VERIFY_EDIT(editing.editor && editing.editor->textCursor().position() == editing.editor->toPlainText().size());
    VERIFY_EDIT(!editing.editor->textCursor().hasSelection());
    VERIFY_EDIT(editing.editor->frameShape() == QFrame::NoFrame);
    VERIFY_EDIT(editing.editor->testAttribute(Qt::WA_TranslucentBackground));
    VERIFY_EDIT(!editing.items.value("a").label->isVisible());
    VERIFY_EDIT(editing.editorProxy->pos() == editing.items.value("a").label->scenePos());
    if (!artifacts.isEmpty()) VERIFY_EDIT(editing.grab().save(QDir(artifacts).filePath("inline-editing.png")));
    for (const auto modifier : {Qt::ControlModifier, Qt::ShiftModifier, Qt::AltModifier}) {
        QTest::keyClick(editing.editor, Qt::Key_Return, modifier);
        VERIFY_EDIT(editing.editor);
    }
    VERIFY_EDIT(editing.editor->toPlainText().count('\n') == 3 && editing.doc.nodes.size() == 7);
    QTest::keyClick(editing.editor, Qt::Key_Escape);
    VERIFY_EDIT(!editing.editor && editing.doc.serialize() == initial);
    VERIFY_EDIT(editing.items.value("a").label->isVisible());
    QTest::keyClick(editing.canvas, Qt::Key_Space, Qt::ShiftModifier);
    VERIFY_EDIT(editing.editor && editing.editor->textCursor().position() == 0);
    editing.finishEdit(false);
    QTest::keyClick(editing.canvas, Qt::Key_F2);
    VERIFY_EDIT(editing.editor && editing.editor->textCursor().selectedText() == editing.doc.nodes.value("a").text);
    editing.finishEdit(false);
    QTest::keyClick(editing.canvas, Qt::Key_Insert, Qt::ControlModifier);
    VERIFY_EDIT(editing.doc.nodes.size() == 10 && editing.parents.value(editing.selected) == "a");
    VERIFY_EDIT(editing.doc.nodes.value(editing.selected).notes == QStringLiteral("备注保留"));
    editing.history(true);
    VERIFY_EDIT(editing.doc.serialize() == initial);
    QTest::keyClick(editing.canvas, Qt::Key_Insert, Qt::ShiftModifier);
    VERIFY_EDIT(editing.editor && editing.doc.nodes.value(editing.selected).children == QStringList{"a"});
    editing.finishEdit();
    editing.history(true);
    VERIFY_EDIT(editing.doc.serialize() == initial);
    editing.selectNode("a");
    QTest::keyClick(editing.canvas, Qt::Key_Delete, Qt::ShiftModifier);
    VERIFY_EDIT(!editing.doc.nodes.contains("a") && editing.doc.nodes.contains("x") && editing.doc.nodes.contains("y"));
    VERIFY_EDIT(editing.doc.nodes.value("r").children == (QStringList{"x", "y", "b", "c", "d"}));
    editing.history(true);
    VERIFY_EDIT(editing.doc.serialize() == initial);
    editing.replaceSelection({"a", "x"});
    editing.removeTopicKeepChildren();
    VERIFY_EDIT(!editing.doc.nodes.contains("a") && !editing.doc.nodes.contains("x") && editing.doc.nodes.contains("y"));
    editing.history(true);
    VERIFY_EDIT(editing.doc.serialize() == initial);

    reset();
    editing.startEdit();
    editing.editor->setPlainText(QStringLiteral("前半后半"));
    auto splitCursor = editing.editor->textCursor();
    splitCursor.setPosition(2);
    editing.editor->setTextCursor(splitCursor);
    QTest::keyClick(editing.editor, Qt::Key_Return, Qt::AltModifier | Qt::ShiftModifier);
    VERIFY_EDIT(editing.editor && editing.doc.nodes.size() == 8);
    VERIFY_EDIT(editing.doc.nodes.value("a").text == QStringLiteral("前半"));
    VERIFY_EDIT(editing.doc.nodes.value(editing.selected).text == QStringLiteral("后半"));
    VERIFY_EDIT(editing.doc.nodes.value("a").children == (QStringList{"x", "y"}));
    editing.finishEdit(false);
    editing.history(true);
    VERIFY_EDIT(editing.doc.serialize() == initial);
    editing.replaceSelection({"a", "b"});
    editing.mergeSelectedTopics();
    VERIFY_EDIT(editing.doc.nodes.size() == 6 && editing.doc.nodes.value("a").text.contains('\n'));
    VERIFY_EDIT(editing.doc.nodes.value("a").notes == QStringLiteral("备注保留"));
    editing.history(true);
    VERIFY_EDIT(editing.doc.serialize() == initial);

    // The outline edits the same IDs, retains styles/notes, and provides its
    // own Enter/Tab habits without firing canvas shortcuts a second time.
    reset();
    QTest::keyClick(editing.canvas, Qt::Key_F10);
    if (!editing.outlineDock || !editing.outlineDock->isVisible()) {
        qInfo() << "Outline shortcut focus" << QApplication::focusWidget() << QApplication::activeWindow();
        for (const auto &command : editing.shortcutSettings.commands())
            if (command.defaults.contains(QKeySequence("F10")))
                qInfo() << command.action->text() << command.action->isEnabled() << command.action->shortcuts();
    }
    VERIFY_EDIT(editing.outlineDock && editing.outlineDock->isVisible());
    VERIFY_EDIT(editing.outlineItems.size() == 7);
    editing.outlineTree->setCurrentItem(editing.outlineItems.value("x"));
    QTest::qWait(20);
    VERIFY_EDIT(editing.selected == "x");
    QTest::keyClick(editing.outlineTree, Qt::Key_F2);
    auto *inlineText = editing.outlineTree->findChild<QLineEdit *>();
    VERIFY_EDIT(inlineText && inlineText->isVisible());
    inlineText->setText(QStringLiteral("大纲修改"));
    QTest::keyClick(inlineText, Qt::Key_Return);
    QTest::qWait(20);
    VERIFY_EDIT(editing.doc.nodes.value("x").text == QStringLiteral("大纲修改"));
    QTest::keyClick(editing.outlineTree, Qt::Key_Return);
    QTest::qWait(20);
    VERIFY_EDIT(editing.doc.nodes.size() == 8 && !editing.editor);
    const QString outlineNew = editing.selected;
    inlineText = editing.outlineTree->findChild<QLineEdit *>();
    VERIFY_EDIT(inlineText && inlineText->isVisible());
    inlineText->setText(QStringLiteral("新大纲条目"));
    QTest::keyClick(inlineText, Qt::Key_Return);
    QTest::qWait(20);
    QTest::keyClick(editing.outlineTree, Qt::Key_Tab);
    VERIFY_EDIT(editing.parents.value(outlineNew) == "x" && editing.doc.nodes.size() == 8);
    QTest::keyClick(editing.outlineTree, Qt::Key_Backtab, Qt::ShiftModifier);
    VERIFY_EDIT(editing.parents.value(outlineNew) == "a");
    editing.selectNode("b");
    VERIFY_EDIT(editing.outlineTree->currentItem() == editing.outlineItems.value("b"));
    if (!artifacts.isEmpty()) editing.grab().save(QDir(artifacts).filePath("outline-editing.png"));
    editing.toggleOutline();
    VERIFY_EDIT(!editing.outlineDock->isVisible());

    reset();
    QTest::keyClick(editing.canvas, Qt::Key_End);
    VERIFY_EDIT(editing.selected == "d");
    QTest::keyClick(editing.canvas, Qt::Key_Home, Qt::ShiftModifier);
    VERIFY_EDIT(editing.selectedRoots(true) == (QStringList{"a", "b", "c", "d"}));
    editing.selectNode("b");
    QTest::keyClick(editing.canvas, Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier);
    VERIFY_EDIT(editing.additionalSelection.size() == 3);
    editing.selectNode("a");
    editing.toggleCollapse();
    QTest::keyClick(editing.canvas, Qt::Key_A, Qt::ControlModifier | Qt::AltModifier);
    VERIFY_EDIT(editing.selected == "x" && editing.additionalSelection.contains("y") && editing.items.contains("x"));
    QTest::keyClick(editing.canvas, Qt::Key_Home, Qt::ControlModifier);
    VERIFY_EDIT(editing.selected == "r");
    editing.selectNode("x");
    QTest::keyClick(editing.canvas, Qt::Key_1, Qt::AltModifier);
    VERIFY_EDIT(editing.items.size() == 5 && editing.doc.nodes.value("a").collapsed);
    QTest::keyClick(editing.canvas, Qt::Key_0, Qt::AltModifier);
    VERIFY_EDIT(editing.items.size() == 7);
    editing.selectNode("a");
    editing.setBranchDetail(0);
    QTest::keyClick(editing.canvas, Qt::Key_D, Qt::AltModifier);
    VERIFY_EDIT(editing.items.contains("x"));

    reset();
    editing.replaceSelection({"b", "c"});
    QTest::keyClick(editing.canvas, Qt::Key_Up, Qt::ControlModifier | Qt::ShiftModifier);
    VERIFY_EDIT(editing.doc.nodes.value("r").children == (QStringList{"b", "c", "a", "d"}));
    VERIFY_EDIT(editing.additionalSelection.size() == 1 && editing.undoStack.size() == 1);
    QTest::keyClick(editing.canvas, Qt::Key_End, Qt::ControlModifier | Qt::ShiftModifier);
    VERIFY_EDIT(editing.doc.nodes.value("r").children == (QStringList{"a", "d", "b", "c"}));
    editing.history(true);
    editing.history(true);
    VERIFY_EDIT(editing.doc.serialize() == initial);
    editing.selectNode("b");
    editing.changeBranchLevel(false);
    VERIFY_EDIT(editing.parents.value("b") == "a");
    editing.history(true);
    VERIFY_EDIT(editing.doc.serialize() == initial);
    editing.selectNode("x");
    editing.changeBranchLevel(true);
    VERIFY_EDIT(editing.doc.nodes.value("r").children == (QStringList{"a", "x", "b", "c", "d"}));
    editing.history(true);
    VERIFY_EDIT(editing.doc.serialize() == initial);

    reset();
    QString error;
    VERIFY_EDIT(!editing.insertOutline(QStringLiteral("主题\n  子级\n 错误缩进"), false, &error));
    VERIFY_EDIT(editing.doc.serialize() == initial && editing.undoStack.isEmpty());
    VERIFY_EDIT(editing.insertOutline(QStringLiteral("计划\n\t需求\n验证"), true, &error));
    const QString inserted = editing.selected;
    VERIFY_EDIT(editing.doc.nodes.value("r").children.size() == 6);
    VERIFY_EDIT(editing.doc.nodes.value("r").children.at(1) == inserted);
    VERIFY_EDIT(editing.doc.nodes.value(inserted).children.size() == 1 && editing.additionalSelection.size() == 1);
    editing.history(true);
    VERIFY_EDIT(editing.doc.serialize() == initial);
    QTimer::singleShot(0, &editing, [&] {
        auto *dialog = editing.findChild<QDialog *>("multipleTopicsDialog");
        if (!dialog) return;
        dialog->findChild<QPlainTextEdit *>("multipleTopicsText")->setPlainText(QStringLiteral("一\n\t二\n三"));
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    QTest::keyClick(editing.canvas, Qt::Key_M, Qt::ControlModifier);
    VERIFY_EDIT(editing.doc.nodes.size() == 10);
    editing.history(true);

    reset();
    editing.replaceSelection({"x", "y"});
    QTest::keyClick(editing.canvas, Qt::Key_I, Qt::ControlModifier);
    QTest::keyClick(editing.canvas, Qt::Key_U, Qt::ControlModifier);
    QTest::keyClick(editing.canvas, Qt::Key_X, Qt::ControlModifier | Qt::ShiftModifier);
    VERIFY_EDIT(editing.doc.nodes.value("x").style.italic && editing.doc.nodes.value("y").style.underline);
    VERIFY_EDIT(editing.items.value("x").label->font().strikeOut());
    Document reopened;
    VERIFY_EDIT(reopened.parse(editing.doc.serialize(), &error) && reopened.nodes.value("x").style == editing.doc.nodes.value("x").style);
    const auto clipboard = BranchClipboard::encode(editing.doc, "x", &error);
    QStringList clones;
    VERIFY_EDIT(BranchClipboard::paste(&reopened, "b", clipboard, &clones, &error));
    VERIFY_EDIT(reopened.nodes.value(clones.first()).style == editing.doc.nodes.value("x").style);
    editing.history(true); editing.history(true); editing.history(true);
    VERIFY_EDIT(editing.doc.serialize() == initial);

    reset();
    editing.fit();
    const QPoint from = editing.canvas->mapFromScene(editing.items.value("a").body->pos());
    const QPoint to = editing.canvas->mapFromScene(editing.items.value("b").body->pos());
    QTest::mousePress(editing.canvas->viewport(), Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(editing.canvas->viewport(), to, 10);
    QTest::keyClick(editing.canvas, Qt::Key_Escape);
    QTest::mouseRelease(editing.canvas->viewport(), Qt::LeftButton, Qt::NoModifier, to);
    VERIFY_EDIT(editing.doc.serialize() == initial && editing.gestureHint == Window::GestureHint::None);
    QTest::keyClick(editing.canvas, Qt::Key_0, Qt::ControlModifier);
    VERIFY_EDIT(qAbs(editing.canvas->transform().m11() - 1) < 0.001 && !editing.fitMode);
    QTest::keyClick(editing.canvas, Qt::Key_F3, Qt::AltModifier | Qt::ShiftModifier);
    VERIFY_EDIT(editing.fitMode);
    editing.selectNode("a");
    QTest::keyClick(editing.canvas, Qt::Key_F4);
    VERIFY_EDIT(editing.focusRoot == "a" && editing.overviewPane->isVisible());
    QTest::keyClick(editing.canvas, Qt::Key_F4);
    VERIFY_EDIT(editing.focusRoot.isEmpty());

    // A conflicting assignment is atomic; a saved custom key drives the actual
    // QAction, survives a fresh Window, and updates the contextual hint.
    auto &settings = editing.shortcutSettings;
    VERIFY_EDIT(settings.commands().size() > 90);
    VERIFY_EDIT(settings.apply(settings.current(), &error, false));
    auto bindings = settings.current();
    QAction *editAction = nullptr;
    for (const auto &command : settings.commands())
        if (command.defaults.contains(QKeySequence("F2"))) editAction = command.action;
    VERIFY_EDIT(editAction);
    bindings[editAction] = {QKeySequence("Ctrl+S")};
    VERIFY_EDIT(!settings.apply(bindings, &error) && editAction->shortcut() == QKeySequence("F2"));
    bindings[editAction] = {QKeySequence("Ctrl+Alt+F2")};
    VERIFY_EDIT(settings.apply(bindings, &error));
    VERIFY_EDIT(settings.hint("F2") == QKeySequence("Ctrl+Alt+F2").toString(QKeySequence::NativeText));
    VERIFY_EDIT(editing.shortcutHint->toolTip().contains(settings.hint("F2")));
    QTest::keyClick(editing.canvas, Qt::Key_F2, Qt::ControlModifier | Qt::AltModifier);
    VERIFY_EDIT(editing.editor);
    editing.finishEdit(false);
    {
        Window reopenedWindow(false);
        VERIFY_EDIT(reopenedWindow.shortcutSettings.hint("F2") == settings.hint("F2"));
    }
    bool inspected = false;
    QTimer::singleShot(0, &editing, [&] {
        auto *dialog = editing.findChild<QDialog *>("shortcutSettingsDialog");
        if (!dialog) return;
        auto *table = dialog->findChild<QTableWidget *>("shortcutTable");
        auto *catalogue = dialog->findChild<QTableWidget *>("mindmasterShortcutCatalogue");
        inspected = table && table->rowCount() > 90 && catalogue && catalogue->rowCount() >= 130;
        if (!artifacts.isEmpty()) dialog->grab().save(QDir(artifacts).filePath("shortcut-settings.png"));
        dialog->reject();
    });
    settings.showDialog(&editing);
    VERIFY_EDIT(inspected);
    VERIFY_EDIT(settings.apply(settings.defaults(), &error));
    VERIFY_EDIT(editAction->shortcut() == QKeySequence("F2"));
    if (!artifacts.isEmpty()) {
        Window demo(false);
        VERIFY_EDIT(demo.loadFrom(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../examples/研发项目交付.smap")), &error));
        demo.resize(1280, 800);
        demo.show();
        demo.activateWindow();
        QTest::qWait(50);
        demo.toggleOutline();
        demo.chooseLayout(true, BranchDirection::Right);
        demo.fit();
        QTest::qWait(40);
        VERIFY_EDIT(demo.grab().save(QDir(artifacts).filePath("outline-editing.png")));
        demo.hide();
    }
    reset();
    editing.beginRelationship(false);
    VERIFY_EDIT(editing.relationshipSource == "a");
    const QPoint bPoint = editing.canvas->mapFromScene(editing.items.value("b").body->pos());
    QTest::mouseClick(editing.canvas->viewport(), Qt::LeftButton, Qt::NoModifier, bPoint);
    VERIFY_EDIT(editing.doc.relationships.size() == 1 && editing.doc.relationships.first().to == "b");
    const auto relationBefore = editing.doc.serialize();
    const auto originalBend = editing.doc.relationships.first().bend;
    editing.moveDecoration(QPointF(45, -25));
    VERIFY_EDIT(editing.doc.relationships.first().bend == originalBend + QPointF(45, -25));
    VERIFY_EDIT(editing.undoStack.last().document.serialize() == relationBefore);
    editing.undoAction->trigger();
    VERIFY_EDIT(editing.doc.serialize() == relationBefore);
    editing.copyBranch(); editing.pasteBranch();
    VERIFY_EDIT(editing.doc.relationships.size() == 2);
    editing.removeSelected();
    VERIFY_EDIT(editing.doc.relationships.size() == 1);
    editing.selectedDecoration = editing.doc.relationships[0].id;
    editing.render();
    QPoint handle;
    for (auto *item : editing.scene->items()) if (item->data(2).toBool()) handle = editing.canvas->mapFromScene(item->pos());
    const auto beforeDrag = editing.doc.relationships[0].bend;
    QTest::mousePress(editing.canvas->viewport(), Qt::LeftButton, Qt::NoModifier, handle);
    QTest::mouseMove(editing.canvas->viewport(), handle + QPoint(40, 25));
    QTest::mouseRelease(editing.canvas->viewport(), Qt::LeftButton, Qt::NoModifier, handle + QPoint(40, 25));
    VERIFY_EDIT(editing.doc.relationships[0].bend != beforeDrag);
    editing.undoAction->trigger();
    VERIFY_EDIT(editing.doc.relationships[0].bend == beforeDrag);
    editing.replaceSelection({"a", "b"}); editing.addBoundary();
    VERIFY_EDIT(editing.doc.boundaries.size() == 1);
    const auto grouped = editing.doc.serialize();
    editing.removeSelected();
    VERIFY_EDIT(editing.doc.boundaries.isEmpty() && editing.doc.nodes.size() == 7);
    editing.undoAction->trigger();
    VERIFY_EDIT(editing.doc.serialize() == grouped);
    editing.doc.relationships[0].text = QStringLiteral("跨分支关系");
    editing.doc.boundaries[0].text = QStringLiteral("交付范围");
    editing.changed();
    const QByteArray copied = BranchClipboard::encode(editing.doc, QStringList{"a", "b"}, &error);
    QStringList pasted;
    Document copiedMap = editing.doc;
    VERIFY_EDIT(BranchClipboard::paste(&copiedMap, "r", copied, &pasted, &error));
    VERIFY_EDIT(copiedMap.relationships.size() == 2 && copiedMap.boundaries.size() == 2);
    VERIFY_EDIT(copiedMap.relationships[1].from == pasted[0] && copiedMap.relationships[1].to == pasted[1]);
    VERIFY_EDIT(copiedMap.relationships[1].id != copiedMap.relationships[0].id);
    editing.selectNode("c");
    editing.floatBranches({"c"}, QPointF(300, 250), false);
    VERIFY_EDIT(editing.doc.floating.contains("c") && !editing.edges.contains("c"));
    const auto floatingPosition = editing.doc.floating.value("c");
    editing.doc.nodes["r"].collapsed = true; editing.changed();
    VERIFY_EDIT(editing.items.contains("c") && editing.items["c"].body->pos() == floatingPosition);
    editing.doc.nodes["r"].collapsed = false; editing.changed();
    Document decoratedReopened;
    VERIFY_EDIT(decoratedReopened.parse(editing.doc.serialize(), &error) && decoratedReopened.serialize() == editing.doc.serialize());
    VERIFY_EDIT(editing.exportTo(temporary.filePath("decorations.svg"), "svg", &error));
    VERIFY_EDIT(exportExchange(editing.doc, temporary.filePath("decorations.drawio"), "drawio", &error));
    VERIFY_EDIT(!exportExchange(editing.doc, temporary.filePath("lossy.opml"), "opml", &error));
    editing.selectNode("a"); editing.removeSelected();
    VERIFY_EDIT(editing.doc.relationships.isEmpty() && editing.doc.boundaries.first().members == QStringList{"b"});
    editing.undoAction->trigger();
    VERIFY_EDIT(editing.doc.relationships.size() == 1);
    editing.moveBranches({"c"}, "b", 0, false);
    VERIFY_EDIT(!editing.doc.floating.contains("c") && editing.edges.contains("c"));
    editing.undoAction->trigger();
    VERIFY_EDIT(editing.doc.floating.contains("c") && !editing.edges.contains("c"));
    editing.selectNode("c"); editing.focusBranch();
    VERIFY_EDIT(editing.overviewItems.contains("r") && editing.items.contains("c"));
    editing.leaveFocus();
    editing.createFloating(QPointF(-250, 250)); editing.finishEdit();
    VERIFY_EDIT(editing.doc.floating.size() == 2);
    editing.fit();
    if (!artifacts.isEmpty()) {
        editing.doc.nodes["r"].text = QStringLiteral("产品交付");
        editing.doc.nodes["a"].text = QStringLiteral("需求与设计");
        editing.doc.nodes["b"].text = QStringLiteral("开发与验证");
        editing.doc.nodes["c"].text = QStringLiteral("待探索的想法");
        editing.doc.nodes["d"].text = QStringLiteral("发布准备");
        editing.doc.nodes["x"].text = QStringLiteral("用户操作习惯");
        editing.doc.nodes["y"].text = QStringLiteral("界面与交互");
        editing.changed(); editing.fit(); QTest::qWait(50);
        VERIFY_EDIT(editing.grab().save(QDir(artifacts).filePath("relationships-floating.png")));
    }
    reset();
    QImage topicImage(320,180,QImage::Format_ARGB32_Premultiplied); topicImage.fill(QColor("#dceee4"));
    qInfo()<<"Testing embedded image lifecycle";
    { QPainter painter(&topicImage); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(QPen(QColor("#258265"),5)); painter.drawEllipse(QRect(65,35,110,110)); painter.drawLine(170,90,260,50); painter.drawLine(170,90,260,135); }
    VERIFY_EDIT(editing.setTopicImage(topicImage));
    qInfo()<<"Image inserted";
    VERIFY_EDIT(editing.items["a"].image && !editing.items["a"].image->pixmap().isNull());
    VERIFY_EDIT(editing.scene->items().contains(editing.items["a"].image));
    editing.render(); VERIFY_EDIT(editing.scene->items().contains(editing.items["a"].image));
    VERIFY_EDIT(editing.items["a"].body->sceneBoundingRect().contains(editing.items["a"].image->sceneBoundingRect()));
    const auto mediaBytes=editing.doc.nodes["a"].imagePng;
    editing.undoAction->trigger(); VERIFY_EDIT(editing.doc.nodes["a"].imagePng.isEmpty() && !editing.items["a"].image);
    qInfo()<<"Image undone";
    editing.redoAction->trigger(); VERIFY_EDIT(editing.doc.nodes["a"].imagePng==mediaBytes);
    qInfo()<<"Image redone";
    VERIFY_EDIT(writeClipboard([&] { auto *mime=new QMimeData; mime->setImageData(topicImage); return mime; }));
    editing.pasteBranch(); VERIFY_EDIT(editing.doc.nodes["a"].imagePng==mediaBytes);
    QTimer::singleShot(50,[&] {
        auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if(!dialog) return;
        auto *input=dialog->findChild<QPlainTextEdit *>(); if(!input) { dialog->reject(); return; }
        input->setPlainText("https://example.com/project");
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    }); editing.editLinks(); VERIFY_EDIT(editing.doc.nodes["a"].links==QStringList{"https://example.com/project"});
    VERIFY_EDIT(editing.items["a"].link && !editing.items["a"].link->toolTip().isEmpty());
    const auto mediaPath=temporary.filePath(QStringLiteral("高效编辑.smap")); VERIFY_EDIT(editing.saveTo(mediaPath));
    qInfo()<<"Media saved";
    Document media; VERIFY_EDIT(media.readFile(mediaPath,&error));
    VERIFY_EDIT(media.nodes["a"].imagePng==mediaBytes && media.nodes["a"].links==editing.doc.nodes["a"].links);
    const auto mediaCopy=BranchClipboard::encode(media,"a",&error); QStringList mediaRoots;
    VERIFY_EDIT(BranchClipboard::paste(&media,"r",mediaCopy,&mediaRoots,&error));
    qInfo()<<"Media copied";
    VERIFY_EDIT(media.nodes[mediaRoots.first()].imagePng==mediaBytes && media.nodes[mediaRoots.first()].links==media.nodes["a"].links);
    VERIFY_EDIT(editing.exportTo(temporary.filePath("media.svg"),"svg",&error));
    qInfo()<<"Media exported";
    QFile mediaSvg(temporary.filePath("media.svg")); VERIFY_EDIT(mediaSvg.open(QIODevice::ReadOnly)); VERIFY_EDIT(mediaSvg.readAll().contains("data:image/png;base64"));
    VERIFY_EDIT(!exportExchange(media,temporary.filePath("lossy-media.opml"),"opml",&error));
    editing.doc.nodes["a"].style.italic=true; editing.changed(); editing.selectNode("a");
    qInfo()<<"Testing continuous format brush";
    editing.findChild<QAction *>("copyTopicStyleAction")->trigger(); VERIFY_EDIT(editing.formatBrush);
    const auto brushPoint=editing.canvas->mapFromScene(editing.items["b"].body->pos());
    editing.canvas->press(brushPoint,Qt::NoModifier); VERIFY_EDIT(editing.doc.nodes["b"].style.italic && editing.formatBrush);
    editing.leaveFocusAction->trigger(); VERIFY_EDIT(!editing.formatBrush);
    editing.undoAction->trigger(); VERIFY_EDIT(!editing.doc.nodes["b"].style.italic);
    // Exercise the actual dialog: one transactional replace covers topic and note fields.
    bool dialogVerified=false;
    qInfo()<<"Testing find/replace dialog";
    QTimer::singleShot(50,[&] {
        auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if(!dialog) return;
        auto *find=dialog->findChild<QLineEdit *>("findText"); auto *replacement=dialog->findChild<QLineEdit *>("replacementText");
        if(!find || !replacement) { dialog->reject(); return; }
        find->setText(QStringLiteral("主题")); replacement->setText(QStringLiteral("工作"));
        if(!artifacts.isEmpty()) dialog->grab().save(QDir(artifacts).filePath("find-replace.png"));
        for(auto *button:dialog->findChildren<QPushButton *>()) if(button->text()==QStringLiteral("全部替换")) button->click();
        dialogVerified=editing.doc.nodes["r"].text==QStringLiteral("工作 r") && editing.doc.nodes["x"].text==QStringLiteral("工作 x");
        dialog->accept();
    });
    editing.findReplace(); VERIFY_EDIT(dialogVerified);
    qInfo()<<"Testing Markdown and selective paste";
    editing.undoAction->trigger(); VERIFY_EDIT(editing.doc.nodes["r"].text==QStringLiteral("主题 r"));
    editing.selectNode("r");
    VERIFY_EDIT(writeClipboard([] { auto *mime=new QMimeData; mime->setText(QStringLiteral("# 会议纪要\n## 结论\n- 持续打磨编辑体验")); return mime; }));
    const auto beforeMarkdown=editing.doc.nodes.size(); editing.pasteBranch(); VERIFY_EDIT(editing.doc.nodes.size()==beforeMarkdown+3);
    VERIFY_EDIT(editing.doc.nodes[editing.selected].text==QStringLiteral("会议纪要"));
    editing.undoAction->trigger(); VERIFY_EDIT(editing.doc.nodes.size()==beforeMarkdown);
    VERIFY_EDIT(writeClipboard([] { auto *mime=new QMimeData; mime->setText(QStringLiteral("第一行\n第二行")); return mime; }));
    QTimer::singleShot(50,[&] {
        auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if(!dialog) return;
        const auto combos=dialog->findChildren<QComboBox *>(); if(combos.size()!=2) { dialog->reject(); return; }
        combos.first()->setCurrentIndex(3);
        if(!artifacts.isEmpty()) dialog->grab().save(QDir(artifacts).filePath("paste-special.png"));
        dialog->accept();
    }); editing.pasteSpecial(); VERIFY_EDIT(editing.doc.nodes[editing.selected].text==QStringLiteral("第一行\n第二行")); editing.undoAction->trigger();
    editing.doc.nodes["r"].text=QStringLiteral("新保存版本"); editing.changed(); VERIFY_EDIT(editing.saveTo(mediaPath));
    const auto beforeRestore=editing.doc.serialize(); bool historyDialog=false;
    qInfo()<<"Testing local history restore";
    QTimer::singleShot(50,[&] {
        auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if(!dialog) return;
        auto *list=dialog->findChild<QListWidget *>(); if(!list || list->count()<2) { dialog->reject(); return; }
        list->setCurrentRow(list->count()-1); historyDialog=true;
        if(!artifacts.isEmpty()) dialog->grab().save(QDir(artifacts).filePath("local-history.png"));
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::RestoreDefaults)->click();
    }); editing.showVersions(); VERIFY_EDIT(historyDialog && editing.dirty && editing.doc.nodes["r"].text==QStringLiteral("主题 r"));
    editing.undoAction->trigger(); VERIFY_EDIT(editing.doc.serialize()==beforeRestore);
    if(!artifacts.isEmpty()) { editing.doc.nodes["r"].text=QStringLiteral("高效编辑工作台"); editing.doc.nodes["a"].text=QStringLiteral("图片与参考链接"); editing.doc.nodes["b"].text=QStringLiteral("选择性粘贴 · 连续格式刷"); editing.doc.nodes["c"].text=QStringLiteral("查找替换 · Markdown"); editing.doc.nodes["d"].text=QStringLiteral("本地历史版本"); editing.changed(); editing.fit(); QTest::qWait(50); VERIFY_EDIT(editing.grab().save(QDir(artifacts).filePath("editing-workbench.png"))); }
    qInfo() << "Editing, media, format brush, replace, Markdown, undo, persistence and export passed.";
#undef VERIFY_EDIT
    return 0;
}
