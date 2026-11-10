#include "appearance.h"
#include "clipboard.h"
#include "document.h"
#include "layout.h"
#include "symbols.h"
#include "symbolpicker.h"
#include "templates.h"
#include "template_dialog.h"
#include "pptx.h"
#include "importer.h"

#include <QtWidgets>
#include <QtSvg/QSvgGenerator>
#include <QtSvg/QGraphicsSvgItem>
#include <QtSvg/QSvgRenderer>
#include <QPdfWriter>
#include <QtMath>
#include <functional>
#include <memory>

namespace {
QString topicTextError(const QString &text) {
    if (text.size() > Document::maxText)
        return QStringLiteral("文字超过 10 万字符，请缩短后再确认");
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i).isHighSurrogate()) {
            if (++i == text.size() || !text.at(i).isLowSurrogate())
                return QStringLiteral("文字包含无效字符，请重新输入或粘贴该段文字");
        } else if (text.at(i).isLowSurrogate()) {
            return QStringLiteral("文字包含无效字符，请重新输入或粘贴该段文字");
        }
    }
    return {};
}

QRect boundedWindowGeometry(const QRect &requested, const QRect &available, const QSize &minimum) {
    const QSize size = requested.size().expandedTo(minimum).boundedTo(available.size());
    return QRect(QPoint(qBound(available.left(), requested.left(), available.right() - size.width() + 1),
                        qBound(available.top(), requested.top(), available.bottom() - size.height() + 1)), size);
}

class Canvas : public QGraphicsView {
public:
    explicit Canvas(QGraphicsScene *scene, QWidget *parent) : QGraphicsView(scene, parent) {
        setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        setDragMode(ScrollHandDrag);
        setViewportUpdateMode(BoundingRectViewportUpdate);
        setTransformationAnchor(AnchorUnderMouse);
    }

    std::function<bool(QPoint, Qt::KeyboardModifiers)> press;
    std::function<bool(QPoint)> doubleClick;
    std::function<void(QPoint, bool)> drop;
    std::function<void(QPoint, bool)> dragOver;
    std::function<void()> dragEnded;
    std::function<void()> toggleClicked;
    std::function<void(QRect)> selectArea;
    std::function<void()> zoomChanged;
    std::function<void()> viewportResized;
    QColor gridColor;

    void showDropPreview(const QPainterPath &marker = {}, const QPainterPath &guide = {}, const QColor &color = {}) {
        const auto area = [this](const QPainterPath &path) {
            return path.isEmpty() ? QRect() : mapFromScene(path.controlPointRect()).boundingRect().adjusted(-4, -4, 4, 4);
        };
        const QRect dirty = area(dropMarker).united(area(dropGuide)).united(area(marker)).united(area(guide));
        dropMarker = marker;
        dropGuide = guide;
        dropColor = color;
        if (!dirty.isEmpty()) viewport()->update(dirty);
    }

    void zoomBy(qreal factor) {
        const qreal current = transform().m11();
        const qreal next = qBound(qMin(0.05, current), current * factor, 3.5);
        scale(next / current, next / current);
        zoomChanged();
    }

protected:
    void drawForeground(QPainter *painter, const QRectF &rect) override {
        QGraphicsView::drawForeground(painter, rect);
        if (dropMarker.isEmpty()) return;
        painter->save();
        painter->setBrush(Qt::NoBrush);
        QPen guidePen(dropColor, 1, Qt::DashLine);
        guidePen.setCosmetic(true);
        painter->setPen(guidePen);
        painter->drawPath(dropGuide);
        QPen markerPen(dropColor, 2.5, Qt::SolidLine, Qt::RoundCap);
        markerPen.setCosmetic(true);
        painter->setPen(markerPen);
        painter->drawPath(dropMarker);
        painter->restore();
    }

    void drawBackground(QPainter *painter, const QRectF &rect) override {
        QGraphicsView::drawBackground(painter, rect);
        if (!gridColor.isValid()) return;
        const qreal scale = transform().m11();
        const qreal step = 24 / scale;
        QPolygonF dots;
        for (qreal y = qCeil(rect.top() / step) * step; y <= rect.bottom(); y += step)
            for (qreal x = qCeil(rect.left() / step) * step; x <= rect.right(); x += step)
                dots.append(QPointF(x, y));
        QPen pen(gridColor, 1.6, Qt::SolidLine, Qt::RoundCap);
        pen.setCosmetic(true);
        painter->setPen(pen);
        painter->drawPoints(dots);
    }

    void wheelEvent(QWheelEvent *event) override {
        if (event->buttons() & Qt::RightButton || event->modifiers() & Qt::ControlModifier) {
            rightWheelUsed = event->buttons() & Qt::RightButton;
            const int delta = event->angleDelta().y();
            if (delta != 0) zoomBy(qPow(1.12, delta / 120.0));
            event->accept();
        } else if (event->modifiers() & Qt::ShiftModifier) {
            auto *bar = horizontalScrollBar();
            const int pixels = event->pixelDelta().y();
            const int ticks = event->angleDelta().y();
            const int step = pixels ? pixels : qRound(ticks * bar->singleStep() * 3 / 120.0);
            bar->setValue(bar->value() - step);
            event->accept();
        } else {
            QGraphicsView::wheelEvent(event);
        }
    }

    bool event(QEvent *event) override {
        if (event->type() == QEvent::ContextMenu && rightWheelUsed &&
            static_cast<QContextMenuEvent *>(event)->reason() == QContextMenuEvent::Mouse) {
            rightWheelUsed = false;
            return true;
        }
        return QGraphicsView::event(event);
    }

    void resizeEvent(QResizeEvent *event) override {
        QGraphicsView::resizeEvent(event);
        if (viewportResized) viewportResized();
    }

    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::RightButton) rightWheelUsed = false;
        if (event->button() == Qt::MiddleButton) {
            middlePanning = true;
            panPoint = event->pos();
            viewport()->setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton) {
            origin = event->pos();
            nodeDrag = press(origin, event->modifiers());
            if (nodeDrag) {
                setFocus();
                event->accept();
                return;
            }
            if ((event->modifiers() & Qt::ShiftModifier) && !itemAt(origin)) {
                if (!band) band = new QRubberBand(QRubberBand::Rectangle, viewport());
                band->setGeometry(QRect(origin, QSize()));
                band->show();
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
        if (band && band->isVisible()) {
            band->setGeometry(QRect(origin, event->pos()).normalized());
            event->accept();
            return;
        }
        if (middlePanning) {
            const QPoint delta = event->pos() - panPoint;
            panPoint = event->pos();
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
            event->accept();
            return;
        }
        if (nodeDrag) {
            if ((event->pos() - origin).manhattanLength() >= QApplication::startDragDistance())
                dragOver(event->pos(), event->modifiers() & Qt::ControlModifier);
            else
                dragEnded();
            event->accept();
            return;
        }
        QGraphicsView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && band && band->isVisible()) {
            band->hide();
            selectArea(QRect(origin, event->pos()).normalized());
            event->accept();
            return;
        }
        if (event->button() == Qt::MiddleButton && middlePanning) {
            middlePanning = false;
            viewport()->unsetCursor();
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton && nodeDrag) {
            nodeDrag = false;
            dragEnded();
            if ((event->pos() - origin).manhattanLength() >= QApplication::startDragDistance())
                drop(event->pos(), event->modifiers() & Qt::ControlModifier);
            else if (event->modifiers() & Qt::ControlModifier)
                toggleClicked();
            event->accept();
            return;
        }
        QGraphicsView::mouseReleaseEvent(event);
    }

    void leaveEvent(QEvent *event) override {
        dragEnded();
        QGraphicsView::leaveEvent(event);
    }

private:
    QPainterPath dropMarker, dropGuide;
    QColor dropColor;
    QPoint origin;
    QPoint panPoint;
    QRubberBand *band = nullptr;
    bool nodeDrag = false;
    bool middlePanning = false;
    bool rightWheelUsed = false;
};

struct NodeItem {
    QGraphicsPathItem *body;
    QGraphicsTextItem *label;
    QGraphicsSimpleTextItem *badge;
    QGraphicsSvgItem *symbol = nullptr;
    QColor branchColor;
    qreal labelLimit = 0;
};

class TopicEditor : public QPlainTextEdit {
public:
    std::function<void()> submit;

protected:
    void inputMethodEvent(QInputMethodEvent *event) override {
        composing = !event->preeditString().isEmpty();
        QPlainTextEdit::inputMethodEvent(event);
    }

