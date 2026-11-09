#include "appearance.h"
#include "document.h"
#include "layout.h"

#include <QtWidgets>
#include <QtSvg/QSvgGenerator>
#include <QPdfWriter>
#include <QtMath>
#include <functional>
#include <memory>

namespace {
class Canvas : public QGraphicsView {
public:
    explicit Canvas(QGraphicsScene *scene, QWidget *parent) : QGraphicsView(scene, parent) {
        setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        setDragMode(ScrollHandDrag);
        setViewportUpdateMode(BoundingRectViewportUpdate);
        setTransformationAnchor(AnchorUnderMouse);
    }

    std::function<bool(QPoint)> press;
    std::function<bool(QPoint)> doubleClick;
    std::function<void(QPoint)> drop;
    std::function<void()> zoomChanged;
    std::function<void()> viewportResized;

    void zoomBy(qreal factor) {
        const qreal current = transform().m11();
        const qreal next = qBound(qMin(0.05, current), current * factor, 3.5);
        scale(next / current, next / current);
        zoomChanged();
    }

protected:
    void wheelEvent(QWheelEvent *event) override {
        if (!(event->modifiers() & Qt::ControlModifier)) return QGraphicsView::wheelEvent(event);
        const int delta = event->angleDelta().y();
        if (delta != 0) zoomBy(qPow(1.12, delta / 120.0));
        event->accept();
    }

    void resizeEvent(QResizeEvent *event) override {
        QGraphicsView::resizeEvent(event);
        if (viewportResized) viewportResized();
    }

    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            origin = event->pos();
            nodeDrag = press(origin);
            if (nodeDrag) {
                setFocus();
                event->accept();
                return;
            }
        }
        QGraphicsView::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && doubleClick(event->pos())) {
            event->accept();
            return;
        }
        QGraphicsView::mouseDoubleClickEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        if (nodeDrag) {
            event->accept();
            return;
        }
        QGraphicsView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && nodeDrag) {
            nodeDrag = false;
            if ((event->pos() - origin).manhattanLength() >= QApplication::startDragDistance())
                drop(event->pos());
            event->accept();
            return;
        }
        QGraphicsView::mouseReleaseEvent(event);
    }

private:
    QPoint origin;
    bool nodeDrag = false;
};

struct NodeItem {
    QGraphicsPathItem *body;
    QGraphicsTextItem *label;
    QGraphicsSimpleTextItem *badge;
};
}

class Window : public QMainWindow {
public:
    explicit Window(bool enableRecovery = true) {
        setMinimumSize(360, 280);
        scene = new QGraphicsScene(this);
        canvas = new Canvas(scene, this);
        canvas->setObjectName("mapCanvas");
        setCentralWidget(canvas);
        canvas->press = [this](QPoint at) {
            if (editor && itemAt(at, editorProxy)) return false;
            const QString id = nodeAt(at);
            finishEdit();
            if (!id.isEmpty()) selectNode(id);
            return !id.isEmpty();
        };
        canvas->doubleClick = [this](QPoint at) {
            const QString id = nodeAt(at);
            if (id.isEmpty()) return false;
            finishEdit();
            selectNode(id);
            startEdit();
            return true;
        };
        canvas->drop = [this](QPoint end) {
            if (editor) return;
            const QString source = selected;
            const QString target = nodeAt(end);
            if (source.isEmpty() || target.isEmpty() || source == target) return;
            const QRectF bounds = items.value(target).body->sceneBoundingRect();
            const QPointF offset = canvas->mapToScene(end) - bounds.center();
            const qreal relative = doc.mindMap ? offset.y() / bounds.height() : offset.x() / bounds.width();
            moveNode(source, target, relative < -0.25 ? -1 : (relative > 0.25 ? 1 : 0));
        };
        makeMenus();
        makeZoomControls();
        canvas->zoomChanged = [this] {
            fitMode = false;
            updateZoomLevel();
        };
        fitTimer.setSingleShot(true);
        connect(&fitTimer, &QTimer::timeout, this, [this] {
            if (fitMode && !editor) fit();
        });
        canvas->viewportResized = [this] {
            if (fitMode) fitTimer.start(0);
        };
        applyAppearance();
        recoveryTimer.setSingleShot(true);
        recoveryTimer.setInterval(900);
        connect(&recoveryTimer, &QTimer::timeout, this, [this] { writeRecovery(); });
        resetDocument();
        resizeForScreen();
        statusBar()->showMessage(canvasHint());
        if (enableRecovery) QTimer::singleShot(0, this, [this] { setupRecovery(); });
    }

private:
    struct Revision {
        Document document;
        QString selected;
    };

    Appearance appearance = Appearance::load();
    QColor ink, moss, paper, line, accent, branchFill, leafFill;
    QToolBar *wideToolbar = nullptr;
    QToolBar *compactToolbar = nullptr;
    QLabel *zoomLevel = nullptr;
    QTimer fitTimer;
    bool fitMode = false;
    QGraphicsScene *scene;
    Canvas *canvas;
    Document doc;
    Document saved;
    QString selected, filePath, query, lastMatch;
    QVector<Revision> undoStack, redoStack;
    QMap<QString, NodeItem> items;
    QMap<QString, QGraphicsPathItem *> edges;
    QMap<QString, QString> parents;
    QVector<QAction *> canvasActions;
    QAction *classicAction = nullptr;
    QAction *hierarchyAction = nullptr;
    QPlainTextEdit *editor = nullptr;
    QGraphicsProxyWidget *editorProxy = nullptr;
    QString editId;
    bool dirty = false;
    QTimer recoveryTimer;
    QString recoveryPath;
    std::unique_ptr<QLockFile> recoveryLock;

