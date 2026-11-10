#include <QtTest/QTest>

int runSmokeTest() {
#define CHECK(condition) do { if (!(condition)) { qCritical() << __LINE__ << #condition; return 1; } } while (false)
    // The Windows clipboard is shared with other processes, including remote desktop.
    const auto waitForClipboard = [](const std::function<void()> &write,
                                     const std::function<bool(const QMimeData &)> &matches) {
        for (int attempt = 0; attempt < 4; ++attempt) {
            write();
            const auto *mime = QApplication::clipboard()->mimeData();
            if (mime && matches(*mime)) return true;
            QTest::qWait(50);
        }
        return false;
    };
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir temp;
    CHECK(temp.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, temp.path());
    qInfo() << "Smoke platform:" << QApplication::platformName();
    const QStringList embedded = embeddedFontFamilies();
    CHECK(embedded.size() == 4);
    for (const auto &family : embedded) {
        const QRawFont raw = QRawFont::fromFont(fontFor({family, 12, false}));
        if (!raw.isValid() || raw.familyName() != family || !raw.supportsCharacter(QChar(0x679D)))
            qCritical() << family << raw.familyName() << raw.isValid() << raw.supportsCharacter(QChar(0x679D));
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
    CHECK(window.shortcutHint->text().contains(QStringLiteral("Esc 取消")));
    window.editor->setPlainText(QStringLiteral("研发项目交付"));
    QTest::keyClick(window.editor, Qt::Key_Return, Qt::ShiftModifier);
    CHECK(window.editor && window.editor->toPlainText().contains('\n'));
    window.editor->clear();
    QInputMethodEvent preedit(QStringLiteral("yanfa"), {});
    QCoreApplication::sendEvent(window.editor, &preedit);
    CHECK(window.editor->toPlainText().isEmpty());
    QTest::keyClick(window.editor, Qt::Key_Return);
    CHECK(window.editor && window.editor->toPlainText().isEmpty());
    QInputMethodEvent commit;
    commit.setCommitString(QStringLiteral("研发项目交付"));
    QCoreApplication::sendEvent(window.editor, &commit);
    QTest::keyClick(window.editor, Qt::Key_Return);
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

    {
        Window workspace(false);
        workspace.doc = window.doc;
        workspace.doc.nodes[first].style.alignment = TextAlignment::Left;
        workspace.doc.nodes[first].style.points = 16;
        workspace.initializeDocument();
        workspace.show();
        workspace.activateWindow();
        workspace.canvas->setFocus();
        QTest::qWait(40);
        workspace.fit();
        auto point = [&](const QString &id) {
            return workspace.canvas->mapFromScene(workspace.items.value(id).body->pos());
        };
        auto drag = [&](const QString &from, const QString &to, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
            QTest::mousePress(workspace.canvas->viewport(), Qt::LeftButton, modifiers, point(from));
            QTest::mouseRelease(workspace.canvas->viewport(), Qt::LeftButton, modifiers, point(to));
        };
        const QByteArray original = workspace.doc.serialize();
        auto *areaButton = workspace.findChild<QToolButton *>("areaSelectionButton");
        CHECK(areaButton && areaButton->isVisible() && !areaButton->isChecked());
        CHECK(areaButton->toolTip().contains(shortcutText(boxShortcut)));
        auto *viewport = workspace.canvas->viewport();
        const QPoint bandStart(2, 2);
        const QPoint bandEnd = viewport->rect().bottomRight() - QPoint(2, 2);
        CHECK(workspace.nodeAt(bandStart).isEmpty());
        const auto selectArea = [&](Qt::KeyboardModifiers modifiers) {
            workspace.selectNode(first);
            QTest::mousePress(viewport, Qt::LeftButton, modifiers, bandStart);
            const bool hintShown = workspace.shortcutHint->text().contains(QStringLiteral("完成框选"));
            QTest::mouseRelease(viewport, Qt::LeftButton, modifiers, bandEnd);
            return hintShown && workspace.shortcutHint->text().contains(QStringLiteral("批量样式")) &&
                (workspace.additionalSelection.contains(child) || workspace.selected == child);
        };
        QTest::keyClick(workspace.canvas, Qt::Key_B, Qt::ControlModifier | Qt::ShiftModifier);
        CHECK(areaButton->isChecked() && selectArea(Qt::NoModifier));
        areaButton->click();
        CHECK(!areaButton->isChecked() && selectArea(Qt::ShiftModifier));
        CHECK(workspace.doc.serialize() == original && workspace.undoStack.isEmpty());
        workspace.selectNode(child);
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, point(child));
        const QPoint dragPoint = point(child) + QPoint(QApplication::startDragDistance() + 2, 0);
        QMouseEvent preview(QEvent::MouseMove, dragPoint, viewport->mapToGlobal(dragPoint),
                            Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
        QCoreApplication::sendEvent(viewport, &preview);
        CHECK(workspace.shortcutHint->text().contains(QStringLiteral("完成复制")));
        QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, point(child));
        CHECK(!workspace.shortcutHint->text().contains(QStringLiteral("完成复制")));

        auto *helpAction = workspace.findChild<QAction *>("helpAction");
        auto *aboutAction = workspace.findChild<QAction *>("aboutAction");
        CHECK(helpAction && aboutAction && helpAction->shortcut() == QKeySequence(Qt::Key_F1));
        bool helpLoaded = false;
        QTimer::singleShot(0, &workspace, [&] {
            auto *dialog = workspace.findChild<QDialog *>("helpDialog");
            if (!dialog) return;
            auto *guide = dialog->findChild<QTextBrowser *>();
            helpLoaded = guide && guide->toPlainText().contains(QStringLiteral("快捷键速查"));
            dialog->reject();
        });
        helpAction->trigger();
        CHECK(helpLoaded);
        bool versionShown = false;
        QTimer::singleShot(0, &workspace, [&] {
            auto *dialog = workspace.findChild<QMessageBox *>("aboutDialog");
            if (!dialog) return;
            versionShown = !QCoreApplication::applicationVersion().isEmpty() &&
                dialog->text().contains(QCoreApplication::applicationVersion());
            dialog->reject();
        });
        aboutAction->trigger();
        CHECK(versionShown);
        workspace.activateWindow();
        workspace.canvas->setFocus();
        QTest::qWait(40);

        workspace.replaceSelection({secondMatch, child, sibling});
        drag(secondMatch, root);
        CHECK(workspace.doc.nodes.value(root).children == (QStringList{first, second, child, sibling, secondMatch}));
        CHECK(workspace.additionalSelection.size() == 2 && workspace.undoStack.size() == 1);
        workspace.history(true);
        CHECK(workspace.doc.serialize() == original);

        workspace.replaceSelection({first, child, secondMatch});
        drag(child, sibling);
        CHECK(workspace.doc.serialize() == original && workspace.undoStack.isEmpty());
        drag(child, second, Qt::ControlModifier);
        CHECK(workspace.doc.nodes.size() == 11 && workspace.additionalSelection.size() == 1);
        const auto copies = workspace.doc.nodes.value(second).children.mid(2);
        CHECK(copies.size() == 2 && workspace.doc.nodes.value(copies.first()).children.size() == 2);
        workspace.history(true);
        CHECK(workspace.doc.serialize() == original);
        workspace.replaceSelection({child, sibling});
        QTest::mouseClick(workspace.canvas->viewport(), Qt::LeftButton, Qt::NoModifier, point(sibling));
        CHECK(workspace.selected == sibling && workspace.additionalSelection.isEmpty());

        workspace.selectNode(first);
        workspace.canvas->zoomBy(1.18);
        QCoreApplication::processEvents();
        const QTransform fullTransform = workspace.canvas->transform();
        const QPointF fullCenter = workspace.canvas->mapToScene(workspace.canvas->viewport()->rect().center());
        workspace.findChild<QAction *>("focusBranchAction")->trigger();
        CHECK(workspace.items.size() == 3 && workspace.items.contains(child) && !workspace.edges.contains(first));
        CHECK(workspace.overview->isVisible() && workspace.overviewItems.size() == 7);
        CHECK(workspace.overviewItems.contains(second) && workspace.focusSplitter->count() == 2);
        CHECK(!workspace.dirty && workspace.undoStack.isEmpty() && workspace.doc.serialize() == original);
        workspace.selectNode(child);
        workspace.startEdit();
        CHECK(workspace.editor);
        workspace.editor->setPlainText(QStringLiteral("局部修改同步到全图"));
        CHECK(workspace.finishEdit());
        CHECK(workspace.overviewItems.value(child).label->toPlainText() == QStringLiteral("局部修改同步到全图"));
        workspace.history(true);
        CHECK(workspace.doc.serialize() == original);
        CHECK(workspace.overviewItems.value(child).label->toPlainText() == workspace.doc.nodes.value(child).text);
        const auto clickOverview = [&](const QString &id) {
            const QPoint at = workspace.overview->mapFromScene(workspace.overviewItems.value(id).body->pos());
            QTest::mouseClick(workspace.overview->viewport(), Qt::LeftButton, Qt::NoModifier, at);
        };
        clickOverview(second);
        CHECK(workspace.focusRoot == second && workspace.overviewItems.size() == 7 && !workspace.items.contains(first));
        clickOverview(first);
        CHECK(workspace.focusRoot == first && workspace.items.contains(child));
        const QSize beforeResize = workspace.size();
        workspace.resize(650, 520);
        QCoreApplication::processEvents();
        CHECK(workspace.focusSplitter->orientation() == Qt::Vertical);
        workspace.resize(beforeResize);
        QCoreApplication::processEvents();
        QString error;
        const QString fullPath = temp.filePath("full-from-focus.svg");
        CHECK(workspace.exportTo(fullPath, "svg", &error));
        QFile full(fullPath);
        CHECK(full.open(QIODevice::ReadOnly) && full.readAll().contains(QStringLiteral("技术验证").toUtf8()));
        CHECK(workspace.focusRoot == first);
        CHECK(workspace.shortcutHint->text().contains(QStringLiteral("Esc 返回全图")));
        workspace.activateWindow();
        workspace.canvas->setFocus();
        QTest::qWait(50);
        QTest::keyClick(workspace.canvas, Qt::Key_Escape);
        QCoreApplication::processEvents();
        CHECK(workspace.focusRoot.isEmpty() && workspace.items.size() == 7 && workspace.overviewPane->isHidden());
        CHECK(workspace.canvas->transform() == fullTransform);
        const QPointF restoredCenter = workspace.canvas->mapToScene(workspace.canvas->viewport()->rect().center());
        if (QLineF(restoredCenter, fullCenter).length() >= 4.0 / fullTransform.m11())
            qInfo() << "Full map center before and after focus:" << fullCenter << restoredCenter;
        CHECK(QLineF(restoredCenter, fullCenter).length() < 4.0 / fullTransform.m11());
        workspace.focusBranch();
        workspace.query = QStringLiteral("技术验证");
        workspace.search(false);
        CHECK(workspace.focusRoot.isEmpty() && workspace.selected == second);
        qInfo() << "Split focus: edit sync, branch switching, narrow window, zoom and center restoration passed";

        workspace.selectNode(first);
        workspace.findChild<QAction *>("copyTopicStyleAction")->trigger();
        workspace.replaceSelection({secondMatch, sibling});
        workspace.findChild<QAction *>("pasteTopicStyleAction")->trigger();
        CHECK(workspace.doc.nodes.value(secondMatch).style == workspace.doc.nodes.value(first).style);
        CHECK(workspace.doc.nodes.value(sibling).style == workspace.doc.nodes.value(first).style);
        CHECK(workspace.undoStack.size() == 1);
        workspace.history(true);
        CHECK(workspace.doc.serialize() == original);

        workspace.selectNode(first);
        workspace.toggleCollapse();
        const QByteArray beforeExport = workspace.doc.serialize();
        const QString nativePath = temp.filePath("selected-branch.smap");
        const QString svgPath = temp.filePath("selected-branch.svg");
        CHECK(workspace.exportBranchTo(first, nativePath, "smap", &error));
        QFile native(nativePath);
        Document detached;
        CHECK(native.open(QIODevice::ReadOnly) && detached.parse(native.readAll(), &error));
        CHECK(detached.root == first && detached.nodes.size() == 3 && detached.nodes.value(first).collapsed);
        CHECK(workspace.exportBranchTo(first, svgPath, "svg", &error));
        QFile svg(svgPath);
        CHECK(svg.open(QIODevice::ReadOnly));
        const auto branchSvg = svg.readAll();
        CHECK(branchSvg.contains(QStringLiteral("梳理需求与验收指标").toUtf8()));
        CHECK(!branchSvg.contains(QStringLiteral("技术验证").toUtf8()));
        CHECK(waitForClipboard(
            [&] { workspace.findChild<QAction *>("copyBranchImageAction")->trigger(); },
            [](const QMimeData &mime) { return mime.hasImage() && mime.hasFormat("image/png"); }));
        const QImage copiedImage = QApplication::clipboard()->image();
        CHECK(!copiedImage.isNull() && copiedImage.pixelColor(0, 0).alpha() == 0);
        CHECK(workspace.doc.serialize() == beforeExport && workspace.selected == first && workspace.undoStack.size() == 1);
        workspace.hide();
        window.activateWindow();
        window.canvas->setFocus();
        QTest::qWait(30);
    }

    {
        Window routing(false);
        routing.doc = window.doc;
        routing.doc.nodes[first].text = QStringLiteral("项目规划\n多行主题\n第三行");
        routing.doc.nodes[lastSearchMatch].text = QString(120, QChar(0x679D));
        Node detail;
        detail.id = "routing-detail";
        detail.text = QStringLiteral("更深层级");
        routing.doc.nodes.insert(detail.id, detail);
        routing.doc.nodes[secondMatch].children.append(detail.id);
        routing.initializeDocument();
        QPainterPathStroker stroke;
        stroke.setWidth(0.5);
        for (bool classic : {true, false}) {
            for (auto direction : {BranchDirection::Both, BranchDirection::Left, BranchDirection::Right}) {
                if (!classic && direction != BranchDirection::Both) continue;
                routing.chooseLayout(classic, direction);
                for (auto style : {ConnectorStyle::Orthogonal, ConnectorStyle::Straight, ConnectorStyle::Curved,
                                   ConnectorStyle::Automatic}) {
                    routing.chooseConnectorStyle(style);
                    Document reopened;
                    CHECK(reopened.parse(routing.doc.serialize()) && reopened.connectorStyle == style);
                    CHECK(routing.connectorActions.value(style)->isChecked());
                    bool hasCurve = false;
                    for (auto edge = routing.edges.cbegin(); edge != routing.edges.cend(); ++edge) {
                        const auto path = edge.value()->path();
                        const auto painted = stroke.createStroke(path);
                        const QString parent = routing.parents.value(edge.key());
                        for (int i = 0; i < path.elementCount(); ++i)
                            hasCurve |= path.elementAt(i).isCurveTo();
                        for (auto node = routing.items.cbegin(); node != routing.items.cend(); ++node) {
                            if (node.key() == parent || node.key() == edge.key()) continue;
                            CHECK(!painted.intersects(node->body->sceneBoundingRect().adjusted(2, 2, -2, -2)));
                        }
                        for (auto other = routing.edges.cbegin(); other != edge; ++other) {
                            const QString otherParent = routing.parents.value(other.key());
                            if (parent == otherParent || parent == other.key() || otherParent == edge.key()) continue;
                            CHECK(!painted.intersects(stroke.createStroke(other.value()->path())));
                        }
                    }
                    if (style == ConnectorStyle::Curved) CHECK(hasCurve);
                    if (style == ConnectorStyle::Straight || style == ConnectorStyle::Orthogonal) CHECK(!hasCurve);
                }
            }
        }
        routing.history(true);
        CHECK(routing.doc.connectorStyle == ConnectorStyle::Curved);
        routing.chooseLayout(true);
        CHECK(routing.visibleRootLane(first) != routing.visibleRootLane(second));
        routing.moveNode(first, second, -1, true);
        CHECK(routing.visibleRootLane(first) == BranchDirection::Left &&
              routing.visibleRootLane(second) == BranchDirection::Left);
        routing.history(true);
        CHECK(routing.visibleRootLane(first) == BranchDirection::Right);
    }

    window.selectNode(first);
    window.toggleCollapse();
    CHECK(!window.items.contains(child));
    window.chooseLayout(false);
    CHECK(!window.items.contains(child));
    window.toggleCollapse();
    CHECK(window.items.contains(child));
    window.fit();
    const QPoint childPoint = window.canvas->mapFromScene(window.items.value(child).body->pos());
    const auto targetBody = window.items.value(second).body->sceneBoundingRect();
    const QPoint targetPoint = window.canvas->mapFromScene(QPointF(targetBody.center().x(), targetBody.top())) - QPoint(0, 8);
    CHECK(window.nodeAt(targetPoint).isEmpty());
    QTest::mousePress(window.canvas->viewport(), Qt::LeftButton, Qt::NoModifier, childPoint);
    const auto snapped = window.snapDrop(targetPoint, false);
    CHECK(snapped.id == second && snapped.side == 0);
    CHECK(window.snapDrop(targetPoint - QPoint(0, 10), false).id != second);
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

    // Terminal siblings are stacked in the structure view, so their leading edge is above.
    window.fit();
    const auto edgeTarget = window.items.value(child).body->sceneBoundingRect();
    const QPoint beforeChild = window.canvas->mapFromScene(QPointF(
        edgeTarget.center().x(), edgeTarget.top() + edgeTarget.height() * 0.15));
    QTest::mousePress(window.canvas->viewport(), Qt::LeftButton, Qt::NoModifier,
                      window.canvas->mapFromScene(window.items.value(sibling).body->pos()));
    QTest::mouseRelease(window.canvas->viewport(), Qt::LeftButton, Qt::NoModifier, beforeChild);
    CHECK(window.doc.nodes.value(first).children == (QStringList{sibling, child}));
    window.history(true);
    CHECK(window.doc.nodes.value(first).children == (QStringList{child, sibling}));

    // Ctrl-drag clones the branch without moving the original.
    window.fit();
    auto pointFor = [&](const QString &id) {
        return window.canvas->mapFromScene(window.items.value(id).body->pos());
    };
    QTest::mousePress(window.canvas->viewport(), Qt::LeftButton, Qt::ControlModifier, pointFor(child));
    QTest::mouseRelease(window.canvas->viewport(), Qt::LeftButton, Qt::ControlModifier, pointFor(second));
    CHECK(window.parents.value(child) == first && window.parents.value(window.selected) == second);
    CHECK(window.selected != child && window.doc.nodes.value(window.selected).text == window.doc.nodes.value(child).text);
    window.history(true);
    CHECK(window.doc.nodes.size() == 7);

    CHECK(window.items.value(child).label->document()->defaultTextOption().alignment() == Qt::AlignHCenter);
    window.replaceSelection({child, sibling});
    bool styleDialogOperated = false;
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("topicStyleDialog");
        if (!dialog) return;
        auto *align = dialog->findChild<QCheckBox *>("applyAlignment");
        auto *points = dialog->findChild<QCheckBox *>("applyPoints");
        auto *alignment = dialog->findChild<QComboBox *>("topicStyleAlignment");
        auto *size = dialog->findChild<QSpinBox *>("topicStylePoints");
        if (!align || !points || !alignment || !size) { dialog->reject(); return; }
        align->setChecked(true);
        points->setChecked(true);
        alignment->setCurrentIndex(2);
        size->setValue(16);
        styleDialogOperated = true;
        dialog->accept();
    });
    window.changeTopicStyles();
    CHECK(styleDialogOperated);
    for (const auto &id : {child, sibling}) {
        CHECK(window.doc.nodes.value(id).style.alignment == TextAlignment::Right);
        CHECK(window.items.value(id).label->document()->defaultTextOption().alignment() == Qt::AlignRight);
        CHECK(window.items.value(id).label->font().pointSize() == 16);
    }
    CHECK(window.doc.nodes.value(second).style == TopicStyle());
    Document styled;
    CHECK(styled.parse(window.doc.serialize()) && styled.nodes.value(child).style == window.doc.nodes.value(child).style);
    window.history(true);
    CHECK(window.doc.nodes.value(child).style == TopicStyle() && window.doc.nodes.value(sibling).style == TopicStyle());
    window.activateWindow();
    window.canvas->setFocus();
    QTest::qWait(60);

    window.selectNode(child);
    QTest::mouseClick(window.canvas->viewport(), Qt::LeftButton, Qt::ControlModifier, pointFor(sibling));
    CHECK(window.selectedRoots(true) == (QStringList{child, sibling}));
    CHECK(waitForClipboard([&] { window.copyBranch(); }, [&](const QMimeData &mime) {
        const QString text = mime.text();
        return mime.data(BranchClipboard::mimeType()).startsWith("zhili-forest") &&
            text.contains(window.doc.nodes.value(child).text) && text.contains(window.doc.nodes.value(sibling).text);
    }));
    window.selectNode(second);
    window.pasteBranch();
    const auto copiedChildren = window.doc.nodes.value(second).children;
    CHECK(copiedChildren.size() == 4);
    CHECK(window.doc.nodes.value(copiedChildren.at(2)).text == window.doc.nodes.value(child).text);
    CHECK(window.doc.nodes.value(copiedChildren.at(3)).text == window.doc.nodes.value(sibling).text);
    window.history(true);
    CHECK(window.doc.nodes.size() == 7);

    // Search visits matches in tree order, expands a collapsed path, and wraps.
    window.selectNode(first);
    window.toggleCollapse();
    CHECK(!window.items.contains(child));
    window.activateWindow();
    if (QApplication::platformName() == "windows")
        CHECK(QTest::qWaitForWindowActive(&window, 1000));
    window.canvas->setFocus();
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

    // Long topics must remain complete on the canvas and in exported files.
    // The emoji straddles the old 360 UTF-16-unit cutoff.
    {
        Window longTopic(false);
        longTopic.show();
        longTopic.activateWindow();
        QTest::qWait(30);
        const QString text = QString(359, QChar(0x679D)) + QString::fromUtf8("🌱")
            + QStringLiteral("\n完整内容，不能截断。\n").repeated(20)
            + QStringLiteral("EXPORT_END_MARKER");
        const QString id = longTopic.doc.root;
        longTopic.startEdit();
        longTopic.editor->setPlainText(text);
        CHECK(longTopic.finishEdit());
        CHECK(longTopic.items.value(id).label->toPlainText() == text);
        longTopic.chooseLayout(false);
        CHECK(longTopic.items.value(id).label->toPlainText() == text);
        const QString fullPath = temp.filePath("long-topic.smap");
        CHECK(longTopic.saveTo(fullPath));
        CHECK(longTopic.loadFrom(fullPath, &error));
        CHECK(longTopic.items.value(id).label->toPlainText() == text);
        for (const QString &format : {QString("png"), QString("svg"), QString("pdf")}) {
            const QString outputPath = temp.filePath("long-topic." + format);
            error.clear();
            CHECK(longTopic.exportTo(outputPath, format, &error));
            if (format == "svg") {
                QFile output(outputPath);
                CHECK(output.open(QIODevice::ReadOnly));
                QXmlStreamReader xml(&output);
                QString exportedText;
                while (!xml.atEnd()) {
                    xml.readNext();
                    if (xml.isStartElement() && xml.name() == QStringLiteral("text"))
                        exportedText += xml.readElementText();
                }
                CHECK(!xml.hasError());
                CHECK(exportedText.contains(QStringLiteral("EXPORT_END_MARKER")));
                CHECK(exportedText.contains(QString::fromUtf8("🌱")));
            }
        }
        longTopic.fit();
        longTopic.startEdit();
        CHECK(longTopic.canvas->transform().m11() >= 0.65);
        CHECK(longTopic.editorProxy->size().height() <= 320);
        CHECK(longTopic.editor->toPlainText() == text);
        const QByteArray beforeOversize = longTopic.doc.serialize();
        const int historySize = longTopic.undoStack.size();
        const QString tooLong(Document::maxText + 1, QLatin1Char('x'));
        longTopic.editor->setPlainText(tooLong);
        CHECK(!longTopic.finishEdit());
        CHECK(longTopic.editor && longTopic.editor->toPlainText() == tooLong);
        CHECK(longTopic.doc.serialize() == beforeOversize && longTopic.undoStack.size() == historySize);
        CHECK(!longTopic.confirmLeave());
        longTopic.classicAction->trigger();
        CHECK(!longTopic.doc.mindMap && longTopic.editor);
        CHECK(longTopic.hierarchyAction->isChecked() && !longTopic.classicAction->isChecked());
        CHECK(!longTopic.close() && longTopic.isVisible());
        // A correction can be committed normally, and an explicit cancel still works.
        longTopic.editor->setPlainText(text + QStringLiteral(" 已修订"));
        CHECK(longTopic.finishEdit());
        CHECK(longTopic.doc.nodes.value(id).text.endsWith(QStringLiteral("已修订")));
        longTopic.history(true);
        CHECK(longTopic.doc.nodes.value(id).text == text);
        longTopic.startEdit();
        longTopic.editor->setPlainText(tooLong);
        CHECK(longTopic.finishEdit(false));
        CHECK(!longTopic.editor && longTopic.doc.nodes.value(id).text == text);

        // Exercise the supported boundary, including a linebreak-heavy topic.
        // Giant raster output must fail without replacing an existing file.
        for (const QString &maximum : {QString(Document::maxText, QChar(0x679D)),
                                      QStringLiteral("行\n").repeated(Document::maxText / 2)}) {
            QElapsedTimer timer;
            timer.start();
            longTopic.startEdit();
            longTopic.editor->setPlainText(maximum);
            CHECK(longTopic.finishEdit());
            CHECK(longTopic.items.value(id).label->toPlainText() == maximum);
            qInfo() << "Maximum-length topic render:" << timer.elapsed() << "ms"
                    << longTopic.items.value(id).label->boundingRect() << longTopic.scene->itemsBoundingRect();
            const QString guardedPath = temp.filePath("guarded-long-topic.png");
            const QByteArray originalBytes("existing export must survive");
            CHECK(writeAtomically(guardedPath, originalBytes, &error));
            error.clear();
            CHECK(!longTopic.exportTo(guardedPath, "png", &error));
            CHECK(!error.isEmpty() && longTopic.selected == id);
            QFile originalOutput(guardedPath);
            CHECK(originalOutput.open(QIODevice::ReadOnly) && originalOutput.readAll() == originalBytes);
            // Restore a manageable topic before the next editor opens.
            longTopic.history(true);
            CHECK(longTopic.doc.nodes.value(id).text == text);
        }
    }

    // Broken UTF-16 from a clipboard/plugin must neither commit an unreopenable
    // document nor replace/delete the previous valid recovery snapshot.
    {
        Window unicode(false);
        const QString unicodeRoot = unicode.doc.root;
        const QByteArray originalDocument = unicode.doc.serialize();
        unicode.recoveryPath = temp.filePath("unicode-recovery.json");
        unicode.startEdit();
        unicode.editor->setPlainText(QString(QChar(0xd800)));
        unicode.writeRecovery();
        CHECK(!QFile::exists(unicode.recoveryPath));
        const QString validDraft = QStringLiteral("有效草稿 ") + QString::fromUtf8("🌱");
        unicode.editor->setPlainText(validDraft);
        unicode.writeRecovery();
        QFile snapshot(unicode.recoveryPath);
        CHECK(snapshot.open(QIODevice::ReadOnly));
        const QByteArray lastValidSnapshot = snapshot.readAll();
        snapshot.close();
        Document draft;
        CHECK(draft.parse(QJsonDocument(QJsonDocument::fromJson(lastValidSnapshot).object()
            .value("document").toObject()).toJson(), &error));
        CHECK(draft.nodes.value(unicodeRoot).text == validDraft);
        const QStringList invalidText{
            QString(QChar(0xd800)), QString(QChar(0xdc00)),
            QStringLiteral("前文") + QChar(0xd800) + QStringLiteral("后文"),
            QString(Document::maxText + 1, QLatin1Char('x'))
        };
        for (const QString &value : invalidText) {
            unicode.editor->setPlainText(value);
            CHECK(unicode.editor->toPlainText() == value);
            CHECK(!unicode.finishEdit() && unicode.editor);
            CHECK(!unicode.save() && !unicode.confirmLeave());
            CHECK(unicode.editor->toPlainText() == value && unicode.doc.serialize() == originalDocument);
            CHECK(unicode.undoStack.isEmpty());
            unicode.writeRecovery();
            CHECK(snapshot.open(QIODevice::ReadOnly) && snapshot.readAll() == lastValidSnapshot);
            snapshot.close();
        }
        unicode.editor->setPlainText(validDraft);
        CHECK(unicode.finishEdit() && !unicode.editor);
        CHECK(unicode.doc.nodes.value(unicodeRoot).text == validDraft);
        const QString unicodePath = temp.filePath("unicode-valid.smap");
        CHECK(unicode.saveTo(unicodePath));
        Document reopened;
        CHECK(reopened.readFile(unicodePath, &error) && reopened.serialize() == unicode.doc.serialize());
        unicode.startEdit();
        unicode.editor->setPlainText(QString(QChar(0xdc00)));
        CHECK(unicode.finishEdit(false) && !unicode.editor);
        CHECK(unicode.doc.nodes.value(unicodeRoot).text == validDraft);
    }

    // Clipboard shortcuts operate on whole branches, but remain ordinary text
    // shortcuts while the topic editor is active.
    {
        Window branches(false);
        branches.show();
        branches.activateWindow();
        branches.canvas->setFocus();
        QTest::qWait(30);
        const QString branchRoot = branches.doc.root;
        branches.createNode(true);
        branches.editor->setPlainText(QStringLiteral("分支🌱\n完整内容"));
        CHECK(branches.finishEdit());
        const QString source = branches.selected;
        branches.createNode(true);
        branches.editor->setPlainText(QStringLiteral("子主题"));
        CHECK(branches.finishEdit());
        branches.selectNode(source);
        branches.toggleCollapse();
        const QByteArray beforeCopy = branches.doc.serialize();
        branches.activateWindow();
        branches.canvas->setFocus();
        if (QApplication::platformName() == "windows")
            CHECK(QTest::qWaitForWindowActive(&branches, 1000));
        CHECK(waitForClipboard(
            [&] { QTest::keyClick(branches.canvas, Qt::Key_C, Qt::ControlModifier); },
            [](const QMimeData &mime) {
                return mime.hasFormat(BranchClipboard::mimeType()) &&
                    mime.text().contains(QStringLiteral("子主题"));
            }));
        CHECK(branches.doc.serialize() == beforeCopy);
        branches.selectNode(branchRoot);
        const int historyBeforePaste = branches.undoStack.size();
        QTest::keyClick(branches.canvas, Qt::Key_V, Qt::ControlModifier);
        const QString pasted = branches.selected;
        CHECK(pasted != source && branches.doc.nodes.size() == 5);
        CHECK(branches.doc.nodes.value(pasted).text == branches.doc.nodes.value(source).text);
        CHECK(branches.doc.nodes.value(pasted).collapsed);
        CHECK(branches.undoStack.size() == historyBeforePaste + 1);
        branches.history(true);
        CHECK(branches.doc.serialize() == beforeCopy);
        branches.history(false);
        CHECK(branches.doc.nodes.contains(pasted));
        QTest::keyClick(branches.canvas, Qt::Key_X, Qt::ControlModifier);
        CHECK(!branches.doc.nodes.contains(pasted) && branches.doc.nodes.size() == 3);
        branches.history(true);
        CHECK(branches.doc.nodes.contains(pasted));
        branches.selectNode(branchRoot);
        const QByteArray beforeRootCut = branches.doc.serialize();
        const QByteArray clipboardBeforeRootCut = QApplication::clipboard()->mimeData()->data(BranchClipboard::mimeType());
        QTest::keyClick(branches.canvas, Qt::Key_X, Qt::ControlModifier);
        CHECK(branches.doc.serialize() == beforeRootCut);
        CHECK(QApplication::clipboard()->mimeData()->data(BranchClipboard::mimeType()) == clipboardBeforeRootCut);
        branches.selectNode(pasted);
        QTest::keyClick(branches.canvas, Qt::Key_Up, Qt::ControlModifier | Qt::ShiftModifier);
        CHECK(branches.doc.nodes.value(branchRoot).children == (QStringList{pasted, source}));
        QTest::keyClick(branches.canvas, Qt::Key_Down, Qt::ControlModifier | Qt::ShiftModifier);
        CHECK(branches.doc.nodes.value(branchRoot).children == (QStringList{source, pasted}));
        const int historyAtEdge = branches.undoStack.size();
        QTest::keyClick(branches.canvas, Qt::Key_Down, Qt::ControlModifier | Qt::ShiftModifier);
        CHECK(branches.undoStack.size() == historyAtEdge);
        branches.history(true);
        branches.history(true);
        QTest::keyClick(branches.canvas, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        CHECK(branches.doc.nodes.value(branchRoot).children == (QStringList{pasted, source}));
        QTest::keyClick(branches.canvas, Qt::Key_Y, Qt::ControlModifier);
        CHECK(branches.doc.serialize() == beforeRootCut);
        branches.selectNode(branchRoot);
        const QString clipboardOutline = QStringLiteral("计划\n\t需求\n\t开发\n验收");
        CHECK(waitForClipboard([&] { QApplication::clipboard()->setText(clipboardOutline); },
                               [&](const QMimeData &mime) { return mime.text() == clipboardOutline; }));
        QTest::keyClick(branches.canvas, Qt::Key_V, Qt::ControlModifier);
        CHECK(branches.doc.nodes.size() == 9);
        CHECK(branches.doc.nodes.value(branches.selected).text == QStringLiteral("计划"));
        CHECK(branches.doc.nodes.value(branches.selected).children.size() == 2);
        branches.history(true);
        CHECK(branches.doc.serialize() == beforeRootCut);
        CHECK(waitForClipboard([] {
            auto *malformed = new QMimeData;
            malformed->setData(BranchClipboard::mimeType(), "not valid JSON");
            malformed->setText(QStringLiteral("不可静默回退到文本"));
            QApplication::clipboard()->setMimeData(malformed);
        }, [](const QMimeData &mime) { return mime.data(BranchClipboard::mimeType()) == "not valid JSON"; }));
        const int historyBeforeFailure = branches.undoStack.size();
        QTest::keyClick(branches.canvas, Qt::Key_V, Qt::ControlModifier);
        CHECK(branches.doc.serialize() == beforeRootCut && branches.undoStack.size() == historyBeforeFailure);
        branches.fit();
        const QPoint atSource = branches.canvas->mapFromScene(branches.items.value(source).body->pos());
        QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, atSource,
            branches.canvas->viewport()->mapToGlobal(atSource));
        QCoreApplication::sendEvent(branches.canvas->viewport(), &contextEvent);
        auto *contextMenu = branches.findChild<QMenu *>("topicContextMenu");
        CHECK(contextMenu && contextMenu->isVisible() && branches.selected == source);
        CHECK(branches.cutAction->isEnabled() && branches.removeAction->isEnabled());
        QTest::keyClick(contextMenu, Qt::Key_Escape);
        CHECK(!contextMenu->isVisible() && branches.doc.serialize() == beforeRootCut);
        branches.selectNode(branchRoot);
        CHECK(!branches.cutAction->isEnabled() && !branches.removeAction->isEnabled() && branches.siblingAction->isEnabled());
        branches.activateWindow();
        branches.canvas->setFocus();
        QTest::qWait(30);
        branches.startEdit();
        branches.editor->setPlainText(QStringLiteral("编辑器文本"));
        branches.editor->selectAll();
        CHECK(!branches.findChild<QAction *>("copyBranchAction")->isEnabled());
        CHECK(waitForClipboard(
            [&] { QTest::keyClick(branches.editor, Qt::Key_C, Qt::ControlModifier); },
            [](const QMimeData &mime) {
                return mime.text() == QStringLiteral("编辑器文本") && !mime.hasFormat(BranchClipboard::mimeType());
            }));
        CHECK(branches.finishEdit(false));
        CHECK(branches.findChild<QAction *>("copyBranchAction")->isEnabled());
        CHECK(branches.doc.serialize() == beforeRootCut);
    }

    // Symbols are an undoable, persistent part of the topic, never file paths.
    {
        qInfo() << "Symbol UI checks";
        Window symbols(false);
        const QString symbolRoot = symbols.doc.root;
        CHECK(!SymbolLibrary::assets().isEmpty());
        const QString symbolId = SymbolLibrary::assets().first().id;
        symbols.setSelectedSymbol(symbolId);
        CHECK(symbols.doc.nodes.value(symbolRoot).symbolId == symbolId);
        CHECK(symbols.items.value(symbolRoot).symbol && symbols.items.value(symbolRoot).symbol->isVisible());
        CHECK(symbols.items.value(symbolRoot).body->sceneBoundingRect().contains(
            symbols.items.value(symbolRoot).label->sceneBoundingRect()));
        CHECK(symbols.items.value(symbolRoot).body->sceneBoundingRect().contains(
            symbols.items.value(symbolRoot).symbol->sceneBoundingRect()));
        const QString symbolPath = temp.filePath("symbol.smap");
        CHECK(symbols.saveTo(symbolPath));
        CHECK(symbols.loadFrom(symbolPath, &error));
        CHECK(symbols.doc.nodes.value(symbolRoot).symbolId == symbolId);
        const QByteArray copiedSymbol = BranchClipboard::encode(symbols.doc, {symbolRoot}, &error);
        CHECK(!copiedSymbol.isEmpty() && waitForClipboard([&] { symbols.copyBranch(); },
            [&](const QMimeData &mime) { return mime.data(BranchClipboard::mimeType()) == copiedSymbol; }));
        Window destination(false);
        destination.pasteBranch();
        CHECK(destination.doc.nodes.value(destination.selected).symbolId == symbolId);
        const QByteArray beforePicker = symbols.doc.serialize();
        QTimer::singleShot(0, &symbols, [&] {
            auto *dialog = symbols.findChild<QDialog *>("symbolPickerDialog");
            if (dialog) dialog->reject();
        });
        symbols.chooseSymbol();
        qInfo() << "Symbol picker cancelled";
        CHECK(symbols.doc.serialize() == beforePicker && !symbols.dirty);
        bool removed = false;
        QTimer::singleShot(0, &symbols, [&] {
            auto *dialog = symbols.findChild<QDialog *>("symbolPickerDialog");
            if (!dialog) return;
            auto *remove = dialog->findChild<QPushButton *>("symbolRemove");
            if (!remove) { dialog->reject(); return; }
            removed = true;
            remove->click();
        });
        symbols.chooseSymbol();
        qInfo() << "Symbol picker applied";
        CHECK(removed && symbols.doc.nodes.value(symbolRoot).symbolId.isEmpty());
        CHECK(!symbols.items.value(symbolRoot).symbol->isVisible());
        symbols.history(true);
        CHECK(symbols.doc.serialize() == beforePicker && symbols.items.value(symbolRoot).symbol->isVisible());
        for (const QString &format : {QString("png"), QString("svg"), QString("pdf")}) {
            const QString output = temp.filePath("symbols." + format);
            CHECK(symbols.exportTo(output, format, &error));
            if (format == "svg") {
                QFile svg(output);
                CHECK(svg.open(QIODevice::ReadOnly));
                CHECK(!svg.readAll().contains("<image"));
            }
        }
        const QString pptxPath = temp.filePath("symbols.pptx");
        CHECK(symbols.exportPowerPointTo(pptxPath, &error));
        QFile pptxOutput(pptxPath);
        CHECK(pptxOutput.open(QIODevice::ReadOnly));
        const QByteArray package = pptxOutput.readAll();
        CHECK(package.startsWith("PK") && package.contains("ppt/media/"));
        CHECK(symbols.doc.serialize() == beforePicker && !symbols.dirty);
        symbols.doc.nodes[symbolRoot].symbolId = QStringLiteral("future-unknown-symbol");
        symbols.render();
        CHECK(!symbols.items.value(symbolRoot).symbol->isVisible());
        CHECK(symbols.doc.nodes.value(symbolRoot).symbolId == QStringLiteral("future-unknown-symbol"));
        CHECK(!symbols.exportPowerPointTo(temp.filePath("unknown-symbol.pptx"), &error));
        CHECK(!QFile::exists(temp.filePath("unknown-symbol.pptx")));
    }

    // Template selection is cancel-safe and starts a new unsaved document.
    {
        qInfo() << "Template UI checks";
        Window templates(false);
        const QByteArray blank = templates.doc.serialize();
        QTimer::singleShot(0, &templates, [&] {
            auto *dialog = templates.findChild<QDialog *>("templateDialog");
            if (dialog) dialog->reject();
        });
        templates.newFromTemplate();
        qInfo() << "Template picker cancelled";
        CHECK(templates.doc.serialize() == blank && !templates.dirty);
        bool chosen = false;
        QTimer::singleShot(0, &templates, [&] {
            auto *dialog = templates.findChild<QDialog *>("templateDialog");
            if (!dialog) return;
            auto *list = dialog->findChild<QListWidget *>("templateList");
            auto *create = dialog->findChild<QPushButton *>("templateCreateButton");
            if (!list || !create || !list->count()) { dialog->reject(); return; }
            list->setCurrentRow(0);
            chosen = true;
            create->click();
        });
        templates.newFromTemplate();
        qInfo() << "Template picker applied";
        CHECK(chosen && templates.doc.nodes.size() > 1 && templates.dirty);
        CHECK(templates.filePath.isEmpty() && templates.needsSaveAs);
        CHECK(templates.undoStack.isEmpty());
        const QByteArray unsavedTemplate = templates.doc.serialize();
        bool cancelledLeave = false;
        QTimer cancelLeaveTimer;
        QObject::connect(&cancelLeaveTimer, &QTimer::timeout, &templates, [&] {
            auto *warning = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (warning) {
                cancelledLeave = true;
                cancelLeaveTimer.stop();
                warning->button(QMessageBox::Cancel)->click();
            }
        });
        cancelLeaveTimer.start(10);
        QTimer::singleShot(0, &templates, [&] {
            auto *dialog = templates.findChild<QDialog *>("templateDialog");
            if (!dialog) return;
            auto *list = dialog->findChild<QListWidget *>("templateList");
            auto *create = dialog->findChild<QPushButton *>("templateCreateButton");
            if (!list || !create || list->count() < 2) { dialog->reject(); return; }
            list->setCurrentRow(1);
            create->click();
        });
        templates.newFromTemplate();
        cancelLeaveTimer.stop();
        qInfo() << "Template replacement cancelled";
        CHECK(cancelledLeave && templates.doc.serialize() == unsavedTemplate && templates.dirty);
        const QString templatePath = temp.filePath("new-template.smap");
        CHECK(templates.saveTo(templatePath) && !templates.dirty && !templates.needsSaveAs);
        CHECK(templates.loadFrom(templatePath, &error) && templates.doc.serialize() == unsavedTemplate);
        CHECK(templates.findChild<QAction *>("importMindMapAction"));
        CHECK(templates.findChild<QAction *>("exportPptxAction"));
    }

    // Import creates an editable copy without binding Save to the source file.
    {
        Window imported(false);
        const QString sourcePath = QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().filePath("fixtures/ui-import.mmap");
        QFile sourceFile(sourcePath);
        CHECK(sourceFile.open(QIODevice::ReadOnly));
        const QByteArray original = sourceFile.readAll();
        sourceFile.close();
        QStringList warnings;
        CHECK(imported.importFrom(sourcePath, &warnings, &error));
        CHECK(imported.doc.nodes.size() == 4 && !warnings.isEmpty());
        CHECK(imported.dirty && imported.needsSaveAs && imported.filePath.isEmpty());
        CHECK(imported.doc.nodes.value(imported.doc.root).text.contains(QString::fromUtf8("🌿")));
        const QByteArray beforeCancelledImport = imported.doc.serialize();
        bool importCancelled = false;
        // Qt 5.12's offscreen backend can dispatch a zero-delay callback before
        // the modal widget becomes active. Wait for this exact dialog instead.
        QTimer importCancelTimer;
        importCancelTimer.setInterval(10);
        QObject::connect(&importCancelTimer, &QTimer::timeout, &imported, [&] {
            auto *dialog = imported.findChild<QMessageBox *>();
            if (dialog && dialog->isVisible()) {
                importCancelled = true;
                importCancelTimer.stop();
                dialog->button(QMessageBox::Cancel)->click();
            }
        });
        importCancelTimer.start();
        CHECK(!imported.importFrom(sourcePath, &warnings, &error));
        importCancelTimer.stop();
        CHECK(importCancelled && error.isEmpty() && imported.doc.serialize() == beforeCancelledImport);
        const QString broken = temp.filePath("broken.mmap");
        CHECK(writeAtomically(broken, "not a map", &error));
        CHECK(!imported.importFrom(broken, &warnings, &error));
        CHECK(!error.isEmpty() && imported.doc.serialize() == beforeCancelledImport && imported.dirty);
        const QString nativeCopy = temp.filePath("imported-copy.smap");
        CHECK(imported.saveTo(nativeCopy));
        CHECK(imported.filePath == nativeCopy && !imported.needsSaveAs && !imported.dirty);
        CHECK(sourceFile.open(QIODevice::ReadOnly) && sourceFile.readAll() == original);
    }

    const QString previewDir = qEnvironmentVariable("ZHILI_PREVIEW_DIR");
    // Preserve an ultrawide-sized workspace, but keep restored windows usable
    // after moving to a smaller screen or disconnecting a monitor.
    {
        const QRect ultrawide(0, 0, 5120, 2160);
        const QRect laptop(0, 0, 1366, 768);
        const QRect leftScreen(-3440, 0, 3440, 1440);
        CHECK(boundedWindowGeometry(QRect(120, 80, 4500, 1800), ultrawide, QSize(360, 280))
            == QRect(120, 80, 4500, 1800));
        CHECK(laptop.contains(boundedWindowGeometry(QRect(3800, 200, 2300, 1200), laptop, QSize(360, 280))));
        CHECK(leftScreen.contains(boundedWindowGeometry(QRect(-4000, -300, 1800, 1000), leftScreen, QSize(360, 280))));
        QSettings().setValue("window/geometry", QByteArray("invalid geometry"));
        Window remembered(false);
        remembered.show();
        remembered.resize(QSize(620, 420).boundedTo(available).expandedTo(remembered.minimumSize()));
        QTest::qWait(30);
        const QSize requested = remembered.size();
        CHECK(remembered.close());
        const QByteArray savedGeometry = QSettings().value("window/geometry").toByteArray();
        CHECK(!savedGeometry.isEmpty() && savedGeometry != QByteArray("invalid geometry"));
        Window restored(false);
        restored.show();
        QTest::qWait(30);
        CHECK(restored.size() == requested);
        restored.startEdit();
        restored.editor->setPlainText(QString(Document::maxText + 1, QLatin1Char('x')));
        CHECK(!restored.close());
        CHECK(QSettings().value("window/geometry").toByteArray() == savedGeometry);
        CHECK(restored.finishEdit(false));
        restored.showMaximized();
        QTest::qWait(30);
        CHECK(restored.close());
        Window maximized(false);
        CHECK(maximized.isMaximized());
        QSettings().remove("window/geometry");
    }
    const auto toolbar = window.findChild<QToolBar *>("wideToolbar");
    const auto compact = window.findChild<QToolBar *>("compactToolbar");
    const auto zoomLevel = window.findChild<QLabel *>("zoomLevel");
    const auto zoomIn = window.findChild<QToolButton *>("zoomIn");
    const auto zoomOut = window.findChild<QToolButton *>("zoomOut");
    CHECK(toolbar && compact && zoomLevel && zoomIn && zoomOut);
    const auto convert = toolbar->findChild<QToolButton *>("workspaceConvert");
    const auto convertAction = window.findChild<QAction *>("convertFileAction");
    CHECK(!QApplication::windowIcon().pixmap(24, 24).isNull());
    CHECK(convert && convertAction && convert->defaultAction() == convertAction && !convert->icon().isNull());

    // Windows can clamp requested dimensions to the scaled desktop work area.
    const auto toolbarMatchesWindow = [&] {
        const bool roomy = window.width() >= qMax(840, toolbar->sizeHint().width() + 24);
        return toolbar->isVisible() == roomy && compact->isVisible() != roomy;
    };
    window.resize(1160, 760);
    QTest::qWait(60);
    CHECK(toolbarMatchesWindow());
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

    bool dialogOperated = false, appearanceChecked = true;
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
        const QFontDatabase database;
        const auto chinese = database.families(QFontDatabase::SimplifiedChinese)
            + database.families(QFontDatabase::TraditionalChinese);
        for (const auto *list : {interfaceFamily, topicFamily}) {
            for (int i = 0; i < embedded.size(); ++i) {
                if (list->itemData(i + 1).toString() != embedded.at(i)) appearanceChecked = false;
            }
            bool nonChineseSeen = false;
            for (int i = embedded.size() + 1; i < list->count(); ++i) {
                if (!chinese.contains(list->itemData(i).toString())) nonChineseSeen = true;
                else if (nonChineseSeen) appearanceChecked = false;
            }
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
    CHECK(dialogOperated && appearanceChecked);
    QTest::qWait(60);
    CHECK(window.dirty == wasDirty && compact->isVisible());
    int visibleControls = 0;
    for (auto *button : compact->findChildren<QToolButton *>()) {
        if (button->accessibleName().isEmpty()) continue;
        CHECK(button->isVisible() && compact->rect().contains(button->geometry()));
        ++visibleControls;
    }
    CHECK(visibleControls == 5);
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
    CHECK(toolbarMatchesWindow());
    if (!previewDir.isEmpty()) {
        CHECK(writeAtomically(QDir(previewDir).filePath(QStringLiteral("研发项目交付.smap")), structure, &error));
        window.fit();
        QTest::qWait(100);
        CHECK(window.grab().save(QDir(previewDir).filePath("hierarchy.png")));
        window.chooseLayout(true);
        window.fit();
        QTest::qWait(100);
        CHECK(window.grab().save(QDir(previewDir).filePath("mindmap.png")));

        // A useful, original built-in example for visual and Office acceptance,
        // rather than only synthetic numbered topics.
        Document research;
        CHECK(zhili::loadBuiltInTemplate("architecture-research-platform", &research, &error));
        const QMap<QString, QString> icons{
            {QStringLiteral("多类型科研平台架构"), "command-control-center"},
            {QStringLiteral("空中平台分类资料"), "air-quadcopter"},
            {QStringLiteral("水面与水下平台资料"), "underwater-auv"},
            {QStringLiteral("地面与专用平台资料"), "ground-four-wheel-rover"},
            {QStringLiteral("通信连接抽象"), "communications-antenna"},
            {QStringLiteral("设备与接口资料"), "support-battery"},
            {QStringLiteral("质量检查与标注"), "sensing-camera"}
        };
        for (Node &node : research.nodes) {
            node.symbolId = icons.value(node.text);
            CHECK(node.symbolId.isEmpty() || SymbolLibrary::find(node.symbolId));
        }
        research.mindMap = true;
        Window showcase(false);
        showcase.appearance.topicFont.points = 14;
        showcase.applyAppearance();
        showcase.adoptUnsavedDocument(research);
        showcase.resize(1920, 1080);
        showcase.show();
        QTest::qWait(60);
        showcase.fit();
        const QString exampleBase = QDir(previewDir).filePath(QStringLiteral("多类型科研平台架构"));
        CHECK(showcase.saveTo(exampleBase + ".smap"));
        CHECK(showcase.exportPowerPointTo(exampleBase + ".pptx", &error));
        CHECK(showcase.exportTo(exampleBase + ".svg", "svg", &error));
        CHECK(showcase.grab().save(QDir(previewDir).filePath("research-platform-workspace.png")));
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
    {
        const QString icon = SymbolLibrary::assets().first().id;
        for (int i = 0; i < 3000; i += 3) window.doc.nodes[QString::number(i)].symbolId = icon;
        QElapsedTimer timer;
        timer.start();
        window.render();
        qInfo() << "3000 nodes with 1000 symbols: render" << timer.elapsed() << "ms";
        CHECK(window.items.value("0").symbol && window.items.value("3").symbol);
        CHECK(window.items.value("3").symbol->renderer() == window.items.value("6").symbol->renderer());
        for (int i = 0; i < 3000; i += 3) {
            const NodeItem item = window.items.value(QString::number(i));
            CHECK(item.symbol->isVisible() && item.body->sceneBoundingRect().contains(item.symbol->sceneBoundingRect()));
        }
    }
    qInfo() << "ZhiLi smoke test passed";
    return 0;
#undef CHECK
}