    void keyPressEvent(QKeyEvent *event) override {
        const auto key = event->key();
        const auto modifiers = event->modifiers() & ~Qt::KeypadModifier;
        if ((key == Qt::Key_Return || key == Qt::Key_Enter) &&
            (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier)) {
            event->accept();
            if (composing) return;
            if (modifiers == Qt::ShiftModifier) insertPlainText(QStringLiteral("\n"));
            else submit();
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }

private:
    bool composing = false;
};

QColor tint(const QColor &color, int percent) {
    const auto channel = [percent](int value) { return value + (255 - value) * percent / 100; };
    return QColor(channel(color.red()), channel(color.green()), channel(color.blue()));
}

Qt::Alignment topicAlignment(TextAlignment alignment) {
    if (alignment == TextAlignment::Left) return Qt::AlignLeft;
    if (alignment == TextAlignment::Right) return Qt::AlignRight;
    return Qt::AlignHCenter;
}

void alignTopicText(QTextDocument *text, TextAlignment alignment) {
    auto option = text->defaultTextOption();
    option.setAlignment(topicAlignment(alignment));
    text->setDefaultTextOption(option);
}

QIcon toolbarIcon(const char *path) {
    QSvgRenderer svg(QByteArrayLiteral(
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
        "<path fill='none' stroke='#285b4c' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round' d='")
        + path + QByteArrayLiteral("'/></svg>"));
    QIcon icon;
    for (int size : {20, 24, 32, 40, 48, 64}) {
        QPixmap image(size, size);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        svg.render(&painter);
        painter.end();
        icon.addPixmap(image);
    }
    return icon;
}

QPainterPath layerConnector(QPointF start, QPointF end, qreal fromLane, qreal toLane,
                            bool horizontal, ConnectorStyle style, bool rounded) {
    // Reserve the space between complete node layers for lateral travel.
    // The short leads cross the node layers only at their own topic's center.
    const auto point = [horizontal](qreal along, qreal across) {
        return horizontal ? QPointF(along, across) : QPointF(across, along);
    };
    const qreal from = horizontal ? start.y() : start.x();
    const qreal to = horizontal ? end.y() : end.x();
    const qreal middle = (fromLane + toLane) / 2;
    QPainterPath path(start);
    if (from == to) {
        path.lineTo(end);
        return path;
    }
    path.lineTo(point(fromLane, from));
    if (style == ConnectorStyle::Curved) {
        path.cubicTo(point(middle, from), point(middle, to), point(toLane, to));
    } else if (style == ConnectorStyle::Orthogonal) {
        const qreal radius = rounded ? qMin(8.0, qAbs(to - from) / 2) : 0;
        const qreal sign = to > from ? 1 : -1;
        path.lineTo(point(middle - radius, from));
        if (radius) path.quadTo(point(middle, from), point(middle, from + sign * radius));
        path.lineTo(point(middle, to - sign * radius));
        if (radius) path.quadTo(point(middle, to), point(middle + radius, to));
    }
    path.lineTo(point(toLane, to));
    path.lineTo(end);
    return path;
}
}