    void resetDocument() {
        doc = Document();
        Node root;
        root.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        root.text = QStringLiteral("中心主题");
        doc.root = root.id;
        doc.nodes.insert(root.id, root);
        filePath.clear();
        needsSaveAs = false;
        initializeDocument();
    }

    void initializeDocument() {
        undoStack.clear();
        redoStack.clear();
        query.clear();
        lastMatch.clear();
        selected = doc.root;
        saved = doc;
        dirty = false;
        clearRecovery();
        fitMode = false;
        canvas->resetTransform();
        updateZoomLevel();
        render();
        canvas->centerOn(0, 0);
    }

    void updateTitle() {
        const QString name = filePath.isEmpty() ? QStringLiteral("未命名导图") : QFileInfo(filePath).fileName();
        setWindowTitle(QStringLiteral("%1%2  ·  枝理").arg(name, dirty ? QStringLiteral(" *") : QString()));
    }

    void checkpoint() {
        undoStack.append({doc, selected});
        if (undoStack.size() > 80) undoStack.removeFirst();
        redoStack.clear();
    }

    void changed() {
        dirty = doc.serialize() != saved.serialize();
        if (dirty) scheduleRecovery();
        else clearRecovery();
        render();
    }

    void history(bool backwards) {
        auto &from = backwards ? undoStack : redoStack;
        auto &to = backwards ? redoStack : undoStack;
        if (from.isEmpty()) return;
        to.append({doc, selected});
        Revision revision = from.takeLast();
        doc = std::move(revision.document);
        selected = revision.selected;
        changed();
    }

    int depth(QString id) const {
        int count = 0;
        while (parents.contains(id)) {
            id = parents.value(id);
            ++count;
        }
        return count;
    }

    void createNode(bool child) {
        const QString parent = child ? selected : parents.value(selected);
        if (parent.isEmpty()) return;
        if (doc.nodes.size() >= Document::maxNodes || depth(parent) >= Document::maxDepth) {
            statusBar()->showMessage(QStringLiteral("已达节点数量或层级上限"), 4000);
            return;
        }
        checkpoint();
        Node node;
        node.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        node.text = QStringLiteral("新主题");
        doc.nodes.insert(node.id, node);
        Node &destination = doc.nodes[parent];
        if (child) destination.children.append(node.id);
        else destination.children.insert(destination.children.indexOf(selected) + 1, node.id);
        destination.collapsed = false;
        selected = node.id;
        changed();
        canvas->ensureVisible(items.value(selected).body, 30, 30);
        startEdit();
    }

    void removeSelected() {
        const QString parent = parents.value(selected);
        if (parent.isEmpty()) return;
        checkpoint();
        doc.nodes[parent].children.removeOne(selected);
        QStringList pending{selected};
        while (!pending.isEmpty()) {
            const QString id = pending.takeLast();
            pending.append(doc.nodes.take(id).children);
        }
        selected = parent;
        changed();
    }

    void moveNode(const QString &source, const QString &target, int side) {
        if (source == doc.root || source == target) return;
        const QString oldParent = parents.value(source);
        const bool sibling = target != doc.root && side != 0;
        const QString newParent = sibling ? parents.value(target) : target;
        for (QString id = newParent; !id.isEmpty(); id = parents.value(id)) {
            if (id == source) {
                statusBar()->showMessage(QStringLiteral("无法将主题移入自己的分支"), 4000);
                return;
            }
        }
        QVector<QPair<QString, int>> pending{{source, depth(newParent) + 1}};
        while (!pending.isEmpty()) {
            const auto entry = pending.takeLast();
            if (entry.second > Document::maxDepth) {
                statusBar()->showMessage(QStringLiteral("移动后将超过层级上限"), 4000);
                return;
            }
            for (const auto &child : doc.nodes.value(entry.first).children)
                pending.append(qMakePair(child, entry.second + 1));
        }
        QStringList newOrder = doc.nodes.value(newParent).children;
        newOrder.removeOne(source);
        const int index = sibling ? newOrder.indexOf(target) + (side > 0 ? 1 : 0) : newOrder.size();
        newOrder.insert(index, source);
        if (newParent == oldParent && newOrder == doc.nodes.value(oldParent).children) return;
        checkpoint();
        doc.nodes[oldParent].children.removeOne(source);
        doc.nodes[newParent].children = newOrder;
        doc.nodes[newParent].collapsed = false;
        selected = source;
        changed();
        canvas->ensureVisible(items.value(source).body, 30, 30);
    }