class Window : public QMainWindow {
public:
    explicit Window(bool enableRecovery = true) {
        setMinimumSize(360, 280);
        scene = new QGraphicsScene(this);
        canvas = new Canvas(scene, this);
        canvas->setObjectName("mapCanvas");
        setCentralWidget(canvas);
        rootRail = scene->addPath(QPainterPath());
        rootRail->setZValue(-1);
        rootRail->setAcceptedMouseButtons(Qt::NoButton);
        canvas->press = [this](QPoint at, Qt::KeyboardModifiers modifiers) {
            dragSource.clear();
            draggedNodes = 0;
            draggedHeight = 0;
            if (editor && itemAt(at, editorProxy)) return false;
            const QString id = nodeAt(at);
            if (!finishEdit()) return false;
            if (id.isEmpty()) return false;
            dragSource = id;
            if (modifiers == Qt::NoModifier && items.value(id).badge->isVisible() && itemAt(at, items.value(id).badge)) {
                selectNode(id);
                toggleCollapse();
                dragSource.clear();
                return true;
            }
            if (modifiers & Qt::ControlModifier) return true;
            selectNode(id);
            return id != doc.root;
        };
        canvas->toggleClicked = [this] { toggleSelection(dragSource); };
        canvas->selectArea = [this](QRect rect) {
            if (editor) return;
            const QRectF area = canvas->mapToScene(rect).boundingRect();
            QStringList hits;
            for (auto it = items.cbegin(); it != items.cend(); ++it)
                if (area.intersects(it->body->sceneBoundingRect())) hits.append(it.key());
            if (!hits.isEmpty()) replaceSelection(hits);
        };
        canvas->doubleClick = [this](QPoint at) {
            if (editor && itemAt(at, editorProxy)) return false;
            const QString id = nodeAt(at);
            if (!finishEdit()) return true;
            if (id.isEmpty()) {
                selectNode(doc.root);
                createNode(true);
            } else {
                selectNode(id);
                startEdit();
            }
            return true;
        };
        canvas->drop = [this](QPoint end, bool copy) {
            const auto target = snapDrop(end, copy);
            if (target.id.isEmpty()) return;
            if (copy) copyDraggedBranch(dragSource, target.id, target.side);
            else moveNode(dragSource, target.id, target.side, true);
        };
        canvas->dragOver = [this](QPoint at, bool copy) { previewDrop(at, copy); };
        canvas->dragEnded = [this] { canvas->showDropPreview(); };
        makeMenus();
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
    QVector<QToolButton *> compactButtons;
    QLabel *documentLabel = nullptr;
    QLabel *saveState = nullptr;
    QLabel *zoomLevel = nullptr;
    QLabel *workspaceStats = nullptr;
    QTimer fitTimer;
    bool fitMode = false;
    QGraphicsScene *scene;
    QGraphicsPathItem *rootRail;
    Canvas *canvas;
    Document doc;
    Document saved;
    QString selected, filePath, query, lastMatch;
    QString dragSource;
    int draggedNodes = 0;
    int draggedHeight = 0;
    QSet<QString> additionalSelection;
    QVector<Revision> undoStack, redoStack;
    QMap<QString, NodeItem> items;
    QMap<QString, QGraphicsPathItem *> edges;
    QMap<QString, qreal> leafRails;
    QMap<QString, QSvgRenderer *> symbolRenderers;
    QMap<QString, QString> parents;
    QVector<QAction *> canvasActions;
    QAction *classicAction = nullptr;
    QAction *hierarchyAction = nullptr;
    QAction *rightLayoutAction = nullptr;
    QAction *leftLayoutAction = nullptr;
    QMap<ConnectorStyle, QAction *> connectorActions;
    QAction *siblingAction = nullptr;
    QAction *removeAction = nullptr;
    QAction *cutAction = nullptr;
    QAction *collapseAction = nullptr;
    QAction *undoAction = nullptr;
    QAction *redoAction = nullptr;
    QAction *moveUpAction = nullptr;
    QAction *moveDownAction = nullptr;
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
        additionalSelection.clear();
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
        documentLabel->setText(documentLabel->fontMetrics().elidedText(name, Qt::ElideMiddle, 205));
        documentLabel->setToolTip(name);
        saveState->setText(dirty ? QStringLiteral("● 待保存")
                                : (filePath.isEmpty() ? QStringLiteral("新建") : QStringLiteral("✓ 已保存")));
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
        additionalSelection.clear();
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

    void createNode(bool child, bool before = false) {
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
        else destination.children.insert(destination.children.indexOf(selected) + (before ? 0 : 1), node.id);
        destination.collapsed = false;
        selected = node.id;
        changed();
        canvas->ensureVisible(items.value(selected).body, 30, 30);
        startEdit();
    }

    void insertParent() {
        const QString parent = parents.value(selected);
        if (parent.isEmpty()) return;
        if (doc.nodes.size() >= Document::maxNodes) {
            statusBar()->showMessage(QStringLiteral("已达节点数量上限"), 4000);
            return;
        }
        Document next = doc;
        Node node;
        node.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        node.text = QStringLiteral("新主题");
        node.children.append(selected);
        auto &siblings = next.nodes[parent].children;
        siblings[siblings.indexOf(selected)] = node.id;
        next.nodes.insert(node.id, node);
        QString error;
        Document checked;
        if (!checked.parse(next.serialize(), &error)) {
            statusBar()->showMessage(error, 4000);
            return;
        }
        checkpoint();
        doc = std::move(checked);
        selected = node.id;
        changed();
        startEdit();
    }

    void removeTopicKeepChildren() {
        const QString parent = parents.value(selected);
        if (parent.isEmpty()) return;
        checkpoint();
        auto &siblings = doc.nodes[parent].children;
        const int index = siblings.indexOf(selected);
        siblings.removeAt(index);
        const auto children = doc.nodes.take(selected).children;
        for (int i = 0; i < children.size(); ++i) siblings.insert(index + i, children.at(i));
        selected = children.isEmpty() ? parent : children.first();
        changed();
    }

    void removeSelected() {
        const QStringList roots = selectedRoots(false);
        if (roots.isEmpty()) return;
        checkpoint();
        for (const QString &id : roots) doc.nodes[parents.value(id)].children.removeOne(id);
        QStringList pending = roots;
        while (!pending.isEmpty()) {
            const QString id = pending.takeLast();
            pending.append(doc.nodes.take(id).children);
        }
        while (!doc.nodes.contains(selected)) selected = parents.value(selected);
        additionalSelection.clear();
        changed();
    }

    void copyBranch(bool cut = false) {
        const QStringList roots = selectedRoots(!cut);
        if (roots.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("中心主题不能剪切，可使用复制"), 4000);
            return;
        }
        QString error;
        const QByteArray data = BranchClipboard::encode(doc, roots, &error);
        if (data.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("复制失败：%1").arg(error), 6000);
            return;
        }
        auto *mime = new QMimeData;
        mime->setData(BranchClipboard::mimeType(), data);
        QStringList outlines;
        for (const QString &id : roots) outlines.append(BranchClipboard::outline(doc, id));
        mime->setText(outlines.join('\n'));
        QApplication::clipboard()->setMimeData(mime);
        if (cut) removeSelected();
        statusBar()->showMessage(cut ? QStringLiteral("已剪切分支，可粘贴或撤销")
                                    : QStringLiteral("已复制分支，可在其他主题下粘贴"), 4000);
    }

    void pasteBranch(bool asSibling = false) {
        const QMimeData *mime = QApplication::clipboard()->mimeData();
        if (!mime || (!mime->hasFormat(BranchClipboard::mimeType()) && !mime->hasText())) {
            statusBar()->showMessage(QStringLiteral("剪贴板中没有分支或文本大纲"), 4000);
            return;
        }
        Document next = doc;
        QStringList inserted;
        QString error;
        const QString parent = asSibling && selected != doc.root ? parents.value(selected) : selected;
        const bool pasted = mime->hasFormat(BranchClipboard::mimeType())
            ? BranchClipboard::paste(&next, parent, mime->data(BranchClipboard::mimeType()), &inserted, &error)
            : BranchClipboard::pasteOutline(&next, parent, mime->text(), &inserted, &error);
        if (!pasted || inserted.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("粘贴失败：%1").arg(error), 6000);
            return;
        }
        if (parent != selected) {
            auto &siblings = next.nodes[parent].children;
            const int position = siblings.indexOf(selected) + 1;
            for (int i = 0; i < inserted.size(); ++i) siblings.removeLast();
            for (int i = 0; i < inserted.size(); ++i) siblings.insert(position + i, inserted.at(i));
        }
        checkpoint();
        doc = std::move(next);
        selected = inserted.first();
        additionalSelection.clear();
        changed();
        canvas->ensureVisible(items.value(selected).body, 30, 30);
        statusBar()->showMessage(QStringLiteral("已粘贴 %1 个分支").arg(inserted.size()), 4000);
    }

    void setSelectedSymbol(const QString &id) {
        if (!doc.nodes.contains(selected) || doc.nodes.value(selected).symbolId == id) return;
        if (!id.isEmpty() && !SymbolLibrary::find(id)) {
            statusBar()->showMessage(QStringLiteral("无法找到所选图形符号"), 4000);
            return;
        }
        checkpoint();
        doc.nodes[selected].symbolId = id;
        changed();
    }

    void chooseSymbol() {
        SymbolPickerDialog picker(this, doc.nodes.value(selected).symbolId, appearance.dimensionalSymbols);
        if (picker.exec() == QDialog::Accepted) setSelectedSymbol(picker.selectedSymbolId());
    }

    void changeTopicStyles() {
        QSet<QString> targets = additionalSelection;
        targets.insert(selected);
        TopicStyle common = doc.nodes.value(selected).style;
        for (const auto &id : targets) {
            const auto &style = qAsConst(doc.nodes)[id].style;
            if (common.alignment != style.alignment) common.alignment = TextAlignment::Center;
            if (common.family != style.family) common.family.clear();
            if (common.points != style.points) common.points = 0;
            if (common.weight != style.weight) common.weight = -1;
            if (common.color != style.color) common.color.clear();
        }
        TopicStyleChange change;
        if (editTopicStyle(this, common, targets.size(), &change)) applyTopicStyles(targets, change);
    }

    void applyTopicStyles(QSet<QString> targets, const TopicStyleChange &change) {
        if (change.descendants) {
            auto pending = targets.values();
            while (!pending.isEmpty()) {
                for (const auto &child : qAsConst(doc.nodes)[pending.takeLast()].children) {
                    if (targets.contains(child)) continue;
                    targets.insert(child);
                    pending.append(child);
                }
            }
        }
        Document next = doc;
        int updated = 0;
        for (const auto &id : targets) {
            const auto &old = qAsConst(doc.nodes)[id].style;
            TopicStyle style = old;
            if (change.alignment) style.alignment = change.style.alignment;
            if (change.family) style.family = change.style.family;
            if (change.points) style.points = change.style.points;
            if (change.weight) style.weight = change.style.weight;
            if (change.color) style.color = change.style.color;
            if (style == old) continue;
            next.nodes[id].style = style;
            ++updated;
        }
        if (updated == 0) return;
        if (next.serialize().size() > Document::maxBytes) {
            statusBar()->showMessage(QStringLiteral("应用样式后将超过文件大小上限"), 5000);
            return;
        }
        checkpoint();
        doc = std::move(next);
        changed();
        if (fitMode) fit();
        statusBar()->showMessage(QStringLiteral("已更新 %1 个主题的样式").arg(updated), 5000);
    }

    QFont fontForTopic(const Node &node, bool root, bool branch) const {
        FontChoice choice = appearance.topicFont;
        if (!node.style.family.isEmpty()) choice.family = node.style.family;
        if (node.style.points) choice.points = node.style.points;
        else if (root) choice.points += 4;
        choice.bold = node.style.weight == -1 ? (root || branch || choice.bold) : node.style.weight == 1;
        return fontFor(choice);
    }

    BranchDirection visibleRootLane(const QString &id) const {
        return items.value(id).body->pos().x() < 0 ? BranchDirection::Left : BranchDirection::Right;
    }

    void preserveRootLanes(Document &next) const {
        for (const auto &id : doc.nodes.value(doc.root).children)
            if (items.contains(id)) next.nodes[id].rootLane = visibleRootLane(id);
    }

    void moveNode(const QString &source, const QString &target, int side, bool alignRootLane = false) {
        if (source == doc.root || source == target || !doc.nodes.contains(source) || !doc.nodes.contains(target))
            return;
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
        const bool preserveLanes = alignRootLane && doc.mindMap && doc.branchDirection == BranchDirection::Both &&
                                   (newParent == doc.root || oldParent == doc.root);
        const bool changeLane = preserveLanes && sibling && newParent == doc.root && oldParent == doc.root &&
                                visibleRootLane(source) != visibleRootLane(target);
        if (newParent == oldParent && newOrder == doc.nodes.value(oldParent).children && !changeLane) return;
        checkpoint();
        if (preserveLanes) {
            preserveRootLanes(doc);
            if (sibling && newParent == doc.root) doc.nodes[source].rootLane = visibleRootLane(target);
            else if (newParent != doc.root) doc.nodes[source].rootLane = BranchDirection::Both;
        }
        doc.nodes[oldParent].children.removeOne(source);
        doc.nodes[newParent].children = newOrder;
        doc.nodes[newParent].collapsed = false;
        selected = source;
        changed();
        canvas->ensureVisible(items.value(source).body, 30, 30);
    }

    void copyDraggedBranch(const QString &source, const QString &target, int side) {
        if (source.isEmpty() || target == source || source == doc.root) return;
        const QString parent = side == 0 ? target : parents.value(target);
        QString error;
        const auto data = BranchClipboard::encode(doc, source, &error);
        Document next = doc;
        QStringList inserted;
        if (data.isEmpty() || !BranchClipboard::paste(&next, parent, data, &inserted, &error)) {
            statusBar()->showMessage(error, 5000);
            return;
        }
        if (side != 0) {
            auto &siblings = next.nodes[parent].children;
            siblings.removeLast();
            siblings.insert(siblings.indexOf(target) + (side > 0 ? 1 : 0), inserted.first());
        }
        if (doc.mindMap && doc.branchDirection == BranchDirection::Both && parent == doc.root) {
            preserveRootLanes(next);
            if (side != 0) next.nodes[inserted.first()].rootLane = visibleRootLane(target);
        }
        checkpoint();
        doc = std::move(next);
        selected = inserted.first();
        additionalSelection.clear();
        changed();
        canvas->ensureVisible(items.value(selected).body, 30, 30);
    }

    void reorderSelected(bool earlier) {
        const QString parent = parents.value(selected);
        if (parent.isEmpty()) return;
        const QStringList siblings = doc.nodes.value(parent).children;
        const int target = siblings.indexOf(selected) + (earlier ? -1 : 1);
        if (target >= 0 && target < siblings.size())
            moveNode(selected, siblings.at(target), earlier ? -1 : 1);
    }

    void navigate(Qt::Key key, bool extend = false) {
        const QString parent = parents.value(selected);
        const bool stacked = leafRails.contains(selected);
        const bool onLeft = doc.mindMap && items.value(selected).body->pos().x() < 0;
        const bool towardParent = key == (stacked ? Qt::Key_Left :
            doc.mindMap ? (onLeft ? Qt::Key_Right : Qt::Key_Left) : Qt::Key_Up);
        const bool towardChildren = key == (doc.mindMap ? (onLeft ? Qt::Key_Left : Qt::Key_Right) : Qt::Key_Down);
        QString target;
        if (doc.mindMap && selected == doc.root && (key == Qt::Key_Left || key == Qt::Key_Right)) {
            const auto &children = qAsConst(doc.nodes)[selected].children;
            const auto lane = key == Qt::Key_Right ? BranchDirection::Right : BranchDirection::Left;
            for (const auto &child : children) {
                if (items.contains(child) && visibleRootLane(child) == lane) {
                    target = child;
                    break;
                }
            }
        } else if (towardParent) {
            target = parent;
        } else if (towardChildren && !stacked) {
            const auto &node = qAsConst(doc.nodes)[selected];
            if (!node.collapsed && !node.children.isEmpty()) target = node.children.first();
        } else if (!parent.isEmpty() && (!stacked || key == Qt::Key_Up || key == Qt::Key_Down)) {
            const auto &siblings = qAsConst(doc.nodes)[parent].children;
            const int direction = key == (doc.mindMap || stacked ? Qt::Key_Up : Qt::Key_Left) ? -1 : 1;
            const bool sameLane = doc.mindMap && doc.branchDirection == BranchDirection::Both && parent == doc.root;
            for (int index = siblings.indexOf(selected) + direction; index >= 0 && index < siblings.size(); index += direction) {
                const auto &candidate = siblings.at(index);
                if (!sameLane || visibleRootLane(candidate) == visibleRootLane(selected)) {
                    target = candidate;
                    break;
                }
            }
        }
        if (items.contains(target)) {
            if (extend && target != selected) {
                const QString previous = selected;
                additionalSelection.insert(previous);
                additionalSelection.remove(target);
                selected = target;
                styleNode(previous);
                styleNode(target);
                updateActionStates();
            } else {
                selectNode(target);
            }
            canvas->ensureVisible(items.value(target).body, 25, 25);
        }
    }

    void toggleCollapse() {
        if (doc.nodes.value(selected).children.isEmpty()) return;
        checkpoint();
        doc.nodes[selected].collapsed = !doc.nodes.value(selected).collapsed;
        changed();
    }

    void chooseLayout(bool classic, BranchDirection direction = BranchDirection::Both) {
        if (doc.mindMap == classic && doc.branchDirection == direction) return;
        checkpoint();
        doc.mindMap = classic;
        doc.branchDirection = direction;
        changed();
        fit();
    }

    void updateLayoutActions() {
        QAction *checked = hierarchyAction;
        if (doc.mindMap) {
            checked = doc.branchDirection == BranchDirection::Right ? rightLayoutAction :
                      doc.branchDirection == BranchDirection::Left ? leftLayoutAction : classicAction;
        }
        checked->setChecked(true);
        connectorActions.value(doc.connectorStyle)->setChecked(true);
    }

    void chooseConnectorStyle(ConnectorStyle style) {
        if (doc.connectorStyle == style) return;
        checkpoint();
        doc.connectorStyle = style;
        changed();
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

    int dropSide(const QString &target, QPoint at) const {
        if (target == doc.root) return 0;
        const QRectF bounds = items.value(target).body->sceneBoundingRect();
        const QPointF offset = canvas->mapToScene(at) - bounds.center();
        const qreal relative = doc.mindMap || leafRails.contains(target)
            ? offset.y() / bounds.height() : offset.x() / bounds.width();
        return relative < -0.25 ? -1 : (relative > 0.25 ? 1 : 0);
    }

    struct DropTarget {
        QString id;
        int side = 0;
    };

    DropTarget snapDrop(QPoint at, bool copy) {
        if (dragSource == doc.root || !doc.nodes.contains(dragSource) || editor) return {};
        const auto candidate = [&](const QString &id) {
            DropTarget target;
            if (id.isEmpty() || id == dragSource) return target;
            const int side = dropSide(id, at);
            const QString parent = side == 0 ? id : parents.value(id);
            for (QString ancestor = parent; !copy && !ancestor.isEmpty(); ancestor = parents.value(ancestor))
                if (ancestor == dragSource) return target;
            if (draggedNodes == 0) {
                QVector<QPair<QString, int>> pending{{dragSource, 0}};
                const auto &nodes = qAsConst(doc.nodes);
                while (!pending.isEmpty()) {
                    const auto entry = pending.takeLast();
                    ++draggedNodes;
                    draggedHeight = qMax(draggedHeight, entry.second);
                    for (const auto &child : nodes[entry.first].children)
                        pending.append(qMakePair(child, entry.second + 1));
                }
            }
            if (depth(parent) + 1 + draggedHeight > Document::maxDepth ||
                (copy && draggedNodes > Document::maxNodes - doc.nodes.size())) return target;
            target.id = id;
            target.side = side;
            return target;
        };
        const QString hit = nodeAt(at);
        if (!hit.isEmpty()) return candidate(hit);

        // Keep the snap radius fixed in viewport coordinates as the map zooms.
        constexpr int radius = 16;
        qreal nearest = radius * radius + 1;
        DropTarget result;
        const QRect area(at - QPoint(radius, radius), at + QPoint(radius, radius));
        for (const auto *part : canvas->items(area, Qt::IntersectsItemBoundingRect)) {
            const QString id = part->data(0).toString();
            if (id.isEmpty() || items.value(id).body != part) continue;
            const QRect bounds = canvas->mapFromScene(part->sceneBoundingRect()).boundingRect();
            const QPoint closest(qBound(bounds.left(), at.x(), bounds.right()),
                                 qBound(bounds.top(), at.y(), bounds.bottom()));
            const QPoint delta = closest - at;
            const qreal distance = QPoint::dotProduct(delta, delta);
            if (distance >= nearest) continue;
            const auto target = candidate(id);
            if (target.id.isEmpty()) continue;
            nearest = distance;
            result = target;
        }
        return result;
    }

    void previewDrop(QPoint at, bool copy) {
        const auto target = snapDrop(at, copy);
        if (target.id.isEmpty()) {
            canvas->showDropPreview();
            return;
        }
        QPainterPath marker, guide;
        const QRectF body = items.value(target.id).body->sceneBoundingRect();
        const QRectF bounds = body.adjusted(-4, -4, 4, 4);
        const qreal extension = qMin(36.0, 24 / canvas->transform().m11());
        const bool stacked = leafRails.contains(target.id);
        if (target.side == 0) {
            marker.addRoundedRect(bounds, 10, 10);
        } else if (doc.mindMap || stacked) {
            const qreal y = target.side < 0 ? bounds.top() : bounds.bottom();
            marker.moveTo(bounds.left(), y);
            marker.lineTo(bounds.right(), y);
            const qreal x = stacked ? body.left() : body.center().x();
            guide.moveTo(x, bounds.top() - extension);
            guide.lineTo(x, bounds.bottom() + extension);
        } else {
            const qreal x = target.side < 0 ? bounds.left() : bounds.right();
            marker.moveTo(x, bounds.top());
            marker.lineTo(x, bounds.bottom());
            const qreal y = body.center().y();
            guide.moveTo(bounds.left() - extension, y);
            guide.lineTo(bounds.right() + extension, y);
        }
        canvas->showDropPreview(marker, guide, appearance.highContrast ? accent : copy ? QColor("#3b68a8") : moss);
    }

    void styleNode(const QString &id) {
        if (!items.contains(id)) return;
        const bool active = id == selected || additionalSelection.contains(id);
        const NodeItem &item = items[id];
        const QColor border = appearance.highContrast ? line : tint(item.branchColor, 58);
        const QColor selection = appearance.highContrast ? accent : id == doc.root ? QColor("#8acbb0") : moss;
        item.body->setPen(QPen(active ? selection : border, active ? 2.6 : 1.1));
    }

    void selectNode(const QString &id) {
        if (!items.contains(id) || (selected == id && additionalSelection.isEmpty())) return;
        replaceSelection(QStringList{id});
    }

    void replaceSelection(const QStringList &ids) {
        const QString old = selected;
        const auto oldExtras = additionalSelection;
        selected = ids.first();
        additionalSelection.clear();
        for (int i = 1; i < ids.size(); ++i) additionalSelection.insert(ids.at(i));
        styleNode(old);
        styleNode(selected);
        for (const auto &id : oldExtras) styleNode(id);
        for (const auto &id : additionalSelection) styleNode(id);
        updateActionStates();
    }

    void toggleSelection(const QString &id) {
        if (!items.contains(id)) return;
        if (id == selected) {
            if (additionalSelection.isEmpty()) return;
            selected = *additionalSelection.cbegin();
            additionalSelection.remove(selected);
        } else if (!additionalSelection.remove(id)) {
            additionalSelection.insert(id);
        }
        styleNode(id);
        styleNode(selected);
        updateActionStates();
    }

    QStringList selectedRoots(bool includeRoot) const {
        if (additionalSelection.isEmpty())
            return selected == doc.root && !includeRoot ? QStringList() : QStringList{selected};
        QStringList roots;
        QStringList pending{doc.root};
        while (!pending.isEmpty()) {
            const QString id = pending.takeLast();
            if ((id != doc.root || includeRoot) && (id == selected || additionalSelection.contains(id))) {
                roots.append(id);
                continue;
            }
            const auto &children = doc.nodes[id].children;
            for (auto it = children.crbegin(); it != children.crend(); ++it) pending.append(*it);
        }
        return roots;
    }

    void updateActionStates() {
        for (auto *action : canvasActions) action->setEnabled(!editor);
        const bool branch = !editor && selected != doc.root && doc.nodes.contains(selected);
        for (auto *action : {removeAction, cutAction})
            if (action) action->setEnabled(!editor && (branch || !additionalSelection.isEmpty()));
        if (collapseAction) collapseAction->setEnabled(!editor && !doc.nodes.value(selected).children.isEmpty());
        if (undoAction) undoAction->setEnabled(!editor && !undoStack.isEmpty());
        if (redoAction) redoAction->setEnabled(!editor && !redoStack.isEmpty());
        const QStringList siblings = doc.nodes.value(parents.value(selected)).children;
        const int index = siblings.indexOf(selected);
        if (moveUpAction) moveUpAction->setEnabled(branch && index > 0);
        if (moveDownAction) moveDownAction->setEnabled(branch && index >= 0 && index + 1 < siblings.size());
        if (additionalSelection.isEmpty()) {
            workspaceStats->setText(items.size() == doc.nodes.size()
                ? QStringLiteral("%1 个主题").arg(items.size())
                : QStringLiteral("显示 %1 / %2 个主题").arg(items.size()).arg(doc.nodes.size()));
        } else {
            workspaceStats->setText(QStringLiteral("已选 %1 个主题").arg(additionalSelection.size() + 1));
        }
    }

    void startEdit() {
        if (editor || !items.contains(selected)) return;
        selectNode(selected);
        // A large map can be fitted far below readable text size. Editing should
        // stay usable, without creating an editor as tall as a long topic.
        if (canvas->transform().m11() < 0.65)
            canvas->zoomBy(1 / canvas->transform().m11());
        editId = selected;
        auto *topicEditor = new TopicEditor;
        topicEditor->submit = [this] { finishEdit(); };
        editor = topicEditor;
        editor->setObjectName("topicEditor");
        const auto &node = qAsConst(doc.nodes)[selected];
        editor->setFont(fontForTopic(node, selected == doc.root, parents.value(selected) == doc.root));
        alignTopicText(editor->document(), node.style.alignment);
        editor->setPlainText(node.text);
        editor->setTabChangesFocus(false);
        editor->selectAll();
        editorProxy = scene->addWidget(editor);
        editorProxy->setZValue(5);
        QRectF bounds = items.value(selected).body->sceneBoundingRect();
        bounds.setWidth(qMax(bounds.width(), 230.0));
        const qreal minimumHeight = editor->fontMetrics().height() * 2 + 16;
        const qreal maximumHeight = qMax(minimumHeight, qMin(320.0,
            canvas->viewport()->height() / canvas->transform().m22() - 60));
        bounds.setHeight(qBound(minimumHeight, bounds.height(), maximumHeight));
        bounds.moveCenter(items.value(selected).body->pos());
        editorProxy->setGeometry(bounds);
        canvas->ensureVisible(editorProxy, 30, 30);
        updateActionStates();
        editor->setFocus(Qt::OtherFocusReason);
        auto *accept = new QShortcut(QKeySequence("Ctrl+Return"), editor);
        auto *cancel = new QShortcut(QKeySequence("Escape"), editor);
        connect(accept, &QShortcut::activated, this, [this] { finishEdit(); });
        connect(cancel, &QShortcut::activated, this, [this] { finishEdit(false); });
        connect(editor, &QPlainTextEdit::textChanged, this, [this] { scheduleRecovery(); });
        statusBar()->showMessage(QStringLiteral("编辑主题  ·  Enter 确认  ·  Shift+Enter 换行  ·  Esc 取消"));
    }

    bool finishEdit(bool accept = true) {
        if (!editor) return true;
        if (accept) QApplication::inputMethod()->commit();
        const QString value = editor->toPlainText();
        const QString validationError = accept ? topicTextError(value) : QString();
        if (!validationError.isEmpty()) {
            editor->setFocus();
            statusBar()->showMessage(validationError + QStringLiteral("；编辑内容仍保留，可按 Esc 取消"));
            return false;
        }
        const QString id = editId;
        QGraphicsProxyWidget *proxy = editorProxy;
        editor = nullptr;
        editorProxy = nullptr;
        scene->removeItem(proxy);
        proxy->deleteLater();
        canvas->setFocus();
        updateActionStates();
        if (accept && value != doc.nodes.value(id).text) {
            checkpoint();
            doc.nodes[id].text = value;
            changed();
        } else if (dirty) {
            writeRecovery();
        } else {
            clearRecovery();
        }
        statusBar()->showMessage(canvasHint(), 5000);
        updateActionStates();
        return true;
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
        if (editor) {
            const QString value = editor->toPlainText();
            const QString validationError = topicTextError(value);
            if (!validationError.isEmpty()) {
                statusBar()->showMessage(QStringLiteral("自动恢复暂停：%1；上次有效快照保持不变").arg(validationError), 8000);
                return;
            }
            draft.nodes[editId].text = value;
        }
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
        if (!finishEdit()) return false;
        if (!dirty) return true;
        const auto answer = QMessageBox::warning(this, QStringLiteral("未保存的修改"),
            QStringLiteral("关闭当前导图前，是否保存修改？"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Save) return save();
        return answer == QMessageBox::Discard;
    }

    bool needsSaveAs = false;

    bool save(bool as = false) {
        if (!finishEdit()) return false;
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

    void adoptUnsavedDocument(Document document) {
        doc = std::move(document);
        filePath.clear();
        needsSaveAs = true;
        initializeDocument();
        // A template/import has no saved counterpart until its first Save As.
        saved = Document();
        changed();
        fit();
    }

    void newFromTemplate() {
        const QString id = zhili::chooseBuiltInTemplate(this);
        if (id.isEmpty()) return;
        Document next;
        QString error;
        if (!zhili::loadBuiltInTemplate(id, &next, &error)) {
            QMessageBox::warning(this, QStringLiteral("模板无法打开"), error);
            return;
        }
        if (!confirmLeave()) return;
        adoptUnsavedDocument(std::move(next));
        statusBar()->showMessage(QStringLiteral("已从模板新建，请保存为自己的 .smap 文件"), 6000);
    }

    void openFile() {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开导图"), {},
            QStringLiteral("枝理导图 (*.smap *.smap.json *.json)"));
        if (path.isEmpty() || !confirmLeave()) return;
        QString error;
        if (!loadFrom(path, &error)) QMessageBox::warning(this, QStringLiteral("打开失败"), error);
    }

    bool importFrom(const QString &path, QStringList *warnings, QString *error) {
        Document next;
        if (!importMindMap(path, &next, warnings, error) || !confirmLeave()) return false;
        adoptUnsavedDocument(std::move(next));
        return true;
    }

    void importFile() {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("导入其他导图"), {},
            QStringLiteral("其他导图 (*.mmap *.emmx)"));
        if (path.isEmpty()) return;
        QStringList warnings;
        QString error;
        if (!importFrom(path, &warnings, &error)) {
            if (!error.isEmpty()) QMessageBox::warning(this, QStringLiteral("导入失败"), error);
            return;
        }
        statusBar()->showMessage(QStringLiteral("已导入主题结构和文字，请另存为 .smap；原文件未修改"), 8000);
        if (!warnings.isEmpty()) QMessageBox::information(this, QStringLiteral("导入说明"), warnings.join('\n'));
    }

    QString canvasHint() const {
        return compactToolbar->isHidden()
            ? QStringLiteral("双击编辑  ·  Ctrl+拖动复制分支  ·  Shift+滚轮横移")
            : QStringLiteral("双击编辑");
    }

    void applyAppearance() {
        QApplication::setFont(fontFor(appearance.interfaceFont));
        const bool contrast = appearance.highContrast;
        ink = QColor(contrast ? "#141414" : "#203d36");
        moss = QColor(contrast ? "#122c46" : "#225b49");
        paper = QColor(contrast ? "#ffffff" : "#f6f8f5");
        line = QColor(contrast ? "#41566c" : "#92afa3");
        accent = QColor(contrast ? "#a14300" : "#b97035");
        branchFill = QColor(contrast ? "#e3edf8" : "#e0ede4");
        leafFill = QColor(contrast ? "#ffffff" : "#fffdf7");
        scene->setBackgroundBrush(paper);
        canvas->gridColor = contrast ? QColor() : QColor("#d6e2db");
        canvas->viewport()->update();
        setStyleSheet(QStringLiteral(
            "QMenuBar {background:%1;color:%3;border-bottom:1px solid %2;padding:2px 8px}"
            "QMenuBar::item {padding:3px 10px;border-radius:4px}"
            "QMenuBar::item:selected {background:%4}"
            "QToolBar {background:%1;border:0;border-bottom:1px solid %2;padding:10px 14px;spacing:6px}"
            "QToolButton {color:%3;padding:5px 8px;border-radius:6px}"
            "QToolButton:hover,QToolButton:focus {background:%4}"
            "QToolButton:checked {background:%6;color:white}"
            "QToolButton#workspaceSave {background:%4}"
            "QToolButton#workspaceSave:hover {border:1px solid %6;padding:4px 7px}"
            "QToolButton:disabled {color:#83938d}"
            "QToolBar#compactToolbar {padding:4px 6px;spacing:2px}"
            "QToolBar#compactToolbar QToolButton {padding:4px 5px}"
            "QLabel#workspaceBrand {background:%6;color:white;font-weight:bold;border-radius:8px;padding:6px 10px}"
            "QLabel#documentLabel {color:%3;font-weight:bold;padding-left:6px}"
            "QLabel#saveState {background:%4;color:%6;border-radius:7px;padding:3px 8px}"
            "QFrame#layoutSwitch,QFrame#zoomDock {background:%1;border:1px solid %2;border-radius:8px}"
            "QFrame#layoutSwitch {background:%7}"
            "QStatusBar {background:%7;color:%3;padding:2px 8px}"
            "QStatusBar::item {border:0}"
            "QLabel#workspaceStats {color:%6;padding:0 9px}"
            "QGraphicsView {border:0}"
            "QScrollBar:vertical {background:%7;width:10px;margin:0}"
            "QScrollBar:horizontal {background:%7;height:10px;margin:0}"
            "QScrollBar::handle {background:%2;border-radius:4px;min-width:24px;min-height:24px}"
            "QScrollBar::handle:hover {background:%6}"
            "QScrollBar::add-line,QScrollBar::sub-line {width:0;height:0}"
            "QScrollBar::add-page,QScrollBar::sub-page {background:none}"
            "QPlainTextEdit {background:%1;color:%3;border:2px solid %5;border-radius:5px}")
            .arg(QStringLiteral("#ffffff"), tint(line, contrast ? 0 : 62).name(), ink.name(),
                 branchFill.name(), accent.name(), moss.name(), paper.name()));
        updateTitle();
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
        const QByteArray geometry = QSettings().value("window/geometry").toByteArray();
        if (!geometry.isEmpty() && restoreGeometry(geometry)) {
            // Qt restores maximized state and handles a disconnected monitor.
            // Constrain normal windows as well when desktop geometry shrinks.
            if (!isMaximized() && !isFullScreen()) {
                for (auto *candidate : QGuiApplication::screens()) {
                    if (candidate->availableGeometry().contains(frameGeometry().center())) {
                        screen = candidate;
                        break;
                    }
                }
                setGeometry(boundedWindowGeometry(this->geometry(), screen->availableGeometry(), minimumSize()));
            }
            return;
        }
        const QRect work = screen->availableGeometry();
        const QSize size = QSize(1160, 760).boundedTo(work.size() - QSize(32, 48)).expandedTo(minimumSize());
        setGeometry(QRect(work.center() - QPoint(size.width() / 2, size.height() / 2), size));
    }

    void updateZoomLevel() {
        zoomLevel->setText(QStringLiteral("%1%").arg(qMax(1, qRound(canvas->transform().m11() * 100))));
    }

    void makeZoomControls(QAction *fitAction) {
        workspaceStats = new QLabel(statusBar());
        workspaceStats->setObjectName("workspaceStats");
        statusBar()->addPermanentWidget(workspaceStats);
        auto *dock = new QFrame(statusBar());
        dock->setObjectName("zoomDock");
        auto *row = new QHBoxLayout(dock);
        row->setContentsMargins(3, 2, 3, 2);
        row->setSpacing(2);
        const auto button = [this, dock, row](const QString &name, const QString &label, const QString &tip, qreal factor) {
            auto *control = new QToolButton(dock);
            control->setObjectName(name);
            control->setText(label);
            control->setToolTip(tip);
            control->setAccessibleName(tip);
            connect(control, &QToolButton::clicked, this, [this, factor] { canvas->zoomBy(factor); });
            row->addWidget(control);
        };
        button("zoomOut", QStringLiteral("−"), QStringLiteral("缩小画布"), 1 / 1.25);
        zoomLevel = new QLabel(dock);
        zoomLevel->setObjectName("zoomLevel");
        zoomLevel->setMinimumWidth(fontMetrics().boundingRect(QStringLiteral("350%")).width() + 6);
        zoomLevel->setAlignment(Qt::AlignCenter);
        row->addWidget(zoomLevel);
        button("zoomIn", QStringLiteral("+"), QStringLiteral("放大画布"), 1.25);
        auto *fitButton = new QToolButton(dock);
        fitButton->setDefaultAction(fitAction);
        fitButton->setAccessibleName(fitAction->text());
        row->addWidget(fitButton);
        statusBar()->addPermanentWidget(dock);
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
        const auto previousExtras = additionalSelection;
        selected.clear();
        additionalSelection.clear();
        styleNode(previous);
        for (const auto &id : previousExtras) styleNode(id);
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
        selected = previous;
        additionalSelection = previousExtras;
        styleNode(selected);
        for (const auto &id : additionalSelection) styleNode(id);
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

    bool exportPowerPointTo(const QString &path, QString *error) {
        QMap<QString, QByteArray> images;
        QMap<QString, QByteArray> rendered;
        for (const Node &node : qAsConst(doc.nodes)) {
            if (node.symbolId.isEmpty()) continue;
            const SymbolAsset *asset = SymbolLibrary::find(node.symbolId);
            if (!asset) {
                if (error) *error = QStringLiteral("导图包含当前版本无法识别的图形符号，请先替换或移除后再导出 PowerPoint。");
                return false;
            }
            if (!rendered.contains(asset->id)) {
                QSvgRenderer renderer(SymbolLibrary::svg(*asset, appearance.dimensionalSymbols));
                QImage image(128, 128, QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::transparent);
                QPainter painter(&image);
                renderer.render(&painter);
                painter.end();
                QByteArray png;
                QBuffer buffer(&png);
                buffer.open(QIODevice::WriteOnly);
                if (!renderer.isValid() || !image.save(&buffer, "PNG")) {
                    if (error) *error = QStringLiteral("无法生成 PowerPoint 图形符号");
                    return false;
                }
                rendered.insert(asset->id, png);
            }
            images.insert(node.id, rendered.value(asset->id));
        }
        return exportPptx(doc, path, images, error);
    }

    void exportPowerPoint() {
        QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出可编辑 PowerPoint"), {},
            QStringLiteral("PowerPoint 演示文稿 (*.pptx)"));
        if (path.isEmpty()) return;
        if (!path.endsWith(".pptx", Qt::CaseInsensitive)) path += ".pptx";
        QString error;
        if (!exportPowerPointTo(path, &error)) QMessageBox::warning(this, QStringLiteral("PowerPoint 导出失败"), error);
        else statusBar()->showMessage(QStringLiteral("已导出可编辑 PPTX：含折叠分支和图形符号，大图与长文字自动分页"), 10000);
    }

    QAction *action(QMenu *menu, const QString &name, const char *shortcut,
                    std::function<void()> run, bool canvasOnly = false) {
        QAction *item = menu->addAction(name);
        item->setShortcut(QKeySequence(QString::fromLatin1(shortcut)));
        connect(item, &QAction::triggered, this, [this, run] {
            if (finishEdit()) run();
            else {
                // Checkable actions may toggle before a rejected edit blocks
                // their command. Keep the UI tied to the unchanged document.
                updateLayoutActions();
            }
        });
        if (canvasOnly) canvasActions.append(item);
        return item;
    }

    void makeMenus() {
        auto *file = menuBar()->addMenu(QStringLiteral("文件"));
        action(file, QStringLiteral("新建"), "Ctrl+N", [this] { if (confirmLeave()) resetDocument(); });
        action(file, QStringLiteral("从模板新建…"), "Ctrl+Shift+N", [this] { newFromTemplate(); })->setObjectName("newFromTemplateAction");
        action(file, QStringLiteral("打开"), "Ctrl+O", [this] { openFile(); });
        action(file, QStringLiteral("导入其他导图…"), "", [this] { importFile(); })->setObjectName("importMindMapAction");
        auto *saveAction = action(file, QStringLiteral("保存"), "Ctrl+S", [this] { save(); });
        action(file, QStringLiteral("另存为"), "Ctrl+Shift+S", [this] { save(true); })
            ->setShortcuts({QKeySequence("Ctrl+Shift+S"), QKeySequence("F12")});
        file->addSeparator();
        for (const QString &format : {QString("png"), QString("svg"), QString("pdf")})
            action(file, QStringLiteral("导出 %1").arg(format.toUpper()), "", [this, format] { exportFile(format); });
        auto *pptx = action(file, QStringLiteral("导出可编辑 PowerPoint…"), "", [this] { exportPowerPoint(); });
        pptx->setObjectName("exportPptxAction");
        pptx->setToolTip(QStringLiteral("导出可编辑形状和文字；图形符号可单独移动。包含折叠分支，大图自动分页，长文字附全文页。"));

        auto *edit = menuBar()->addMenu(QStringLiteral("编辑"));
        auto *child = action(edit, QStringLiteral("新增子级"), "Tab", [this] { createNode(true); }, true);
        child->setShortcuts({QKeySequence("Tab"), QKeySequence("Insert"), QKeySequence("Ctrl+Return"),
                             QKeySequence("Ctrl+Enter")});
        siblingAction = action(edit, QStringLiteral("新增同级／主分支"), "Return",
                               [this] { createNode(selected == doc.root); }, true);
        siblingAction->setShortcuts({QKeySequence("Return"), QKeySequence("Enter")});
        action(edit, QStringLiteral("在前面新增同级"), "Shift+Return",
               [this] { createNode(selected == doc.root, true); }, true);
        action(edit, QStringLiteral("插入父主题"), "Ctrl+Shift+Insert", [this] { insertParent(); }, true);
        auto *editTopic = action(edit, QStringLiteral("编辑主题"), "F2", [this] { startEdit(); }, true);
        auto *topicStyle = action(edit, QStringLiteral("主题样式…"), "Ctrl+Shift+F",
                                  [this] { changeTopicStyles(); }, true);
        auto *symbolAction = action(edit, QStringLiteral("设置图形符号…"), "Ctrl+Shift+I", [this] { chooseSymbol(); }, true);
        symbolAction->setObjectName("symbolPickerAction");
        removeAction = action(edit, QStringLiteral("删除分支"), "Delete", [this] { removeSelected(); }, true);
        action(edit, QStringLiteral("移除主题并保留子级"), "Ctrl+Shift+Delete",
               [this] { removeTopicKeepChildren(); }, true);
        collapseAction = action(edit, QStringLiteral("折叠 / 展开"), "Space", [this] { toggleCollapse(); }, true);
        edit->addSeparator();
        auto *copy = action(edit, QStringLiteral("复制分支"), "Ctrl+C", [this] { copyBranch(); }, true);
        copy->setObjectName("copyBranchAction");
        copy->setShortcuts({QKeySequence("Ctrl+C"), QKeySequence("Ctrl+Insert")});
        cutAction = action(edit, QStringLiteral("剪切分支"), "Ctrl+X", [this] { copyBranch(true); }, true);
        cutAction->setObjectName("cutBranchAction");
        cutAction->setShortcuts({QKeySequence("Ctrl+X"), QKeySequence("Shift+Delete")});
        auto *paste = action(edit, QStringLiteral("粘贴分支 / 文本大纲"), "Ctrl+V", [this] { pasteBranch(); }, true);
        paste->setObjectName("pasteBranchAction");
        paste->setShortcuts({QKeySequence("Ctrl+V"), QKeySequence("Shift+Insert")});
        action(edit, QStringLiteral("粘贴为同级"), "Ctrl+Alt+V", [this] { pasteBranch(true); }, true);
        edit->addSeparator();
        auto *undo = action(edit, QStringLiteral("撤销"), "Ctrl+Z", [this] { history(true); }, true);
        undoAction = undo;
        redoAction = action(edit, QStringLiteral("重做"), "Ctrl+Y", [this] { history(false); }, true);
        redoAction->setShortcuts({QKeySequence("Ctrl+Y"), QKeySequence("Ctrl+Shift+Z")});
        auto *find = action(edit, QStringLiteral("查找"), "Ctrl+F", [this] { search(true); });
        action(edit, QStringLiteral("查找下一个"), "F3", [this] { search(false); }, true);

        auto *navigation = edit->addMenu(QStringLiteral("移动选中项"));
        action(navigation, QStringLiteral("向上"), "Up", [this] { navigate(Qt::Key_Up); }, true);
        action(navigation, QStringLiteral("向下"), "Down", [this] { navigate(Qt::Key_Down); }, true);
        action(navigation, QStringLiteral("向左"), "Left", [this] { navigate(Qt::Key_Left); }, true);
        action(navigation, QStringLiteral("向右"), "Right", [this] { navigate(Qt::Key_Right); }, true);
        action(navigation, QStringLiteral("向上扩展选择"), "Shift+Up", [this] { navigate(Qt::Key_Up, true); }, true);
        action(navigation, QStringLiteral("向下扩展选择"), "Shift+Down", [this] { navigate(Qt::Key_Down, true); }, true);
        action(navigation, QStringLiteral("向左扩展选择"), "Shift+Left", [this] { navigate(Qt::Key_Left, true); }, true);
        action(navigation, QStringLiteral("向右扩展选择"), "Shift+Right", [this] { navigate(Qt::Key_Right, true); }, true);
        action(edit, QStringLiteral("选择所有可见主题"), "Ctrl+A", [this] {
            QStringList ids{selected};
            for (auto it = items.cbegin(); it != items.cend(); ++it)
                if (it.key() != selected) ids.append(it.key());
            replaceSelection(ids);
        }, true);
        auto *reorder = edit->addMenu(QStringLiteral("调整分支顺序"));
        moveUpAction = action(reorder, QStringLiteral("前移"), "Ctrl+Shift+Up", [this] { reorderSelected(true); }, true);
        moveDownAction = action(reorder, QStringLiteral("后移"), "Ctrl+Shift+Down", [this] { reorderSelected(false); }, true);

        auto *view = menuBar()->addMenu(QStringLiteral("视图"));
        action(view, QStringLiteral("外观设置…"), "", [this] { changeAppearance(); });
        view->addSeparator();
        classicAction = action(view, QStringLiteral("经典双侧"), "Ctrl+1", [this] { chooseLayout(true); });
        hierarchyAction = action(view, QStringLiteral("结构图"), "Ctrl+2", [this] { chooseLayout(false); });
        rightLayoutAction = action(view, QStringLiteral("向右展开"), "Ctrl+3",
                                   [this] { chooseLayout(true, BranchDirection::Right); });
        leftLayoutAction = action(view, QStringLiteral("向左展开"), "Ctrl+4",
                                  [this] { chooseLayout(true, BranchDirection::Left); });
        const QList<QAction *> layoutsForView{classicAction, rightLayoutAction, leftLayoutAction, hierarchyAction};
        // Keep checked state tied to the actual document, including undo and open.
        auto *group = new QActionGroup(this);
        for (auto *item : layoutsForView) {
            item->setCheckable(true);
            group->addAction(item);
        }
        auto *connectorMenu = view->addMenu(QStringLiteral("连线线型"));
        auto *connectorGroup = new QActionGroup(this);
        const QMap<ConnectorStyle, QString> connectorLabels{
            {ConnectorStyle::Automatic, QStringLiteral("自动搭配")},
            {ConnectorStyle::Orthogonal, QStringLiteral("直角")},
            {ConnectorStyle::Straight, QStringLiteral("直线")},
            {ConnectorStyle::Curved, QStringLiteral("曲线")}
        };
        for (auto it = connectorLabels.cbegin(); it != connectorLabels.cend(); ++it) {
            const ConnectorStyle style = it.key();
            auto *item = action(connectorMenu, it.value(), "", [this, style] { chooseConnectorStyle(style); });
            item->setCheckable(true);
            connectorGroup->addAction(item);
            connectorActions.insert(style, item);
        }
        auto *fitAction = action(view, QStringLiteral("适应窗口"), "Ctrl+0", [this] { fit(); });
        fitAction->setShortcuts({QKeySequence("Ctrl+0"), QKeySequence("Ctrl+F5")});
        action(view, QStringLiteral("居中所选主题"), "Alt+F3", [this] {
            if (items.contains(selected)) canvas->centerOn(items.value(selected).body);
        }, true);
        saveAction->setIcon(toolbarIcon("M4 4h13l3 3v13H4z M8 4v6h8V4 M8 20v-6h8v6"));
        child->setIcon(toolbarIcon("M4 5v8a3 3 0 0 0 3 3h3 M15 11v10 M10 16h10"));
        undo->setIcon(toolbarIcon("M9 5l-5 5 5 5 M4 10h10a5 5 0 0 1 0 10h-3"));
        find->setIcon(toolbarIcon("M16 16l4 4 M18 10a8 8 0 1 1-16 0 8 8 0 0 1 16 0z"));
        fitAction->setIcon(toolbarIcon("M9 4H4v5 M15 4h5v5 M20 15v5h-5 M9 20H4v-5"));
        makeZoomControls(fitAction);

        auto *context = new QMenu(canvas);
        context->setObjectName("topicContextMenu");
        for (auto *item : {editTopic, topicStyle, symbolAction, child, siblingAction}) context->addAction(item);
        context->addSeparator();
        for (auto *item : {moveUpAction, moveDownAction}) context->addAction(item);
        context->addSeparator();
        for (auto *item : {copy, cutAction, paste}) context->addAction(item);
        context->addSeparator();
        for (auto *item : {collapseAction, removeAction, fitAction}) context->addAction(item);
        context->addMenu(connectorMenu);
        canvas->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(canvas, &QWidget::customContextMenuRequested, this, [this, context](const QPoint &at) {
            if (!finishEdit()) return;
            const QString id = nodeAt(at);
            if (!id.isEmpty()) selectNode(id);
            context->popup(canvas->viewport()->mapToGlobal(at));
        });

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
        bar->setIconSize(QSize(19, 19));
        auto *brand = new QLabel(QStringLiteral("枝理"), bar);
        brand->setObjectName("workspaceBrand");
        bar->addWidget(brand);
        documentLabel = new QLabel(bar);
        documentLabel->setObjectName("documentLabel");
        bar->addWidget(documentLabel);
        saveState = new QLabel(bar);
        saveState->setObjectName("saveState");
        bar->addWidget(saveState);
        auto *spacer = new QWidget(bar);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        bar->addWidget(spacer);
        auto *layoutSwitch = new QFrame(bar);
        layoutSwitch->setObjectName("layoutSwitch");
        auto *layoutRow = new QHBoxLayout(layoutSwitch);
        layoutRow->setContentsMargins(3, 3, 3, 3);
        layoutRow->setSpacing(2);
        for (auto *item : layoutsForView) {
            auto *button = new QToolButton(layoutSwitch);
            button->setDefaultAction(item);
            button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            layoutRow->addWidget(button);
        }
        bar->addWidget(layoutSwitch);
        auto *connectorButton = new QToolButton(bar);
        connectorButton->setText(QStringLiteral("线型"));
        connectorButton->setAccessibleName(QStringLiteral("连线线型"));
        connectorButton->setPopupMode(QToolButton::InstantPopup);
        connectorButton->setMenu(connectorMenu);
        bar->addWidget(connectorButton);
        bar->addSeparator();
        topicStyle->setIcon(toolbarIcon("M4 6h16 M12 6v14 M8 20h8 M4 6v3 M20 6v3"));
        for (auto *item : {child, topicStyle, undo, find, saveAction}) bar->addAction(item);
        bar->widgetForAction(saveAction)->setObjectName("workspaceSave");

        compactToolbar = new QToolBar(QStringLiteral("常用（紧凑）"), this);
        compactToolbar->setObjectName("compactToolbar");
        compactToolbar->setMovable(false);
        compactToolbar->setFloatable(false);
        addToolBar(Qt::TopToolBarArea, compactToolbar);
        auto compactButton = [this](QAction *action, const QString &label) {
            auto *button = new QToolButton(compactToolbar);
            button->setText(label);
            button->setIcon(action->icon());
            button->setIconSize(QSize(22, 22));
            button->setAccessibleName(action->text());
            button->setToolTip(action->text());
            button->setEnabled(action->isEnabled());
            connect(button, &QToolButton::clicked, action, &QAction::trigger);
            connect(action, &QAction::changed, button, [button, action] { button->setEnabled(action->isEnabled()); });
            compactToolbar->addWidget(button);
            compactButtons.append(button);
        };
        compactButton(saveAction, QStringLiteral("保存"));
        compactButton(child, QStringLiteral("加子"));
        auto *layouts = new QToolButton(compactToolbar);
        layouts->setText(QStringLiteral("布局"));
        layouts->setIcon(toolbarIcon("M10 3h4v5h-4z M3 16h5v5H3z M16 16h5v5h-5z M12 8v4 M5.5 16v-4h13v4"));
        layouts->setIconSize(QSize(22, 22));
        layouts->setToolTip(QStringLiteral("切换导图布局"));
        layouts->setAccessibleName(QStringLiteral("切换导图布局"));
        layouts->setPopupMode(QToolButton::InstantPopup);
        auto *layoutMenu = new QMenu(layouts);
        layoutMenu->addActions(layoutsForView);
        layouts->setMenu(layoutMenu);
        compactToolbar->addWidget(layouts);
        compactButtons.append(layouts);
        compactButton(topicStyle, QStringLiteral("样式"));
        compactToolbar->hide();
    }

    void render() {
        parents.clear();
        const auto &nodes = qAsConst(doc.nodes);
        for (const Node &node : nodes)
            for (const auto &child : node.children) parents.insert(child, node.id);

        const QVector<QColor> colors{QColor("#258265"), QColor("#5478ac"), QColor("#a36730"),
                                    QColor("#8a609a"), QColor("#a95765"), QColor("#457a87")};
        QMap<QString, QColor> branchColors;
        const auto &branches = nodes[doc.root].children;
        for (int i = 0; i < branches.size(); ++i)
            branchColors.insert(branches.at(i), appearance.highContrast ? line : colors.at(i % colors.size()));
        QMap<QString, QSizeF> sizes;
        QVector<QString> visible{doc.root};
        for (int i = 0; i < visible.size(); ++i) {
            const QString id = visible.at(i);
            const Node &node = nodes[id];
            if (!node.collapsed) for (const auto &child : node.children) visible.append(child);
            if (!items.contains(id)) {
                NodeItem item;
                item.body = scene->addPath(QPainterPath());
                item.label = scene->addText(QString());
                item.badge = scene->addSimpleText(QString());
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
            NodeItem &item = items[id];
            const bool root = id == doc.root;
            const QString parentId = parents.value(id);
            const bool branch = parentId == doc.root;
            item.branchColor = root ? moss : (branch ? branchColors.value(id) : items.value(parentId).branchColor);
            const SymbolAsset *asset = node.symbolId.isEmpty() ? nullptr : SymbolLibrary::find(node.symbolId);
            if (asset) {
                const QString key = asset->id + (appearance.dimensionalSymbols ? "/dimensional" : "/flat")
                    + (root ? "/dark" : "");
                if (!symbolRenderers.contains(key))
                    symbolRenderers.insert(key, new QSvgRenderer(
                        SymbolLibrary::svg(*asset, appearance.dimensionalSymbols, root), this));
                if (!item.symbol) {
                    item.symbol = new QGraphicsSvgItem;
                    scene->addItem(item.symbol);
                    item.symbol->setZValue(2);
                    // Keep SVG/PDF exports vector rather than baking view caches.
                    item.symbol->setCacheMode(QGraphicsItem::NoCache);
                    item.symbol->setData(0, id);
                    item.symbol->setAcceptedMouseButtons(Qt::NoButton);
                }
                item.symbol->setSharedRenderer(symbolRenderers.value(key));
                const QSizeF iconSize = item.symbol->boundingRect().size();
                const qreal scale = 32.0 / qMax(1.0, qMax(iconSize.width(), iconSize.height()));
                item.symbol->setScale(scale);
            }
            if (item.symbol) item.symbol->setVisible(asset != nullptr);
            const QString &display = node.text;
            const QFont font = fontForTopic(node, root, branch);
            const qreal maxWidth = doc.mindMap || root ? 248 : 166;
            auto *textDocument = item.label->document();
            const bool realign = textDocument->defaultTextOption().alignment() != topicAlignment(node.style.alignment);
            if (item.label->toPlainText() != display || item.labelLimit != maxWidth || item.label->font() != font || realign) {
                item.labelLimit = maxWidth;
                item.label->setFont(font);
                alignTopicText(textDocument, node.style.alignment);
                item.label->setTextWidth(-1);
                item.label->setPlainText(display);
                item.label->setTextWidth(qBound(root ? 136.0 : 70.0, item.label->boundingRect().width(), maxWidth));
                // Qt lays out long multi-paragraph documents lazily. Force the
                // final block before sizing the node/export scene, otherwise
                // thousands of lines can be clipped to an unfinished height.
                textDocument->documentLayout()->blockBoundingRect(textDocument->lastBlock());
                // Refresh QGraphicsTextItem's cached rectangle synchronously;
                // its lazy document-size notification otherwise arrives later.
                item.label->setDocument(textDocument);
            }
            item.label->setDefaultTextColor(node.style.color.isEmpty() ? (root ? QColor(Qt::white) : ink)
                                                                      : QColor(node.style.color));
            const QSizeF label = item.label->boundingRect().size();
            const qreal padding = root ? 44 : branch ? 28 : doc.mindMap ? 20 : 12;
            const QSizeF size(label.width() + padding + (asset ? 42 : 0),
                              qMax(root ? 64.0 : branch ? 48.0 : asset ? 42.0 : 34.0,
                                   label.height() + (root ? 18 : branch ? 12 : 8)));
            sizes[id] = size;
            QPainterPath shape;
            const qreal radius = root ? 16 : doc.mindMap ? (branch ? 13 : 14) : 7;
            shape.addRoundedRect(QRectF(QPointF(-size.width() / 2, -size.height() / 2), size), radius, radius);
            item.body->setPath(shape);
            if (appearance.highContrast) {
                item.body->setBrush(root ? moss : branch ? branchFill : leafFill);
            } else if (root) {
                QLinearGradient fill(shape.boundingRect().topLeft(), shape.boundingRect().bottomRight());
                fill.setColorAt(0, QColor("#173d33"));
                fill.setColorAt(1, moss);
                item.body->setBrush(fill);
            } else {
                item.body->setBrush(tint(item.branchColor, branch ? 79 : doc.mindMap ? 94 : 98));
            }
            item.badge->setText(QStringLiteral("+%1").arg(node.children.size()));
            item.badge->setBrush(appearance.highContrast ? accent : item.branchColor.darker(125));
            item.badge->setVisible(node.collapsed && !node.children.isEmpty());
        }

        for (auto it = items.begin(); it != items.end();) {
            if (sizes.contains(it.key())) { ++it; continue; }
            delete it->body;
            delete it->label;
            delete it->badge;
            delete it->symbol;
            it = items.erase(it);
        }
        for (auto it = edges.begin(); it != edges.end();) {
            if (sizes.contains(it.key())) { ++it; continue; }
            delete it.value();
            it = edges.erase(it);
        }

        QVector<qreal> layerSizes;
        const auto positions = layoutNodes(doc, sizes, &leafRails, &layerSizes);
        if (!positions.contains(selected)) selected = doc.root;
        additionalSelection.remove(selected);
        for (auto it = additionalSelection.begin(); it != additionalSelection.end();) {
            if (!positions.contains(*it)) it = additionalSelection.erase(it);
            else ++it;
        }
        QPainterPath rail;
        qreal rootRailY = 0;
        const bool automatic = doc.connectorStyle == ConnectorStyle::Automatic;
        const ConnectorStyle connectorStyle = automatic
            ? (doc.mindMap ? ConnectorStyle::Curved : ConnectorStyle::Orthogonal) : doc.connectorStyle;
        const bool useRootRail = !doc.mindMap && connectorStyle == ConnectorStyle::Orthogonal;
        if (useRootRail && !nodes[doc.root].collapsed && !branches.isEmpty()) {
            const qreal rootBottom = layerSizes.first() / 2;
            const auto first = positions.value(branches.first());
            qreal left = qMin(0.0, first.x());
            qreal right = qMax(0.0, first.x());
            for (const auto &child : branches) {
                const QPointF position = positions.value(child);
                left = qMin(left, position.x());
                right = qMax(right, position.x());
            }
            rootRailY = (rootBottom + first.y() - layerSizes.at(1) / 2) / 2;
            rail.moveTo(0, rootBottom);
            rail.lineTo(0, rootRailY);
            rail.moveTo(left, rootRailY);
            rail.lineTo(right, rootRailY);
        }
        rootRail->setPath(rail);
        rootRail->setPen(QPen(line, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QMap<QString, int> depths;
        for (const QString &id : visible) {
            const QString parent = parents.value(id);
            const int level = id == doc.root ? 0 : depths.value(parent) + 1;
            depths.insert(id, level);
            const QPointF point = positions.value(id);
            const NodeItem item = items.value(id);
            const QSizeF label = item.label->boundingRect().size();
            const QSizeF size = sizes.value(id);
            item.body->setPos(point);
            const bool symbolVisible = item.symbol && item.symbol->isVisible();
            item.label->setPos(point - QPointF(label.width() / 2 - (symbolVisible ? 21 : 0), label.height() / 2));
            if (symbolVisible) {
                const auto iconSize = item.symbol->boundingRect().size() * item.symbol->scale();
                item.symbol->setPos(point + QPointF(-label.width() / 2 - 21 + (32 - iconSize.width()) / 2,
                                                     -iconSize.height() / 2));
            }
            item.badge->setPos(point + QPointF(size.width() / 2 + 5, -item.badge->boundingRect().height() / 2));
            styleNode(id);
            if (id == doc.root) continue;

            QPointF a = positions.value(parent);
            QPointF b = point;
            QPainterPath path;
            if (doc.mindMap) {
                const qreal direction = b.x() > a.x() ? 1 : -1;
                const qreal fromLane = a.x() + direction * layerSizes.at(level - 1) / 2;
                const qreal toLane = b.x() - direction * layerSizes.at(level) / 2;
                a.rx() += direction * sizes.value(parent).width() / 2;
                b.rx() -= direction * size.width() / 2;
                path = layerConnector(a, b, fromLane, toLane, true, connectorStyle, false);
            } else if (useRootRail && parent == doc.root) {
                b.ry() -= size.height() / 2;
                path.moveTo(b.x(), rootRailY);
                path.lineTo(b);
            } else if (leafRails.contains(id)) {
                a.ry() += sizes.value(parent).height() / 2;
                b.rx() -= size.width() / 2;
                const qreal rail = leafRails.value(id);
                const qreal start = a.y() + 23;
                path.moveTo(a);
                path.lineTo(a.x(), start);
                path.lineTo(rail, start);
                if (connectorStyle == ConnectorStyle::Curved) {
                    path.cubicTo(rail, b.y(), rail, b.y(), b.x(), b.y());
                } else {
                    if (connectorStyle == ConnectorStyle::Orthogonal) path.lineTo(rail, b.y());
                    path.lineTo(b);
                }
            } else {
                const qreal fromLane = a.y() + layerSizes.at(level - 1) / 2;
                const qreal toLane = b.y() - layerSizes.at(level) / 2;
                a.ry() += sizes.value(parent).height() / 2;
                b.ry() -= size.height() / 2;
                path = layerConnector(a, b, fromLane, toLane, false, connectorStyle, automatic);
            }
            if (!edges.contains(id)) edges.insert(id, scene->addPath(path));
            edges.value(id)->setPath(path);
            const bool primary = parent == doc.root;
            const QColor connector = appearance.highContrast ? line : tint(item.branchColor, primary ? 16 : 44);
            edges.value(id)->setPen(QPen(connector, primary ? 2.9 : 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        }
        scene->setSceneRect(scene->itemsBoundingRect().adjusted(-100, -100, 100, 100));
        updateLayoutActions();
        updateActionStates();
        updateTitle();
    }

    void updateToolbarMode(int width) {
        if (!wideToolbar || !compactToolbar) return;
        const bool compact = width < qMax(840, wideToolbar->sizeHint().width() + 24);
        const QString priorHint = canvasHint();
        if (compact) {
            // Leave room for platform toolbar margins and the layout menu indicator.
            int textWidth = 54;
            for (const auto *button : compactButtons)
                textWidth += button->fontMetrics().boundingRect(button->text()).width() + 24;
            const auto buttonStyle = width < textWidth ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextOnly;
            for (auto *button : compactButtons) button->setToolButtonStyle(buttonStyle);
        }
        wideToolbar->setVisible(!compact);
        compactToolbar->setVisible(compact);
        workspaceStats->setVisible(!compact);
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
            QSettings().setValue("window/geometry", saveGeometry());
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