    void navigate(Qt::Key key) {
        const bool towardParent = key == (doc.mindMap ? Qt::Key_Left : Qt::Key_Up);
        const bool towardChildren = key == (doc.mindMap ? Qt::Key_Right : Qt::Key_Down);
        const QString parent = parents.value(selected);
        QString target;
        if (towardParent) {
            target = parent;
        } else if (towardChildren) {
            const auto &node = qAsConst(doc.nodes)[selected];
            if (!node.collapsed && !node.children.isEmpty()) target = node.children.first();
        } else if (!parent.isEmpty()) {
            const auto &siblings = qAsConst(doc.nodes)[parent].children;
            const int step = key == (doc.mindMap ? Qt::Key_Up : Qt::Key_Left) ? -1 : 1;
            const int index = siblings.indexOf(selected) + step;
            if (index >= 0 && index < siblings.size()) target = siblings.at(index);
        }
        if (items.contains(target)) {
            selectNode(target);
            canvas->ensureVisible(items.value(target).body, 25, 25);
        }
    }

    void toggleCollapse() {
        if (doc.nodes.value(selected).children.isEmpty()) return;
        checkpoint();
        doc.nodes[selected].collapsed = !doc.nodes.value(selected).collapsed;
        changed();
    }

    void chooseLayout(bool classic) {
        if (doc.mindMap == classic) return;
        checkpoint();
        doc.mindMap = classic;
        changed();
        if (fitMode) fit();
        else canvas->ensureVisible(items.value(selected).body, 25, 25);
    }

    bool itemAt(QPoint at, const QGraphicsItem *item) const {
        return canvas->items(at).contains(const_cast<QGraphicsItem *>(item));
    }

    QString nodeAt(QPoint at) const {
        for (QGraphicsItem *item : canvas->items(at)) {
            const QString id = item->data(0).toString();
            if (!id.isEmpty()) return id;
        }
        return {};
    }

    void styleNode(const QString &id) {
        if (!items.contains(id)) return;
        const bool active = id == selected;
        items.value(id).body->setPen(QPen(active ? accent : line, active ? 2.5 : 1));
    }

    void selectNode(const QString &id) {
        if (!items.contains(id) || selected == id) return;
        const QString old = selected;
        selected = id;
        styleNode(old);
        styleNode(selected);
    }

    void startEdit() {
        if (editor || !items.contains(selected)) return;
        editId = selected;
        editor = new QPlainTextEdit;
        editor->setObjectName("topicEditor");
        editor->setFont(fontFor(appearance.topicFont));
        editor->setPlainText(doc.nodes.value(selected).text);
        editor->setTabChangesFocus(false);
        editor->selectAll();
        editorProxy = scene->addWidget(editor);
        editorProxy->setZValue(5);
        QRectF bounds = items.value(selected).body->sceneBoundingRect();
        bounds.setWidth(qMax(bounds.width(), 230.0));
        bounds.setHeight(qMax(bounds.height(), qreal(editor->fontMetrics().height() * 2 + 16)));
        bounds.moveCenter(items.value(selected).body->pos());
        editorProxy->setGeometry(bounds);
        canvas->ensureVisible(editorProxy, 30, 30);
        for (auto *action : canvasActions) action->setEnabled(false);
        editor->setFocus(Qt::OtherFocusReason);
        auto *accept = new QShortcut(QKeySequence("Ctrl+Return"), editor);
        auto *cancel = new QShortcut(QKeySequence("Escape"), editor);
        connect(accept, &QShortcut::activated, this, [this] { finishEdit(); });
        connect(cancel, &QShortcut::activated, this, [this] { finishEdit(false); });
        connect(editor, &QPlainTextEdit::textChanged, this, [this] { scheduleRecovery(); });
        statusBar()->showMessage(QStringLiteral("编辑主题  ·  Ctrl+Enter 确认  ·  Esc 取消  ·  Enter 换行"));
    }

    void finishEdit(bool accept = true) {
        if (!editor) return;
        if (accept) QApplication::inputMethod()->commit();
        const QString value = editor->toPlainText();
        const QString id = editId;
        QGraphicsProxyWidget *proxy = editorProxy;
        editor = nullptr;
        editorProxy = nullptr;
        scene->removeItem(proxy);
        proxy->deleteLater();
        canvas->setFocus();
        for (auto *action : canvasActions) action->setEnabled(true);
        if (accept && value.size() <= Document::maxText && value != doc.nodes.value(id).text) {
            checkpoint();
            doc.nodes[id].text = value;
            changed();
        } else if (dirty) {
            writeRecovery();
        } else {
            clearRecovery();
        }
        statusBar()->showMessage(accept && value.size() > Document::maxText
            ? QStringLiteral("文字超过 10 万字符，本次修改未保存")
            : canvasHint(), 5000);
    }

    void search(bool prompt) {
        if (prompt || query.isEmpty()) {
            bool ok = false;
            const QString value = QInputDialog::getText(this, QStringLiteral("查找主题"),
                QStringLiteral("输入主题关键词"), QLineEdit::Normal, query, &ok).trimmed();
            if (!ok || value.isEmpty()) return;
            query = value;
            lastMatch.clear();
        }
        QStringList matches;
        QVector<QString> pending{doc.root};
        while (!pending.isEmpty()) {
            const QString id = pending.takeLast();
            const Node &node = qAsConst(doc.nodes)[id];
            if (node.text.contains(query, Qt::CaseInsensitive)) matches.append(id);
            for (auto child = node.children.crbegin(); child != node.children.crend(); ++child)
                pending.append(*child);
        }
        if (matches.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("未找到匹配主题"), 4000);
            return;
        }
        const int index = (matches.indexOf(lastMatch) + 1) % matches.size();
        lastMatch = matches.at(index);
        QStringList collapsed;
        for (QString id = parents.value(lastMatch); !id.isEmpty(); id = parents.value(id))
            if (doc.nodes.value(id).collapsed) collapsed.append(id);
        if (!collapsed.isEmpty()) {
            checkpoint();
            for (const QString &id : collapsed) doc.nodes[id].collapsed = false;
            selected = lastMatch;
            changed();
        } else {
            selectNode(lastMatch);
        }
        canvas->centerOn(items.value(selected).body);
        statusBar()->showMessage(QStringLiteral("匹配 %1 / %2  ·  F3 下一个").arg(index + 1).arg(matches.size()), 5000);
    }

    void scheduleRecovery() {
        if (!recoveryPath.isEmpty() && !recoveryTimer.isActive()) recoveryTimer.start();
    }

    void setupRecovery(const QString &directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)) {
        if (directory.isEmpty() || !QDir().mkpath(directory)) {
            statusBar()->showMessage(QStringLiteral("无法创建自动恢复目录"), 8000);
            return;
        }
        recoveryPath = QDir(directory).filePath("recovery.json");
        recoveryLock.reset(new QLockFile(recoveryPath + ".lock"));
        recoveryLock->setStaleLockTime(0);
        if (!recoveryLock->tryLock()) {
            recoveryPath.clear();
            statusBar()->showMessage(QStringLiteral("另一实例正在使用自动恢复功能"), 8000);
            return;
        }
        QFile file(recoveryPath);
        if (!file.open(QIODevice::ReadOnly)) return;
        const QJsonObject envelope = QJsonDocument::fromJson(file.read(Document::maxBytes + 16384)).object();
        file.close();
        const QByteArray data = QJsonDocument(envelope.value("document").toObject()).toJson(QJsonDocument::Compact);
        Document recovered;
        if (!recovered.parse(data)) {
            statusBar()->showMessage(QStringLiteral("上次的恢复文件无法读取"), 8000);
            return;
        }
        const auto answer = QMessageBox::question(this, QStringLiteral("恢复导图"),
            QStringLiteral("发现上次会话留下的未保存内容。是否恢复？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer == QMessageBox::No) {
            clearRecovery();
            return;
        }
        doc = std::move(recovered);
        selected = doc.root;
        dirty = true;
        render();
        canvas->centerOn(0, 0);
        const QString original = envelope.value("file").toString();
        if (!original.isEmpty()) {
            filePath = QFileInfo(original).absolutePath() + "/" + QFileInfo(original).completeBaseName()
                + QStringLiteral("（已恢复）.smap");
            needsSaveAs = true;
        }
        updateTitle();
        statusBar()->showMessage(QStringLiteral("已恢复未保存的内容。请保存为新文件。"), 8000);
    }

    void writeRecovery() {
        if (recoveryPath.isEmpty()) return;
        Document draft = doc;
        if (editor && editor->toPlainText().size() <= Document::maxText)
            draft.nodes[editId].text = editor->toPlainText();
        const QByteArray data = draft.serialize();
        if (!dirty && data == saved.serialize()) {
            clearRecovery();
            return;
        }
        QString error;
        const QByteArray envelope = QJsonDocument(QJsonObject{
            {"file", filePath}, {"document", QJsonDocument::fromJson(data).object()}
        }).toJson(QJsonDocument::Compact);
        if (data.size() > Document::maxBytes || !writeAtomically(recoveryPath, envelope, &error))
            statusBar()->showMessage(QStringLiteral("自动恢复写入失败，请手动保存"), 8000);
    }

    void clearRecovery() {
        recoveryTimer.stop();
        if (!recoveryPath.isEmpty()) QFile::remove(recoveryPath);
    }

    bool confirmLeave() {
        finishEdit();
        if (!dirty) return true;
        const auto answer = QMessageBox::warning(this, QStringLiteral("未保存的修改"),
            QStringLiteral("关闭当前导图前，是否保存修改？"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Save) return save();
        return answer == QMessageBox::Discard;
    }

    bool needsSaveAs = false;

    bool save(bool as = false) {
        QString path = filePath;
        if (as || needsSaveAs || path.isEmpty()) {
            path = QFileDialog::getSaveFileName(this, QStringLiteral("保存导图"),
                path.isEmpty() ? QStringLiteral("未命名.smap") : path,
                QStringLiteral("枝理导图 (*.smap)"));
            if (path.isEmpty()) return false;
            if (!path.endsWith(".smap", Qt::CaseInsensitive) && !path.endsWith(".smap.json", Qt::CaseInsensitive))
                path += ".smap";
        }
        return saveTo(path);
    }

    bool saveTo(const QString &path) {
        const QByteArray data = doc.serialize();
        QString error = QStringLiteral("文件超过 50 MiB");
        if (data.size() > Document::maxBytes || !writeAtomically(path, data, &error)) {
            QMessageBox::warning(this, QStringLiteral("保存失败"), error);
            return false;
        }
        filePath = path;
        needsSaveAs = false;
        saved = doc;
        dirty = false;
        clearRecovery();
        updateTitle();
        statusBar()->showMessage(QStringLiteral("已保存  %1").arg(QFileInfo(path).fileName()), 4000);
        return true;
    }

    bool loadFrom(const QString &path, QString *error) {
        Document next;
        if (!next.readFile(path, error)) return false;
        doc = std::move(next);
        filePath = path;
        needsSaveAs = false;
        initializeDocument();
        fit();
        return true;
    }

    void openFile() {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开导图"), {},
            QStringLiteral("枝理导图 (*.smap *.smap.json *.json)"));
        if (path.isEmpty() || !confirmLeave()) return;
        QString error;
        if (!loadFrom(path, &error)) QMessageBox::warning(this, QStringLiteral("打开失败"), error);
    }

    QString canvasHint() const {
        return compactToolbar->isHidden()
            ? QStringLiteral("双击编辑  ·  拖到主题中心移入子级，拖到边缘调整顺序  ·  Ctrl+滚轮缩放")
            : QStringLiteral("双击编辑");
    }

    void applyAppearance() {
        QApplication::setFont(fontFor(appearance.interfaceFont));
        const bool contrast = appearance.highContrast;
        ink = QColor(contrast ? "#141414" : "#203d36");
        moss = QColor(contrast ? "#122c46" : "#286557");
        paper = QColor(contrast ? "#ffffff" : "#f7f7f1");
        line = QColor(contrast ? "#41566c" : "#92afa3");
        accent = QColor(contrast ? "#a14300" : "#b97035");
        branchFill = QColor(contrast ? "#e3edf8" : "#e0ede4");
        leafFill = QColor(contrast ? "#ffffff" : "#fffdf7");
        scene->setBackgroundBrush(paper);
        setStyleSheet(QStringLiteral(
            "QToolBar {background:%1;border:0;border-bottom:1px solid %2;padding:8px;spacing:5px}"
            "QToolButton {color:%3;padding:6px 10px;border-radius:5px}"
            "QToolButton:hover,QToolButton:checked {background:%4}"
            "QToolBar#compactToolbar {padding:4px;spacing:2px}"
            "QToolBar#compactToolbar QToolButton {padding:4px 5px}"
            "QStatusBar {background:%1;color:%3}"
            "QGraphicsView {border:0}"
            "QPlainTextEdit {background:%1;color:%3;border:2px solid %5;border-radius:5px}")
            .arg(leafFill.name(), line.name(), ink.name(), branchFill.name(), accent.name()));
        zoomLevel->setMinimumWidth(zoomLevel->fontMetrics().boundingRect(QStringLiteral("350%")).width() + 6);
        updateToolbarMode(width());
        if (!doc.root.isEmpty()) {
            render();
            if (fitMode) fit();
        }
    }

    void changeAppearance() {
        if (!editAppearance(this, &appearance)) return;
        appearance.save();
        applyAppearance();
    }

    void resizeForScreen() {
        QScreen *screen = QGuiApplication::primaryScreen();
        for (auto *candidate : QGuiApplication::screens()) {
            if (candidate->geometry().contains(QCursor::pos())) {
                screen = candidate;
                break;
            }
        }
        if (!screen) {
            resize(1160, 760);
            return;
        }
        const QRect work = screen->availableGeometry();
        const QSize size = QSize(1160, 760).boundedTo(work.size() - QSize(32, 48)).expandedTo(minimumSize());
        setGeometry(QRect(work.center() - QPoint(size.width() / 2, size.height() / 2), size));
    }

    void updateZoomLevel() {
        zoomLevel->setText(QStringLiteral("%1%").arg(qMax(1, qRound(canvas->transform().m11() * 100))));
    }

    void makeZoomControls() {
        const auto button = [this](const QString &name, const QString &label, const QString &tip, qreal factor) {
            auto *control = new QToolButton(statusBar());
            control->setObjectName(name);
            control->setText(label);
            control->setToolTip(tip);
            control->setAccessibleName(tip);
            connect(control, &QToolButton::clicked, this, [this, factor] { canvas->zoomBy(factor); });
            statusBar()->addPermanentWidget(control);
        };
        button("zoomOut", QStringLiteral("−"), QStringLiteral("缩小画布"), 1 / 1.25);
        zoomLevel = new QLabel(statusBar());
        zoomLevel->setObjectName("zoomLevel");
        zoomLevel->setMinimumWidth(fontMetrics().boundingRect(QStringLiteral("350%")).width() + 6);
        zoomLevel->setAlignment(Qt::AlignCenter);
        statusBar()->addPermanentWidget(zoomLevel);
        button("zoomIn", QStringLiteral("+"), QStringLiteral("放大画布"), 1.25);
    }

    void fit() {
        fitMode = true;
        canvas->fitInView(scene->itemsBoundingRect().adjusted(-45, -45, 45, 45), Qt::KeepAspectRatio);
        const qreal scale = canvas->transform().m11();
        if (scale > 1.25) canvas->scale(1.25 / scale, 1.25 / scale);
        updateZoomLevel();
    }

    bool exportTo(const QString &path, const QString &format, QString *error) {
        const QRectF bounds = scene->itemsBoundingRect().adjusted(-24, -24, 24, 24);
        const QSize size(qCeil(bounds.width()), qCeil(bounds.height()));
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            *error = file.errorString();
            return false;
        }
        auto paint = [&](QPaintDevice *device, const QRectF &target) {
            QPainter painter;
            if (!painter.begin(device)) return false;
            painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
            scene->render(&painter, target, bounds, Qt::KeepAspectRatio);
            return painter.end();
        };
        const QString previous = selected;
        selected.clear();
        styleNode(previous);
        bool written = false;
        if (format == "svg") {
            QSvgGenerator svg;
            svg.setOutputDevice(&file);
            svg.setSize(size);
            svg.setViewBox(QRect(QPoint(0, 0), size));
            svg.setTitle(QStringLiteral("枝理导图"));
            written = paint(&svg, QRectF(QPointF(), size));
        } else if (format == "pdf") {
            QPdfWriter pdf(&file);
            pdf.setResolution(144);
            pdf.setPageSize(QPageSize(QPageSize::A4));
            pdf.setPageOrientation(size.width() > size.height() ? QPageLayout::Landscape : QPageLayout::Portrait);
            pdf.setTitle(doc.nodes.value(doc.root).text);
            const qreal ratio = qMin(pdf.width() / bounds.width(), pdf.height() / bounds.height());
            const QSizeF extent = bounds.size() * ratio;
            const QPointF corner((pdf.width() - extent.width()) / 2, (pdf.height() - extent.height()) / 2);
            written = paint(&pdf, QRectF(corner, extent));
        } else if (qint64(size.width()) * size.height() <= 32000000 && size.width() <= 32767 && size.height() <= 32767) {
            QImage image(size, QImage::Format_ARGB32);
            image.fill(paper);
            written = paint(&image, QRectF(QPointF(), size)) && image.save(&file, "PNG");
        } else {
            *error = QStringLiteral("导图过大，请折叠部分分支或导出为 SVG / PDF。");
        }
        selectNode(previous);
        if (written && file.commit()) return true;
        if (error->isEmpty())
            *error = file.error() == QFileDevice::NoError ? QStringLiteral("无法生成导出文件") : file.errorString();
        return false;
    }

    void exportFile(const QString &format) {
        const QString suffix = "." + format;
        QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出导图"), {},
            QStringLiteral("%1 (*%2)").arg(format.toUpper(), suffix));
        if (path.isEmpty()) return;
        if (!path.endsWith(suffix, Qt::CaseInsensitive)) path += suffix;
        QString error;
        if (!exportTo(path, format, &error)) QMessageBox::warning(this, QStringLiteral("导出失败"), error);
        else statusBar()->showMessage(QStringLiteral("已导出  %1").arg(QFileInfo(path).fileName()), 4000);
    }

    void makeMenus() {
        auto action = [&](QMenu *menu, const QString &name, const char *shortcut,
                          std::function<void()> run, bool canvasOnly = false) {
            QAction *item = menu->addAction(name);
            item->setShortcut(QKeySequence(QString::fromLatin1(shortcut)));
            connect(item, &QAction::triggered, this, [this, run] { finishEdit(); run(); });
            if (canvasOnly) canvasActions.append(item);
            return item;
        };
        auto *file = menuBar()->addMenu(QStringLiteral("文件"));
        action(file, QStringLiteral("新建"), "Ctrl+N", [this] { if (confirmLeave()) resetDocument(); });
        action(file, QStringLiteral("打开"), "Ctrl+O", [this] { openFile(); });
        auto *saveAction = action(file, QStringLiteral("保存"), "Ctrl+S", [this] { save(); });
        action(file, QStringLiteral("另存为"), "Ctrl+Shift+S", [this] { save(true); });
        file->addSeparator();
        for (const QString &format : {QString("png"), QString("svg"), QString("pdf")})
            action(file, QStringLiteral("导出 %1").arg(format.toUpper()), "", [this, format] { exportFile(format); });

        auto *edit = menuBar()->addMenu(QStringLiteral("编辑"));
        auto *child = action(edit, QStringLiteral("新增子级"), "Tab", [this] { createNode(true); }, true);
        action(edit, QStringLiteral("新增同级"), "Return", [this] { createNode(false); }, true);
        action(edit, QStringLiteral("编辑主题"), "F2", [this] { startEdit(); }, true);
        action(edit, QStringLiteral("删除分支"), "Delete", [this] { removeSelected(); }, true);
        action(edit, QStringLiteral("折叠 / 展开"), "Space", [this] { toggleCollapse(); }, true);
        edit->addSeparator();
        auto *undo = action(edit, QStringLiteral("撤销"), "Ctrl+Z", [this] { history(true); }, true);
        action(edit, QStringLiteral("重做"), "Ctrl+Y", [this] { history(false); }, true);
        auto *find = action(edit, QStringLiteral("查找"), "Ctrl+F", [this] { search(true); });
        action(edit, QStringLiteral("查找下一个"), "F3", [this] { search(false); }, true);

        auto *navigation = edit->addMenu(QStringLiteral("移动选中项"));
        action(navigation, QStringLiteral("向上"), "Up", [this] { navigate(Qt::Key_Up); }, true);
        action(navigation, QStringLiteral("向下"), "Down", [this] { navigate(Qt::Key_Down); }, true);
        action(navigation, QStringLiteral("向左"), "Left", [this] { navigate(Qt::Key_Left); }, true);
        action(navigation, QStringLiteral("向右"), "Right", [this] { navigate(Qt::Key_Right); }, true);

        auto *view = menuBar()->addMenu(QStringLiteral("视图"));
        action(view, QStringLiteral("外观设置…"), "", [this] { changeAppearance(); });
        view->addSeparator();
        classicAction = action(view, QStringLiteral("经典导图"), "Ctrl+1", [this] { chooseLayout(true); });
        hierarchyAction = action(view, QStringLiteral("结构图"), "Ctrl+2", [this] { chooseLayout(false); });
        for (auto *item : {classicAction, hierarchyAction}) item->setCheckable(true);
        // Keep checked state tied to the actual document, including undo and open.
        auto *group = new QActionGroup(this);
        group->addAction(classicAction);
        group->addAction(hierarchyAction);
        auto *fitAction = action(view, QStringLiteral("适应窗口"), "Ctrl+0", [this] { fit(); });

        action(view, QStringLiteral("放大"), "Ctrl+=", [this] { canvas->zoomBy(1.25); }, true);
        action(view, QStringLiteral("缩小"), "Ctrl+-", [this] { canvas->zoomBy(1 / 1.25); }, true);
        action(view, QStringLiteral("实际大小"), "Ctrl+Shift+0", [this] {
            canvas->zoomBy(1 / canvas->transform().m11());
        }, true);

        wideToolbar = addToolBar(QStringLiteral("常用"));
        auto *bar = wideToolbar;
        bar->setObjectName("wideToolbar");
        bar->setMovable(false);
        bar->setFloatable(false);
        auto *brand = new QLabel(QStringLiteral("枝理  /  ZHILI"), bar);
        brand->setStyleSheet(QStringLiteral("font-weight:700;color:#286557;padding:0 14px 0 7px"));
        bar->addWidget(brand);
        for (auto *item : {saveAction, child, undo, find}) bar->addAction(item);
        auto *spacer = new QWidget(bar);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        bar->addWidget(spacer);
        for (auto *item : {classicAction, hierarchyAction, fitAction}) bar->addAction(item);

        compactToolbar = new QToolBar(QStringLiteral("常用（紧凑）"), this);
        compactToolbar->setObjectName("compactToolbar");
        compactToolbar->setMovable(false);
        compactToolbar->setFloatable(false);
        addToolBar(Qt::TopToolBarArea, compactToolbar);
        auto compactButton = [this](QAction *action, const QString &label) {
            auto *button = new QToolButton(compactToolbar);
            button->setText(label);
            button->setAccessibleName(action->text());
            button->setToolTip(action->text());
            button->setEnabled(action->isEnabled());
            connect(button, &QToolButton::clicked, action, &QAction::trigger);
            connect(action, &QAction::changed, button, [button, action] { button->setEnabled(action->isEnabled()); });
            compactToolbar->addWidget(button);
        };
        compactButton(saveAction, QStringLiteral("保存"));
        compactButton(child, QStringLiteral("加子"));
        auto *layouts = new QToolButton(compactToolbar);
        layouts->setText(QStringLiteral("布局"));
        layouts->setAccessibleName(QStringLiteral("切换导图布局"));
        layouts->setPopupMode(QToolButton::InstantPopup);
        auto *layoutMenu = new QMenu(layouts);
        layoutMenu->addAction(classicAction);
        layoutMenu->addAction(hierarchyAction);
        layouts->setMenu(layoutMenu);
        compactToolbar->addWidget(layouts);
        compactButton(fitAction, QStringLiteral("适应"));
        compactToolbar->hide();
    }

    void render() {
        parents.clear();
        const auto &nodes = qAsConst(doc.nodes);
        for (const Node &node : nodes)
            for (const auto &child : node.children) parents.insert(child, node.id);

        const QFont topicFont = fontFor(appearance.topicFont);
        QMap<QString, QSizeF> sizes;
        QVector<QString> visible{doc.root};
        for (int i = 0; i < visible.size(); ++i) {
            const QString id = visible.at(i);
            const Node &node = nodes[id];
            if (!node.collapsed) for (const auto &child : node.children) visible.append(child);
            if (!items.contains(id)) {
                NodeItem item{scene->addPath(QPainterPath()), scene->addText(QString()), scene->addSimpleText(QString())};
                item.body->setZValue(1);
                item.label->setZValue(2);
                item.badge->setZValue(3);
                for (auto *part : {static_cast<QGraphicsItem *>(item.body), static_cast<QGraphicsItem *>(item.label),
                                   static_cast<QGraphicsItem *>(item.badge)}) {
                    part->setData(0, id);
                    part->setAcceptedMouseButtons(Qt::NoButton);
                }
                items.insert(id, item);
            }
            const NodeItem item = items.value(id);
            const bool root = id == doc.root;
            const bool branch = parents.value(id) == doc.root;
            QString display = node.text.left(360);
            if (node.text.size() > display.size()) display += QChar(0x2026);
            QFont font = topicFont;
            if (root) font.setPointSize(appearance.topicFont.points + 4);
            if (item.label->toPlainText() != display || item.label->textWidth() < 0 || item.label->font() != font) {
                item.label->setFont(font);
                item.label->setTextWidth(-1);
                item.label->setPlainText(display);
                item.label->setTextWidth(qBound(root ? 136.0 : 78.0, item.label->boundingRect().width(), 248.0));
            }
            item.label->setDefaultTextColor(root ? Qt::white : ink);
            const QSizeF label = item.label->boundingRect().size();
            const QSizeF size(label.width() + 24, qMax(root ? 50.0 : 36.0, label.height() + 12));
            sizes[id] = size;
            QPainterPath shape;
            shape.addRoundedRect(QRectF(QPointF(-size.width() / 2, -size.height() / 2), size), 9, 9);
            item.body->setPath(shape);
            item.body->setBrush(root ? moss : (branch ? branchFill : leafFill));
            item.badge->setText(QStringLiteral("+%1").arg(node.children.size()));
            item.badge->setBrush(accent);
            item.badge->setVisible(node.collapsed && !node.children.isEmpty());
        }

        for (auto it = items.begin(); it != items.end();) {
            if (sizes.contains(it.key())) { ++it; continue; }
            delete it->body;
            delete it->label;
            delete it->badge;
            it = items.erase(it);
        }
        for (auto it = edges.begin(); it != edges.end();) {
            if (sizes.contains(it.key())) { ++it; continue; }
            delete it.value();
            it = edges.erase(it);
        }

        const auto positions = layoutNodes(doc, sizes);
        if (!positions.contains(selected)) selected = doc.root;
        for (auto it = positions.cbegin(); it != positions.cend(); ++it) {
            const QString &id = it.key();
            const QPointF point = it.value();
            const NodeItem item = items.value(id);
            const QSizeF label = item.label->boundingRect().size();
            const QSizeF size = sizes.value(id);
            item.body->setPos(point);
            item.label->setPos(point - QPointF(label.width() / 2, label.height() / 2));
            item.badge->setPos(point + QPointF(size.width() / 2 + 5, -item.badge->boundingRect().height() / 2));
            styleNode(id);
            if (id == doc.root) continue;

            const QString parent = parents.value(id);
            QPointF a = positions.value(parent);
            QPointF b = point;
            QPainterPath path;
            if (doc.mindMap) {
                const qreal direction = b.x() > a.x() ? 1 : -1;
                a.rx() += direction * sizes.value(parent).width() / 2;
                b.rx() -= direction * size.width() / 2;
                const qreal middle = (a.x() + b.x()) / 2;
                path.moveTo(a);
                path.cubicTo(middle, a.y(), middle, b.y(), b.x(), b.y());
            } else {
                a.ry() += sizes.value(parent).height() / 2;
                b.ry() -= size.height() / 2;
                const qreal middle = (a.y() + b.y()) / 2;
                path.moveTo(a);
                path.lineTo(a.x(), middle);
                path.lineTo(b.x(), middle);
                path.lineTo(b);
            }
            if (!edges.contains(id)) edges.insert(id, scene->addPath(path));
            edges.value(id)->setPath(path);
            edges.value(id)->setPen(QPen(line, 1.6));
        }
        scene->setSceneRect(scene->itemsBoundingRect().adjusted(-100, -100, 100, 100));
        classicAction->setChecked(doc.mindMap);
        hierarchyAction->setChecked(!doc.mindMap);
        updateTitle();
    }

    void updateToolbarMode(int width) {
        if (!wideToolbar || !compactToolbar) return;
        const bool compact = width < qMax(840, wideToolbar->sizeHint().width() + 24);
        const QString priorHint = canvasHint();
        wideToolbar->setVisible(!compact);
        compactToolbar->setVisible(compact);
        if (statusBar()->currentMessage() == priorHint) statusBar()->showMessage(canvasHint());
    }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QMainWindow::resizeEvent(event);
        updateToolbarMode(event->size().width());
    }

    void closeEvent(QCloseEvent *event) override {
        if (confirmLeave()) {
            clearRecovery();
            event->accept();
        } else {
            event->ignore();
        }
    }

#ifdef ZHILI_TEST
    friend int runSmokeTest();
#endif
};

#ifdef ZHILI_TEST
#include "../tests/smoke.inc"
#endif

int main(int argc, char *argv[]) {
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
    QApplication app(argc, argv);
    app.setApplicationName("ZhiLi");
    app.setApplicationDisplayName(QStringLiteral("枝理"));
    app.setOrganizationName("ZhiLi");
    registerEmbeddedFonts();
#ifdef ZHILI_TEST
    return runSmokeTest();
#else
    Window window;
    window.show();
    return app.exec();
#endif
}
