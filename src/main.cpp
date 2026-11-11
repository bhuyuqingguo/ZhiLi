#include "appearance.h"
#include "branch_export.h"
#include "clipboard.h"
#include "document.h"
#include "exchange.h"
#include "layout.h"
#include "symbols.h"
#include "symbolpicker.h"
#include "templates.h"
#include "template_dialog.h"
#include "pptx.h"
#include "importer.h"
#include "shortcuts.h"
#include "startup.h"

#include <QtWidgets>
#include <QtSvg/QSvgGenerator>
#include <QtSvg/QGraphicsSvgItem>
#include <QtSvg/QSvgRenderer>
#include <QPdfWriter>
#include <QtMath>
#include <algorithm>
#include <functional>
#include <memory>

namespace {
constexpr auto boxShortcut = "Ctrl+Alt+B";
constexpr auto convertShortcut = "Ctrl+Shift+M";
constexpr auto pasteStyleShortcut = "Ctrl+Shift+V";

QString shortcutText(const char *keys) {
    return QKeySequence(QString::fromLatin1(keys)).toString(QKeySequence::NativeText);
}

QString copyModifierText() {
    QString modifier = shortcutText("Ctrl+A");
    modifier.chop(1);
    if (modifier.endsWith('+')) modifier.chop(1);
    return modifier;
}

template<typename CreateMimeData>
bool writeClipboard(CreateMimeData create) {
    auto *clipboard = QApplication::clipboard();
    clipboard->setMimeData(create());
#ifdef Q_OS_WIN
    // Qt may exhaust its OLE retries without reporting failure to the caller.
    if (QGuiApplication::platformName() == QStringLiteral("windows") && !clipboard->ownsClipboard()) {
        clipboard->setMimeData(create());
        return clipboard->ownsClipboard();
    }
#endif
    return true;
}

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
        setResizeAnchor(AnchorViewCenter);
        edgeScroll.setInterval(30);
        connect(&edgeScroll, &QTimer::timeout, this, [this] {
            if (!nodeDrag) { edgeScroll.stop(); return; }
            const auto step = [](int position, int extent) {
                if (position < 32) return -qBound(2, (32 - position) / 2, 24);
                if (position > extent - 32) return qBound(2, (position - extent + 32) / 2, 24);
                return 0;
            };
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() + step(dragPoint.x(), viewport()->width()));
            verticalScrollBar()->setValue(verticalScrollBar()->value() + step(dragPoint.y(), viewport()->height()));
            dragOver(dragPoint, QApplication::keyboardModifiers() & Qt::ControlModifier);
        });
    }

    std::function<bool(QPoint, Qt::KeyboardModifiers)> press;
    std::function<bool(QPoint)> doubleClick;
    std::function<void(QPoint, bool)> drop;
    std::function<void(QPoint, bool)> dragOver;
    std::function<void()> dragEnded;
    std::function<void(bool)> topicClicked;
    std::function<bool(QPoint)> canStartArea;
    std::function<void(QRect)> selectArea;
    std::function<void(bool)> areaDragChanged;
    std::function<void()> zoomChanged;
    std::function<void()> viewportResized;
    QColor gridColor;

    void setAreaSelection(bool enabled) { areaSelection = enabled; }
    bool areaSelectionEnabled() const { return areaSelection; }

    bool cancelGesture() {
        const bool active = nodeDrag || middlePanning || (band && band->isVisible());
        if (!active) return false;
        nodeDrag = middlePanning = false;
        edgeScroll.stop();
        if (band && band->isVisible()) { band->hide(); areaDragChanged(false); }
        viewport()->unsetCursor();
        dragEnded();
        return true;
    }

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
        if (event->type() == QEvent::ShortcutOverride &&
            static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape &&
            (nodeDrag || middlePanning || (band && band->isVisible()))) {
            event->accept();
            return true;
        }
        if (event->type() == QEvent::ContextMenu && rightWheelUsed &&
            static_cast<QContextMenuEvent *>(event)->reason() == QContextMenuEvent::Mouse) {
            rightWheelUsed = false;
            return true;
        }
        return QGraphicsView::event(event);
    }

    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape && cancelGesture()) { event->accept(); return; }
        QGraphicsView::keyPressEvent(event);
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
            if ((areaSelection || (event->modifiers() & Qt::ShiftModifier)) && canStartArea(origin)) {
                if (!band) band = new QRubberBand(QRubberBand::Rectangle, viewport());
                setFocus();
                band->setGeometry(QRect(origin, QSize()));
                band->show();
                areaDragChanged(true);
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
            if ((event->pos() - origin).manhattanLength() >= QApplication::startDragDistance()) {
                dragPoint = event->pos();
                edgeScroll.start();
                dragOver(event->pos(), event->modifiers() & Qt::ControlModifier);
            } else {
                edgeScroll.stop();
                dragEnded();
            }
            event->accept();
            return;
        }
        QGraphicsView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && band && band->isVisible()) {
            band->hide();
            areaDragChanged(false);
            if ((event->pos() - origin).manhattanLength() >= QApplication::startDragDistance())
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
            edgeScroll.stop();
            dragEnded();
            if ((event->pos() - origin).manhattanLength() >= QApplication::startDragDistance())
                drop(event->pos(), event->modifiers() & Qt::ControlModifier);
            else
                topicClicked(event->modifiers() & Qt::ControlModifier);
            event->accept();
            return;
        }
        QGraphicsView::mouseReleaseEvent(event);
    }

    void leaveEvent(QEvent *event) override {
        edgeScroll.stop();
        dragEnded();
        QGraphicsView::leaveEvent(event);
    }

private:
    QPainterPath dropMarker, dropGuide;
    QColor dropColor;
    QPoint origin;
    QPoint panPoint;
    QPoint dragPoint;
    QTimer edgeScroll;
    QRubberBand *band = nullptr;
    bool areaSelection = false;
    bool nodeDrag = false;
    bool middlePanning = false;
    bool rightWheelUsed = false;
};

class OverviewCanvas : public QGraphicsView {
public:
    explicit OverviewCanvas(QGraphicsScene *scene, QWidget *parent) : QGraphicsView(scene, parent) {
        setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        setDragMode(ScrollHandDrag);
        setTransformationAnchor(AnchorUnderMouse);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }

    std::function<void(const QString &)> topicClicked;

    void refresh(bool reset = false) {
        if (reset) autoFit = true;
        if (!isVisible() || !autoFit) return;
        fitInView(scene()->itemsBoundingRect().adjusted(-30, -30, 30, 30), Qt::KeepAspectRatio);
        const qreal zoom = transform().m11();
        if (zoom > 1.25) scale(1.25 / zoom, 1.25 / zoom);
    }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QGraphicsView::resizeEvent(event);
        refresh();
    }

    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) pressedAt = event->pos();
        QGraphicsView::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        QGraphicsView::mouseReleaseEvent(event);
        if (event->button() != Qt::LeftButton) return;
        if ((event->pos() - pressedAt).manhattanLength() >= QApplication::startDragDistance()) {
            autoFit = false;
            return;
        }
        for (auto *item : items(event->pos())) {
            const QString id = item->data(0).toString();
            if (!id.isEmpty()) {
                topicClicked(id);
                return;
            }
        }
    }

    void wheelEvent(QWheelEvent *event) override {
        const qreal zoom = transform().m11();
        const int delta = event->angleDelta().y();
        if (delta) {
            autoFit = false;
            const qreal next = qBound(qMin(0.05, zoom), zoom * qPow(1.12, delta / 120.0), 3.5);
            scale(next / zoom, next / zoom);
        }
        event->accept();
    }

private:
    QPoint pressedAt;
    bool autoFit = true;
};

struct NodeItem {
    QGraphicsPathItem *body;
    QGraphicsTextItem *label;
    QGraphicsSimpleTextItem *badge;
    QGraphicsSimpleTextItem *note = nullptr;
    QGraphicsSvgItem *symbol = nullptr;
    QColor branchColor;
    qreal labelLimit = 0;
};

class TopicEditor : public QPlainTextEdit {
public:
    std::function<void()> submit;
    std::function<void()> split;

protected:
    void inputMethodEvent(QInputMethodEvent *event) override {
        composing = !event->preeditString().isEmpty();
        QPlainTextEdit::inputMethodEvent(event);
    }

    void keyPressEvent(QKeyEvent *event) override {
        const auto key = event->key();
        const auto modifiers = event->modifiers() & ~Qt::KeypadModifier;
        if (modifiers == Qt::ControlModifier && (key == Qt::Key_Left || key == Qt::Key_Right)) {
            moveCursor(key == Qt::Key_Left ? QTextCursor::Start : QTextCursor::End);
            event->accept();
            return;
        }
        if ((key == Qt::Key_Return || key == Qt::Key_Enter) && modifiers == (Qt::ShiftModifier | Qt::AltModifier)) {
            event->accept();
            if (!composing && split) split();
            return;
        }
        if ((key == Qt::Key_Return || key == Qt::Key_Enter) &&
            (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier ||
             modifiers == Qt::ControlModifier || modifiers == Qt::AltModifier)) {
            event->accept();
            if (composing) return;
            if (modifiers != Qt::NoModifier) insertPlainText(QStringLiteral("\n"));
            else submit();
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }

private:
    bool composing = false;
};

class OutlineTree : public QTreeWidget {
public:
    std::function<bool(int, Qt::KeyboardModifiers)> command;
    std::function<void()> editingChanged;
    bool editingText() const { return state() == QAbstractItemView::EditingState; }
protected:
    bool focusNextPrevChild(bool next) override {
        return editingText() ? QTreeWidget::focusNextPrevChild(next) : false;
    }
    bool event(QEvent *event) override {
        if (event->type() == QEvent::ShortcutOverride) {
            auto *key = static_cast<QKeyEvent *>(event);
            const int code = key->key();
            if (!(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
                (code == Qt::Key_Tab || code == Qt::Key_Backtab || code == Qt::Key_Return || code == Qt::Key_Enter ||
                 code == Qt::Key_Insert || code == Qt::Key_Delete || code == Qt::Key_Backspace || code == Qt::Key_F2 || code == Qt::Key_Escape)) {
                event->accept(); return true;
            }
        }
        return QTreeWidget::event(event);
    }
    bool edit(const QModelIndex &index, QAbstractItemView::EditTrigger trigger, QEvent *event) override {
        const bool result = QTreeWidget::edit(index, trigger, event);
        if (editingChanged) editingChanged();
        return result;
    }
    void closeEditor(QWidget *editor, QAbstractItemDelegate::EndEditHint hint) override {
        QTreeWidget::closeEditor(editor, hint);
        if (editingChanged) editingChanged();
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (!editingText() && command && command(event->key(), event->modifiers())) { event->accept(); return; }
        QTreeWidget::keyPressEvent(event);
    }
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
    explicit Window(bool enableRecovery = true, Appearance initialAppearance = Appearance::load())
        : appearance(std::move(initialAppearance)) {
        setMinimumSize(360, 280);
        scene = new QGraphicsScene(this);
        focusSplitter = new QSplitter(Qt::Horizontal, this);
        focusSplitter->setObjectName("focusSplitter");
        focusSplitter->setChildrenCollapsible(false);
        focusSplitter->setHandleWidth(6);
        overviewPane = new QWidget(focusSplitter);
        auto *overviewLayout = new QVBoxLayout(overviewPane);
        overviewLayout->setContentsMargins(0, 0, 0, 0);
        overviewLayout->setSpacing(0);
        auto *overviewTitle = new QLabel(QStringLiteral("全图概览  ·  单击主题切换分支"), overviewPane);
        overviewTitle->setObjectName("focusPaneTitle");
        overviewTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        overviewTitle->setToolTip(overviewTitle->text());
        overviewLayout->addWidget(overviewTitle);
        overviewScene = new QGraphicsScene(this);
        overview = new OverviewCanvas(overviewScene, overviewPane);
        overview->setObjectName("fullMapOverview");
        overview->setAccessibleName(QStringLiteral("全图概览"));
        overview->topicClicked = [this](const QString &id) {
            if (id != doc.root && id != focusRoot && finishEdit()) focusBranch(id);
        };
        overviewLayout->addWidget(overview, 1);
        auto *branchPane = new QWidget(focusSplitter);
        auto *branchLayout = new QVBoxLayout(branchPane);
        branchLayout->setContentsMargins(0, 0, 0, 0);
        branchLayout->setSpacing(0);
        focusTitle = new QLabel(branchPane);
        focusTitle->setObjectName("focusPaneTitle");
        focusTitle->setTextFormat(Qt::PlainText);
        focusTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        branchLayout->addWidget(focusTitle);
        canvas = new Canvas(scene, branchPane);
        canvas->setObjectName("mapCanvas");
        branchLayout->addWidget(canvas, 1);
        focusSplitter->addWidget(overviewPane);
        focusSplitter->addWidget(branchPane);
        focusSplitter->setStretchFactor(0, 2);
        focusSplitter->setStretchFactor(1, 3);
        overviewPane->hide();
        focusTitle->hide();
        setCentralWidget(focusSplitter);
        rootRail = scene->addPath(QPainterPath());
        rootRail->setZValue(-1);
        rootRail->setAcceptedMouseButtons(Qt::NoButton);
        canvas->press = [this](QPoint at, Qt::KeyboardModifiers modifiers) {
            decorationDrag.clear();
            dragOrigin = canvas->mapToScene(at);
            dragSource.clear();
            draggedRoots.clear();
            draggedRootSet.clear();
            draggedNodes = 0;
            draggedHeight = 0;
            if (editor && itemAt(at, editorProxy)) return false;
            const QString id = nodeAt(at);
            if (!finishEdit()) return false;
            if (placingFloating && id.isEmpty()) {
                placingFloating = false; canvas->unsetCursor(); createFloating(dragOrigin); return true;
            }
            if (!relationshipSource.isEmpty()) {
                if (!id.isEmpty() && id != relationshipSource) {
                    addRelationship(relationshipSource, id, relationshipStraight);
                    relationshipSource.clear(); canvas->unsetCursor();
                    updateActionStates();
                }
                return true;
            }
            if (id.isEmpty()) {
                const QString decoration = decorationAt(at);
                if (!decoration.isEmpty()) {
                    selectedDecoration = decoration; decorationDrag = decoration; render(); return true;
                }
                if (!selectedDecoration.isEmpty()) { selectedDecoration.clear(); renderDecorations(scene, items); }
            }
            if (id.isEmpty()) return false;
            selectedDecoration.clear();
            dragSource = id;
            if (modifiers == Qt::NoModifier && items.value(id).badge->isVisible() && itemAt(at, items.value(id).badge)) {
                selectNode(id);
                toggleCollapse();
                dragSource.clear();
                return true;
            }
            const bool copy = modifiers & Qt::ControlModifier;
            if (id == viewRoot()) {
                if (!copy) selectNode(id);
                return copy;
            }
            if (id == selected || additionalSelection.contains(id)) {
                draggedRoots = selectedRoots(false, viewRoot());
            } else {
                draggedRoots.append(id);
                if (!copy) selectNode(id);
            }
            for (const auto &root : draggedRoots) draggedRootSet.insert(root);
            return true;
        };
        canvas->topicClicked = [this](bool toggle) {
            if (dragSource.isEmpty()) return;
            if (toggle) toggleSelection(dragSource);
            else selectNode(dragSource);
        };
        canvas->canStartArea = [this](QPoint at) { return !editor && nodeAt(at).isEmpty(); };
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
                const QString decoration = decorationAt(at);
                if (!decoration.isEmpty()) { selectedDecoration = decoration; editDecoration(); }
                else createFloating(canvas->mapToScene(at));
            } else {
                selectNode(id);
                auto *note = items.value(id).note;
                if (note && itemAt(at, note)) editNotes();
                else startEdit();
            }
            return true;
        };
        canvas->drop = [this](QPoint end, bool copy) {
            if (!decorationDrag.isEmpty()) { moveDecoration(canvas->mapToScene(end) - dragOrigin); return; }
            const auto target = snapDrop(end, copy);
            if (target.id.isEmpty()) {
                if (nodeAt(end).isEmpty()) floatBranches(draggedRoots, canvas->mapToScene(end) - dragOrigin, copy);
                return;
            }
            if (copy) copyDraggedBranches(draggedRoots, target.id, target.side);
            else moveBranches(draggedRoots, target.id, target.side, true);
        };
        canvas->areaDragChanged = [this](bool active) { setGestureHint(active ? GestureHint::Box : GestureHint::None); };
        canvas->dragOver = [this](QPoint at, bool copy) {
            if (!decorationDrag.isEmpty()) { previewDecoration(canvas->mapToScene(at) - dragOrigin); return; }
            setGestureHint(copy ? GestureHint::Copy : GestureHint::Move);
            if (snapDrop(at, copy).id.isEmpty() && nodeAt(at).isEmpty() && focusRoot.isEmpty()) {
                QPainterPath marker;
                for (const auto &id : draggedRoots) if (items.contains(id))
                    marker.addRoundedRect(items.value(id).body->sceneBoundingRect().translated(canvas->mapToScene(at) - dragOrigin), 12, 12);
                canvas->showDropPreview(marker, {}, moss);
                statusBar()->showMessage(QStringLiteral("松开转为浮动分支 · Ctrl 拖拽复制 · Esc 取消"));
                return;
            }
            previewDrop(at, copy);
        };
        canvas->dragEnded = [this] {
            render();
            canvas->showDropPreview();
            if (gestureHint != GestureHint::Box) setGestureHint(GestureHint::None);
        };
        makeMenus();
        shortcutSettings.changed = [this] { updateShortcutHint(); };
        shortcutSettings.load();
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
        connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *) { updateShortcutHint(); });
        if (enableRecovery) QTimer::singleShot(0, this, [this] { setupRecovery(); });
    }

    ~Window() override {
        // QMainWindow tears down child widgets before QObject disconnects its
        // receivers. Focus/outline notifications must not access those widgets
        // or the already-destroyed Window members during that interval.
        QObject::disconnect(qApp, nullptr, this, nullptr);
        if (outlineTree) {
            outlineTree->command = {};
            outlineTree->editingChanged = {};
            QObject::disconnect(outlineTree, nullptr, this, nullptr);
        }
        if (outlineDock) QObject::disconnect(outlineDock, nullptr, this, nullptr);
    }

private:
    enum class GestureHint { None, Box, Move, Copy };
    GestureHint gestureHint = GestureHint::None;
    QLabel *shortcutHint = nullptr;

    struct Revision {
        Document document;
        QString selected;
    };

    Appearance appearance;
    QColor ink, moss, paper, line, accent, branchFill, leafFill;
    QToolBar *wideToolbar = nullptr;
    int wideToolbarNaturalWidth = 0;
    QToolBar *compactToolbar = nullptr;
    QVector<QToolButton *> compactButtons;
    QLabel *documentLabel = nullptr;
    QLabel *saveState = nullptr;
    QLabel *zoomLevel = nullptr;
    QLabel *workspaceStats = nullptr;
    QToolButton *leaveFocusButton = nullptr;
    QTimer fitTimer;
    bool fitMode = false;
    QGraphicsScene *scene;
    QGraphicsPathItem *rootRail;
    Canvas *canvas;
    QSplitter *focusSplitter;
    QWidget *overviewPane;
    QLabel *focusTitle;
    OverviewCanvas *overview;
    QGraphicsScene *overviewScene;
    QGraphicsPathItem *overviewRail = nullptr;
    QMap<QString, NodeItem> overviewItems;
    QMap<QString, QGraphicsPathItem *> overviewEdges;
    Document doc;
    Document saved;
    QString selected, filePath, query, lastMatch;
    QString focusRoot;
    QTransform fullMapTransform;
    QPointF fullMapCenter;
    bool fullMapFit = false;
    TopicStyle copiedStyle;
    bool hasCopiedStyle = false;
    QString dragSource;
    QStringList draggedRoots;
    QSet<QString> draggedRootSet;
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
    QString selectedDecoration, decorationDrag, relationshipSource;
    bool placingFloating = false, relationshipStraight = false;
    QPointF dragOrigin;
    ShortcutSettings shortcutSettings;
    QDockWidget *outlineDock = nullptr;
    OutlineTree *outlineTree = nullptr;
    QMap<QString, QTreeWidgetItem *> outlineItems;
    int outlineGeneration = 0;
    QAction *classicAction = nullptr;
    QAction *hierarchyAction = nullptr;
    QAction *rightLayoutAction = nullptr;
    QAction *leftLayoutAction = nullptr;
    QMap<ConnectorStyle, QAction *> connectorActions;
    QAction *siblingAction = nullptr;
    QAction *removeAction = nullptr;
    QAction *cutAction = nullptr;
    QAction *collapseAction = nullptr;
    QAction *duplicateBranchAction = nullptr;
    QVector<QAction *> sortActions;
    QAction *undoAction = nullptr;
    QAction *redoAction = nullptr;
    QAction *moveUpAction = nullptr;
    QAction *moveDownAction = nullptr;
    QAction *focusAction = nullptr;
    QAction *leaveFocusAction = nullptr;
    QAction *pasteStyleAction = nullptr;
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
        selectedDecoration.clear(); relationshipSource.clear(); placingFloating = false;
        focusRoot.clear();
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
        doc.pruneDecorations();
        dirty = doc.serialize() != saved.serialize();
        if (dirty) scheduleRecovery();
        else clearRecovery();
        render();
    }

    QString viewRoot() const { return focusRoot.isEmpty() ? doc.root : focusRoot; }

    void focusBranch() { focusBranch(selected); }

    void focusBranch(const QString &id) {
        if (id == doc.root || id == focusRoot || !doc.nodes.contains(id)) return;
        const bool entering = focusRoot.isEmpty();
        if (entering) {
            fullMapFit = fitMode;
            fullMapTransform = canvas->transform();
            fullMapCenter = canvas->mapToScene(canvas->viewport()->rect().center());
        }
        focusRoot = id;
        selected = id;
        additionalSelection.clear();
        render();
        fit();
        canvas->setFocus();
        statusBar()->clearMessage();
    }

    void leaveFocus() {
        if (focusRoot.isEmpty()) return;
        statusBar()->clearMessage();
        QString selection = selected;
        for (QString id = parents.value(selection); !id.isEmpty(); id = parents.value(id))
            if (doc.nodes.value(id).collapsed) selection = id;
        focusRoot.clear();
        selected = selection;
        render();
        restoreFullMapView();
    }

    void restoreFullMapView() {
        focusSplitter->refresh();
        canvas->parentWidget()->layout()->activate();
        if (fullMapFit) {
            fit();
        } else {
            fitMode = false;
            canvas->setTransform(fullMapTransform);
            canvas->centerOn(fullMapCenter);
            updateZoomLevel();
            // Scrollbar visibility can resize the viewport after the splitter layout.
            const QString root = doc.root;
            const QTransform transform = fullMapTransform;
            const QPointF center = fullMapCenter;
            QTimer::singleShot(0, this, [this, root, transform, center] {
                if (focusRoot.isEmpty() && doc.root == root && !fitMode && canvas->transform() == transform)
                    canvas->centerOn(center);
            });
        }
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

    void createNode(bool child, bool before = false, bool editInCanvas = true) {
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
        if (!child && doc.floating.contains(selected))
            doc.floating.insert(node.id, doc.floating.value(selected) + QPointF(0, before ? -100 : 100));
        selected = node.id;
        changed();
        canvas->ensureVisible(items.value(selected).body, 30, 30);
        if (editInCanvas) startEdit();
    }

    void insertParent() {
        if (selected == viewRoot()) return;
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
        if (next.floating.contains(selected)) next.floating.insert(node.id, next.floating.take(selected));
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
        if (!selectedDecoration.isEmpty()) { deleteDecoration(); return; }
        QStringList targets = additionalSelection.values();
        targets.append(selected);
        targets.removeAll(doc.root);
        targets.removeAll(viewRoot());
        if (targets.isEmpty()) return;
        // Remove deepest selections first so nested selections promote every
        // surviving descendant exactly once, in its original order.
        std::stable_sort(targets.begin(), targets.end(), [this](const QString &a, const QString &b) {
            return depth(a) > depth(b);
        });
        checkpoint();
        preserveRootLanes(doc);
        for (const auto &id : targets) {
            const QString parent = parents.value(id);
            auto &siblings = doc.nodes[parent].children;
            const int index = siblings.indexOf(id);
            siblings.removeAt(index);
            const Node removed = doc.nodes.take(id);
            for (int i = 0; i < removed.children.size(); ++i) {
                const QString child = removed.children.at(i);
                siblings.insert(index + i, child);
                if (parent == doc.root) doc.nodes[child].rootLane = removed.rootLane;
                if (doc.floating.contains(id)) doc.floating.insert(child, doc.floating.value(id) + QPointF(0, i * 100));
            }
            if (selected == id) selected = removed.children.isEmpty() ? parent : removed.children.first();
        }
        additionalSelection.clear();
        changed();
    }

    bool insertOutline(const QString &text, bool asSibling, QString *error) {
        const QString parent = asSibling && selected != viewRoot() ? parents.value(selected) : selected;
        Document next = doc;
        QStringList inserted;
        if (!BranchClipboard::pasteOutline(&next, parent, text, &inserted, error)) return false;
        if (parent != selected) {
            auto &siblings = next.nodes[parent].children;
            for (int i = 0; i < inserted.size(); ++i) siblings.removeLast();
            const int index = siblings.indexOf(selected) + 1;
            for (int i = 0; i < inserted.size(); ++i) siblings.insert(index + i, inserted.at(i));
        }
        if (parent == doc.root && doc.mindMap && doc.branchDirection == BranchDirection::Both) {
            preserveRootLanes(next);
            if (parent != selected)
                for (const auto &id : inserted) next.nodes[id].rootLane = visibleRootLane(selected);
        }
        checkpoint();
        doc = std::move(next);
        selected = inserted.first();
        changed();
        replaceSelection(inserted);
        canvas->ensureVisible(items.value(selected).body, 30, 30);
        return true;
    }

    void addMultipleTopics() {
        QDialog dialog(this);
        dialog.setObjectName("multipleTopicsDialog");
        dialog.setWindowTitle(QStringLiteral("添加多个主题"));
        auto *layout = new QVBoxLayout(&dialog);
        auto *instructions = new QLabel(QStringLiteral("每行一个主题，Tab 缩进一层；同层主题保持相同缩进。"), &dialog);
        instructions->setWordWrap(true);
        layout->addWidget(instructions);
        auto *outline = new QPlainTextEdit(&dialog);
        outline->setObjectName("multipleTopicsText");
        outline->setAccessibleName(QStringLiteral("主题大纲"));
        outline->setTabChangesFocus(false);
        outline->setPlaceholderText(QStringLiteral("项目规划\n\t调研需求\n\t确定目标\n执行计划"));
        layout->addWidget(outline);
        auto *position = new QComboBox(&dialog);
        position->setObjectName("multipleTopicsPosition");
        position->addItem(QStringLiteral("添加到所选主题下"), false);
        if (selected != viewRoot()) position->addItem(QStringLiteral("添加到所选主题之后（同级）"), true);
        layout->addWidget(position);
        auto *message = new QLabel(&dialog);
        message->setWordWrap(true);
        layout->addWidget(message);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("添加主题"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            QString error;
            if (insertOutline(outline->toPlainText(), position->currentData().toBool(), &error)) dialog.accept();
            else message->setText(error);
        });
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.resize(QSize(580, 430).boundedTo(size()));
        outline->setFocus();
        dialog.exec();
    }

    void removeSelected() {
        if (!selectedDecoration.isEmpty()) { deleteDecoration(); return; }
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
        if (!selectedDecoration.isEmpty()) { copyDecoration(cut); return; }
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
        QStringList outlines;
        for (const auto &id : roots) outlines.append(BranchClipboard::outline(doc, id));
        const QString outline = outlines.join('\n');
        if (!writeClipboard([&] {
            auto *mime = new QMimeData;
            mime->setData(BranchClipboard::mimeType(), data);
            mime->setText(outline);
            return mime;
        })) {
            statusBar()->showMessage(cut ? QStringLiteral("剪贴板正忙，分支已保留。请重试剪切")
                                        : QStringLiteral("剪贴板正忙，请重试复制"), 5000);
            return;
        }
        if (cut) removeSelected();
        statusBar()->showMessage(cut ? QStringLiteral("已剪切分支，可粘贴或撤销")
                                    : QStringLiteral("已复制分支，可在其他主题下粘贴"), 4000);
    }

    void pasteBranch(bool asSibling = false) {
        const QMimeData *mime = QApplication::clipboard()->mimeData();
        if (mime && mime->hasFormat("application/x-zhili-decoration+json")) { pasteDecoration(mime->data("application/x-zhili-decoration+json")); return; }
        if (!mime || (!mime->hasFormat(BranchClipboard::mimeType()) && !mime->hasText())) {
            statusBar()->showMessage(QStringLiteral("剪贴板中没有分支或文本大纲"), 4000);
            return;
        }
        Document next = doc;
        QStringList inserted;
        QString error;
        const QString parent = asSibling && selected != viewRoot() ? parents.value(selected) : selected;
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

    void pasteTopicStyle() {
        if (!hasCopiedStyle) return;
        QSet<QString> targets = additionalSelection;
        targets.insert(selected);
        TopicStyleChange change;
        change.style = copiedStyle;
        change.alignment = change.family = change.points = change.weight = change.color = true;
        change.italic = change.underline = change.strikeOut = true;
        applyTopicStyles(targets, change);
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
            if (change.italic) style.italic = change.style.italic;
            if (change.underline) style.underline = change.style.underline;
            if (change.strikeOut) style.strikeOut = change.style.strikeOut;
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
        QFont font = fontFor(choice);
        font.setItalic(node.style.italic);
        font.setUnderline(node.style.underline);
        font.setStrikeOut(node.style.strikeOut);
        return font;
    }

    void quickTextStyle(int property) {
        QSet<QString> targets = additionalSelection;
        targets.insert(selected);
        Document next = doc;
        bool allSet = true;
        for (const auto &id : targets) {
            const QFont font = fontForTopic(doc.nodes.value(id), id == viewRoot(), parents.value(id) == viewRoot());
            allSet = allSet && (property == 0 ? font.bold() : property == 1 ? font.italic() :
                               property == 2 ? font.underline() : font.strikeOut());
        }
        for (const auto &id : targets) {
            auto &style = next.nodes[id].style;
            if (property == 0) style.weight = allSet ? 0 : 1;
            if (property == 1) style.italic = !allSet;
            if (property == 2) style.underline = !allSet;
            if (property == 3) style.strikeOut = !allSet;
            if (property == 4 || property == 5) {
                const QFont font = fontForTopic(doc.nodes.value(id), id == viewRoot(), parents.value(id) == viewRoot());
                style.points = qBound(9, font.pointSize() + (property == 4 ? 1 : -1), 48);
            }
        }
        if (next.serialize() == doc.serialize()) return;
        if (next.serialize().size() > Document::maxBytes) {
            statusBar()->showMessage(QStringLiteral("应用样式后将超过文件大小上限"), 5000);
            return;
        }
        checkpoint();
        doc = std::move(next);
        changed();
    }

    void chooseTextColor() {
        const QColor initial(doc.nodes.value(selected).style.color);
        const QColor color = QColorDialog::getColor(initial.isValid() ? initial : ink, this, QStringLiteral("文字颜色"));
        if (!color.isValid()) return;
        TopicStyleChange change;
        change.color = true;
        change.style.color = color.name();
        QSet<QString> targets = additionalSelection;
        targets.insert(selected);
        applyTopicStyles(targets, change);
    }

    BranchDirection visibleRootLane(const QString &id) const {
        return items.value(id).body->pos().x() < 0 ? BranchDirection::Left : BranchDirection::Right;
    }

    void preserveRootLanes(Document &next) const {
        if (!focusRoot.isEmpty() || !doc.mindMap || doc.branchDirection != BranchDirection::Both) return;
        for (const auto &id : doc.nodes.value(doc.root).children)
            if (items.contains(id)) next.nodes[id].rootLane = visibleRootLane(id);
    }

    void moveNode(const QString &source, const QString &target, int side, bool alignRootLane = false) {
        moveBranches({source}, target, side, alignRootLane);
    }

    void moveBranches(const QStringList &roots, const QString &target, int side, bool alignRootLane) {
        if (roots.isEmpty() || roots.contains(viewRoot()) || roots.contains(target) || !doc.nodes.contains(target)) return;
        const bool sibling = target != viewRoot() && side != 0;
        const QString newParent = sibling ? parents.value(target) : target;
        QSet<QString> moving;
        for (const auto &id : roots) moving.insert(id);
        for (QString id = newParent; !id.isEmpty(); id = parents.value(id)) {
            if (moving.contains(id)) {
                statusBar()->showMessage(QStringLiteral("无法将主题移入自己的分支"), 4000);
                return;
            }
        }
        QVector<QPair<QString, int>> pending;
        const int startDepth = depth(newParent) + 1;
        QSet<QString> oldParents;
        for (const auto &id : roots) {
            const QString oldParent = parents.value(id);
            if (oldParent.isEmpty()) return;
            oldParents.insert(oldParent);
            pending.append(qMakePair(id, startDepth));
        }
        while (!pending.isEmpty()) {
            const auto entry = pending.takeLast();
            if (entry.second > Document::maxDepth) {
                statusBar()->showMessage(QStringLiteral("移动后将超过层级上限"), 4000);
                return;
            }
            for (const auto &child : doc.nodes.value(entry.first).children)
                pending.append(qMakePair(child, entry.second + 1));
        }
        QStringList newOrder;
        for (const auto &id : qAsConst(doc.nodes)[newParent].children)
            if (!moving.contains(id)) newOrder.append(id);
        const int index = sibling ? newOrder.indexOf(target) + (side > 0 ? 1 : 0) : newOrder.size();
        for (int i = 0; i < roots.size(); ++i) newOrder.insert(index + i, roots.at(i));
        const bool preserveLanes = alignRootLane && focusRoot.isEmpty() && doc.mindMap &&
            doc.branchDirection == BranchDirection::Both && (newParent == doc.root || oldParents.contains(doc.root));
        const bool pinToTarget = preserveLanes && sibling && newParent == doc.root;
        const bool changeLane = pinToTarget && std::any_of(roots.cbegin(), roots.cend(), [&](const QString &id) {
            return items.contains(id) && visibleRootLane(id) != visibleRootLane(target);
        });
        const bool attachingFloating = std::any_of(roots.cbegin(), roots.cend(), [this](const QString &id) { return doc.floating.contains(id); });
        if (oldParents.size() == 1 && oldParents.contains(newParent) &&
            newOrder == qAsConst(doc.nodes)[newParent].children && !changeLane && !attachingFloating) return;
        checkpoint();
        if (preserveLanes) preserveRootLanes(doc);
        for (const auto &id : roots) {
            doc.floating.remove(id);
            if (pinToTarget) doc.nodes[id].rootLane = visibleRootLane(target);
            else if (newParent != doc.root) doc.nodes[id].rootLane = BranchDirection::Both;
        }
        for (const auto &parent : oldParents) {
            if (parent == newParent) continue;
            auto &children = doc.nodes[parent].children;
            children.erase(std::remove_if(children.begin(), children.end(), [&](const QString &id) {
                return moving.contains(id);
            }), children.end());
        }
        doc.nodes[newParent].children = newOrder;
        doc.nodes[newParent].collapsed = false;
        changed();
        replaceSelection(roots);
        if (items.contains(selected)) canvas->ensureVisible(items.value(selected).body, 30, 30);
    }

    void copyDraggedBranches(const QStringList &roots, const QString &target, int side) {
        if (roots.isEmpty() || (side == 0 && roots.contains(target)) || roots.contains(viewRoot())) return;
        const QString parent = side == 0 ? target : parents.value(target);
        QString error;
        const auto data = BranchClipboard::encode(doc, roots, &error);
        Document next = doc;
        QStringList inserted;
        if (data.isEmpty() || !BranchClipboard::paste(&next, parent, data, &inserted, &error)) {
            statusBar()->showMessage(error, 5000);
            return;
        }
        if (side != 0) {
            auto &siblings = next.nodes[parent].children;
            siblings.erase(siblings.end() - inserted.size(), siblings.end());
            const int index = siblings.indexOf(target) + (side > 0 ? 1 : 0);
            for (int i = 0; i < inserted.size(); ++i) siblings.insert(index + i, inserted.at(i));
        }
        if (focusRoot.isEmpty() && doc.mindMap && doc.branchDirection == BranchDirection::Both && parent == doc.root) {
            preserveRootLanes(next);
            if (side != 0)
                for (const auto &id : inserted) next.nodes[id].rootLane = visibleRootLane(target);
        }
        if (parent == doc.root && side != 0)
            for (int i = 0; i < roots.size(); ++i) if (doc.floating.contains(roots[i]))
                next.floating.insert(inserted[i], doc.floating.value(roots[i]) + QPointF(30, side * 100));
        checkpoint();
        doc = std::move(next);
        changed();
        replaceSelection(inserted);
        if (items.contains(selected)) canvas->ensureVisible(items.value(selected).body, 30, 30);
    }

    void duplicateAsChild() {
        const QString source = selected;
        QString error;
        const QByteArray bytes = BranchClipboard::encode(doc, source, &error);
        Document next = doc;
        QStringList inserted;
        if (bytes.isEmpty() || !BranchClipboard::paste(&next, source, bytes, &inserted, &error)) {
            statusBar()->showMessage(error, 5000);
            return;
        }
        checkpoint();
        doc = std::move(next);
        selected = inserted.first();
        additionalSelection.clear();
        changed();
        canvas->ensureVisible(items.value(selected).body, 30, 30);
    }

    void reorderSelected(bool earlier, bool boundary = false) {
        const QStringList roots = selectedRoots(false, viewRoot());
        QSet<QString> moving;
        for (const auto &id : roots) moving.insert(id);
        QSet<QString> groups;
        for (const auto &id : roots) groups.insert(parents.value(id));
        Document next = doc;
        bool updated = false;
        for (const auto &parent : groups) {
            QStringList order = doc.nodes.value(parent).children;
            if (boundary) {
                QStringList moved, rest;
                for (const auto &id : order) (moving.contains(id) ? moved : rest).append(id);
                order = earlier ? moved + rest : rest + moved;
            } else if (earlier) {
                for (int i = 1; i < order.size(); ++i)
                    if (moving.contains(order.at(i)) && !moving.contains(order.at(i - 1))) order.swap(i, i - 1);
            } else {
                for (int i = order.size() - 2; i >= 0; --i)
                    if (moving.contains(order.at(i)) && !moving.contains(order.at(i + 1))) order.swap(i, i + 1);
            }
            if (order != doc.nodes.value(parent).children) {
                next.nodes[parent].children = order;
                updated = true;
            }
        }
        if (!updated) return;
        checkpoint();
        doc = std::move(next);
        changed();
        canvas->ensureVisible(items.value(selected).body, 25, 25);
    }

    void navigateBoundary(bool last, bool extend = false) {
        const QString parent = selected == viewRoot() ? QString() : parents.value(selected);
        const QStringList siblings = parent.isEmpty() ? QStringList{viewRoot()} : doc.nodes.value(parent).children;
        if (siblings.isEmpty()) return;
        const QString target = last ? siblings.last() : siblings.first();
        if (extend) {
            QStringList ids{target};
            const int from = siblings.indexOf(selected), to = siblings.indexOf(target);
            for (int i = qMin(from, to); i <= qMax(from, to); ++i)
                if (items.contains(siblings.at(i)) && siblings.at(i) != target) ids.append(siblings.at(i));
            replaceSelection(ids);
        } else selectNode(target);
        canvas->ensureVisible(items.value(selected).body, 25, 25);
    }

    enum class SelectionKind { Siblings, Level, Children, Ancestors, Branch };

    void selectRelated(SelectionKind kind) {
        QStringList ids;
        if (kind == SelectionKind::Children) {
            ids = doc.nodes.value(selected).children;
            if (!ids.isEmpty() && doc.nodes.value(selected).collapsed) toggleCollapse();
        } else if (kind == SelectionKind::Siblings) {
            ids = selected == viewRoot() ? QStringList{selected} : doc.nodes.value(parents.value(selected)).children;
        } else if (kind == SelectionKind::Ancestors) {
            for (QString id = selected; id != viewRoot() && parents.contains(id);) {
                id = parents.value(id);
                ids.append(id);
            }
        } else if (kind == SelectionKind::Branch) {
            // Make the full selection visible so the next batch edit is reviewable.
            setBranchDetail(-1);
            ids = selectedRoots(true);
            for (int i = 0; i < ids.size(); ++i) ids.append(doc.nodes.value(ids.at(i)).children);
        } else {
            const int level = depth(selected);
            for (auto it = items.cbegin(); it != items.cend(); ++it)
                if (depth(it.key()) == level) ids.append(it.key());
        }
        if (ids.isEmpty()) return;
        if (ids.removeOne(selected)) ids.prepend(selected);
        replaceSelection(ids);
    }

    void changeBranchLevel(bool promote) {
        const QStringList roots = selectedRoots(false, viewRoot());
        if (roots.isEmpty()) return;
        const QString parent = parents.value(roots.first());
        for (const auto &id : roots) {
            if (parents.value(id) != parent) {
                statusBar()->showMessage(QStringLiteral("请先选择同一父主题下的分支再调整层级"), 4000);
                return;
            }
        }
        if (promote) {
            if (parent != viewRoot()) moveBranches(roots, parent, 1, true);
        } else {
            const auto siblings = doc.nodes.value(parent).children;
            const int index = siblings.indexOf(roots.first());
            if (index > 0) moveBranches(roots, siblings.at(index - 1), 0, true);
        }
    }

    void mergeSelectedTopics() {
        const QStringList roots = selectedRoots(false, viewRoot());
        if (roots.size() < 2) {
            statusBar()->showMessage(QStringLiteral("请选择至少两个同级主题后合并"), 4000);
            return;
        }
        const QString parent = parents.value(roots.first());
        for (const auto &id : roots) {
            if (parents.value(id) != parent) {
                statusBar()->showMessage(QStringLiteral("仅可合并同一父主题下的主题"), 4000);
                return;
            }
        }
        Document next = doc;
        const QString target = roots.first();
        QStringList text, notes;
        for (const auto &id : roots) {
            const Node node = doc.nodes.value(id);
            text.append(node.text);
            if (!node.notes.isEmpty()) notes.append(node.notes);
            if (id == target) continue;
            next.nodes[target].children.append(node.children);
            next.nodes[parent].children.removeOne(id);
            next.nodes.remove(id);
        }
        next.nodes[target].text = text.join('\n');
        next.nodes[target].notes = notes.join(QStringLiteral("\n\n"));
        next.nodes[target].collapsed = false;
        Document checked;
        QString error;
        if (!checked.parse(next.serialize(), &error)) { statusBar()->showMessage(error, 5000); return; }
        checkpoint();
        doc = std::move(checked);
        selected = target;
        additionalSelection.clear();
        changed();
    }

    void syncOutline() {
        if (!outlineDock || !outlineDock->isVisible()) return;
        ++outlineGeneration;
        const QSignalBlocker block(outlineTree);
        const int scroll = outlineTree->verticalScrollBar()->value();
        outlineTree->clear();
        outlineItems.clear();
        QStringList pending{viewRoot()};
        while (!pending.isEmpty()) {
            const QString id = pending.takeLast();
            const auto &node = qAsConst(doc.nodes)[id];
            auto *item = new QTreeWidgetItem;
            item->setText(0, node.text);
            item->setData(0, Qt::UserRole, id);
            item->setToolTip(0, node.notes.isEmpty() ? node.text : node.text + QStringLiteral("\n\n") + node.notes);
            item->setFlags(item->flags() | Qt::ItemIsEditable);
            if (id == viewRoot()) outlineTree->addTopLevelItem(item);
            else outlineItems.value(parents.value(id))->addChild(item);
            outlineItems.insert(id, item);
            for (auto child = node.children.crbegin(); child != node.children.crend(); ++child) pending.append(*child);
        }
        for (auto it = outlineItems.cbegin(); it != outlineItems.cend(); ++it) {
            it.value()->setExpanded(!doc.nodes.value(it.key()).collapsed);
            it.value()->setSelected(it.key() == selected || additionalSelection.contains(it.key()));
        }
        outlineTree->setCurrentItem(outlineItems.value(selected), 0, QItemSelectionModel::NoUpdate);
        outlineTree->verticalScrollBar()->setValue(scroll);
    }

    void syncOutlineSelection() {
        if (!outlineDock || !outlineDock->isVisible()) return;
        const QSignalBlocker block(outlineTree);
        for (auto it = outlineItems.cbegin(); it != outlineItems.cend(); ++it)
            it.value()->setSelected(it.key() == selected || additionalSelection.contains(it.key()));
        outlineTree->setCurrentItem(outlineItems.value(selected), 0, QItemSelectionModel::NoUpdate);
    }

    void revealTopic(const QString &id) {
        QStringList collapsed;
        for (QString ancestor = id; ancestor != viewRoot() && parents.contains(ancestor);) {
            ancestor = parents.value(ancestor);
            if (doc.nodes.value(ancestor).collapsed) collapsed.append(ancestor);
        }
        if (!collapsed.isEmpty()) {
            checkpoint();
            for (const auto &ancestor : collapsed) doc.nodes[ancestor].collapsed = false;
            selected = id;
            changed();
        } else selectNode(id);
        if (items.contains(id)) canvas->ensureVisible(items.value(id).body, 25, 25);
    }

    void toggleOutline() {
        if (!outlineDock) {
            outlineDock = new QDockWidget(QStringLiteral("大纲"), this);
            outlineDock->setObjectName("outlineDock");
            outlineDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
            outlineTree = new OutlineTree;
            outlineTree->setObjectName("outlineTree");
            outlineTree->setHeaderHidden(true);
            outlineTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
            outlineTree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
            outlineTree->setMinimumWidth(230);
            outlineTree->editingChanged = [this] { updateActionStates(); };
            outlineTree->setAlternatingRowColors(true);
            outlineDock->setWidget(outlineTree);
            addDockWidget(Qt::LeftDockWidgetArea, outlineDock);
            connect(outlineTree, &QTreeWidget::itemSelectionChanged, this, [this] {
                QStringList ids;
                if (auto *current = outlineTree->currentItem())
                    if (current->isSelected()) ids.append(current->data(0, Qt::UserRole).toString());
                for (const auto *item : outlineTree->selectedItems()) {
                    const QString id = item->data(0, Qt::UserRole).toString();
                    if (!ids.contains(id)) ids.append(id);
                }
                const int generation = outlineGeneration;
                QTimer::singleShot(0, this, [this, ids, generation] {
                    if (ids.isEmpty() || generation != outlineGeneration || !finishEdit()) return;
                    revealTopic(ids.first());
                    replaceSelection(ids);
                });
            });
            connect(outlineTree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int) {
                const QString id = item->data(0, Qt::UserRole).toString();
                const QString value = item->text(0);
                QTimer::singleShot(0, this, [this, id, value] {
                    if (!doc.nodes.contains(id) || doc.nodes.value(id).text == value) return;
                    QString error = topicTextError(value);
                    Document next = doc;
                    next.nodes[id].text = value;
                    if (error.isEmpty() && next.serialize().size() > Document::maxBytes)
                        error = QStringLiteral("文字修改后将超过文件大小上限");
                    if (!error.isEmpty()) { syncOutline(); statusBar()->showMessage(error, 6000); return; }
                    checkpoint();
                    doc = std::move(next);
                    changed();
                });
            });
            const auto fold = [this](QTreeWidgetItem *item, bool collapsed) {
                const QString id = item->data(0, Qt::UserRole).toString();
                if (doc.nodes.value(id).collapsed == collapsed) return;
                QTimer::singleShot(0, this, [this, id, collapsed] {
                    if (!doc.nodes.contains(id) || doc.nodes.value(id).collapsed == collapsed) return;
                    checkpoint(); doc.nodes[id].collapsed = collapsed; changed();
                });
            };
            connect(outlineTree, &QTreeWidget::itemCollapsed, this, [fold](QTreeWidgetItem *item) { fold(item, true); });
            connect(outlineTree, &QTreeWidget::itemExpanded, this, [fold](QTreeWidgetItem *item) { fold(item, false); });
            outlineTree->command = [this](int key, Qt::KeyboardModifiers modifiers) {
                if (modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) return false;
                if (auto *current = outlineTree->currentItem()) {
                    const QString id = current->data(0, Qt::UserRole).toString();
                    QStringList selection{id};
                    for (const auto *item : outlineTree->selectedItems()) {
                        const QString other = item->data(0, Qt::UserRole).toString();
                        if (other != id && items.contains(other)) selection.append(other);
                    }
                    if (items.contains(id)) replaceSelection(selection);
                }
                if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Insert) {
                    createNode(key == Qt::Key_Insert || selected == viewRoot(), modifiers & Qt::ShiftModifier, false);
                    QTimer::singleShot(0, this, [this] {
                        outlineTree->setFocus();
                        if (auto *item = outlineItems.value(selected)) outlineTree->editItem(item, 0);
                    });
                } else if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
                    changeBranchLevel(key == Qt::Key_Backtab || (modifiers & Qt::ShiftModifier));
                } else if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
                    if (modifiers & Qt::ShiftModifier) removeTopicKeepChildren(); else removeSelected();
                } else return false;
                return true;
            };
            connect(outlineDock, &QDockWidget::visibilityChanged, this, [this](bool shown) { if (shown) syncOutline(); });
            outlineDock->show();
        } else outlineDock->setVisible(!outlineDock->isVisible());
        syncOutline();
        if (outlineDock->isVisible()) outlineTree->setFocus();
        else canvas->setFocus();
    }

    void navigate(Qt::Key key, bool extend = false) {
        const QString parent = selected == viewRoot() ? QString() : parents.value(selected);
        const bool stacked = leafRails.contains(selected);
        const bool onLeft = doc.mindMap && items.value(selected).body->pos().x() < 0;
        const bool towardParent = key == (stacked ? Qt::Key_Left :
            doc.mindMap ? (onLeft ? Qt::Key_Right : Qt::Key_Left) : Qt::Key_Up);
        const bool towardChildren = key == (doc.mindMap ? (onLeft ? Qt::Key_Left : Qt::Key_Right) : Qt::Key_Down);
        QString target;
        if (doc.mindMap && selected == viewRoot() && (key == Qt::Key_Left || key == Qt::Key_Right)) {
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
            const bool sameLane = doc.mindMap && doc.branchDirection == BranchDirection::Both && parent == viewRoot();
            for (int index = siblings.indexOf(selected) + direction; index >= 0 && index < siblings.size(); index += direction) {
                const auto &candidate = siblings.at(index);
                if (items.contains(candidate) && (!sameLane || visibleRootLane(candidate) == visibleRootLane(selected))) {
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

    void setBranchDetail(int levels, bool wholeView = false) {
        QVector<QPair<QString, int>> pending;
        const QStringList roots = wholeView ? QStringList{viewRoot()} : selectedRoots(true);
        for (const auto &id : roots) pending.append(qMakePair(id, 0));
        QVector<QPair<QString, bool>> updates;
        while (!pending.isEmpty()) {
            const auto entry = pending.takeLast();
            const auto &node = qAsConst(doc.nodes)[entry.first];
            if (node.children.isEmpty()) continue;
            const bool collapsed = levels >= 0 && entry.second >= levels;
            if (node.collapsed != collapsed) updates.append(qMakePair(entry.first, collapsed));
            for (const auto &child : node.children) pending.append(qMakePair(child, entry.second + 1));
        }
        if (updates.isEmpty()) return;
        checkpoint();
        for (const auto &entry : updates) doc.nodes[entry.first].collapsed = entry.second;
        changed();
    }

    void revealNextLevel() {
        QStringList pending = selectedRoots(true), expand;
        while (!pending.isEmpty()) {
            const QString id = pending.takeLast();
            const Node &node = qAsConst(doc.nodes)[id];
            if (node.children.isEmpty()) continue;
            if (node.collapsed) expand.append(id);
            else pending.append(node.children);
        }
        if (expand.isEmpty()) return;
        checkpoint();
        for (const auto &id : expand) doc.nodes[id].collapsed = false;
        changed();
    }

    void sortChildren(bool ascending) {
        QStringList children = doc.nodes.value(selected).children;
        QCollator collator;
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        collator.setNumericMode(true);
        const auto &nodes = qAsConst(doc.nodes);
        std::stable_sort(children.begin(), children.end(), [&](const QString &a, const QString &b) {
            const int comparison = collator.compare(nodes[a].text, nodes[b].text);
            return ascending ? comparison < 0 : comparison > 0;
        });
        if (children == nodes[selected].children) return;
        Document next = doc;
        if (focusRoot.isEmpty() && selected == doc.root && doc.mindMap && doc.branchDirection == BranchDirection::Both)
            preserveRootLanes(next);
        next.nodes[selected].children = children;
        checkpoint();
        doc = std::move(next);
        changed();
    }

    void editNotes() {
        const QString id = selected;
        const auto &node = qAsConst(doc.nodes)[id];
        QDialog dialog(this);
        dialog.setObjectName("topicNotesDialog");
        dialog.setWindowTitle(QStringLiteral("主题备注"));
        auto *layout = new QVBoxLayout(&dialog);
        auto *title = new QLabel(node.text.section(QLatin1Char(10), 0, 0).left(100), &dialog);
        title->setTextFormat(Qt::PlainText);
        title->setWordWrap(true);
        layout->addWidget(title);
        auto *notes = new QPlainTextEdit(node.notes, &dialog);
        notes->setObjectName("topicNotesText");
        notes->setAccessibleName(QStringLiteral("备注正文"));
        layout->addWidget(notes);
        auto *message = new QLabel(&dialog);
        message->setWordWrap(true);
        message->hide();
        layout->addWidget(message);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
        buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存备注"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            const QString value = notes->toPlainText();
            if (value == node.notes) { dialog.accept(); return; }
            QString error = topicTextError(value);
            if (error.isEmpty()) {
                Document next = doc;
                next.nodes[id].notes = value;
                if (next.serialize().size() <= Document::maxBytes) {
                    checkpoint();
                    doc = std::move(next);
                    changed();
                    dialog.accept();
                    return;
                }
                error = QStringLiteral("保存备注后导图将超过 50 MiB");
            }
            message->setText(error);
            message->show();
        });
        dialog.resize(QSize(620, 420).boundedTo(size()));
        notes->setFocus();
        dialog.exec();
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
        if (target == viewRoot()) return 0;
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
        if (draggedRoots.isEmpty() || editor) return {};
        const auto candidate = [&](const QString &id) {
            DropTarget target;
            if (id.isEmpty() || draggedRootSet.contains(id)) return target;
            const int side = dropSide(id, at);
            const QString parent = side == 0 ? id : parents.value(id);
            for (QString ancestor = parent; !copy && !ancestor.isEmpty(); ancestor = parents.value(ancestor))
                if (draggedRootSet.contains(ancestor)) return target;
            if (draggedNodes == 0) {
                QVector<QPair<QString, int>> pending;
                for (const auto &root : draggedRoots) pending.append(qMakePair(root, 0));
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
        paintNodeOutline(items.value(id), id == viewRoot(), active);
    }

    void paintNodeOutline(const NodeItem &item, bool root, bool active) const {
        const QColor border = appearance.highContrast ? line : tint(item.branchColor, 58);
        const QColor selection = appearance.highContrast ? accent : root ? QColor("#8acbb0") : moss;
        item.body->setPen(QPen(active ? selection : border, active ? 2.6 : 1.1));
    }

    void selectNode(const QString &id) {
        if (!selectedDecoration.isEmpty()) { selectedDecoration.clear(); renderDecorations(scene, items); }
        if (!items.contains(id) || (selected == id && additionalSelection.isEmpty())) return;
        replaceSelection(QStringList{id});
    }

    void replaceSelection(const QStringList &ids) {
        if (ids.isEmpty()) return;
        if (!selectedDecoration.isEmpty()) { selectedDecoration.clear(); renderDecorations(scene, items); }
        const QString old = selected;
        const auto oldExtras = additionalSelection;
        selected = ids.first();
        additionalSelection.clear();
        for (int i = 1; i < ids.size(); ++i) additionalSelection.insert(ids.at(i));
        styleNode(old);
        styleNode(selected);
        for (const auto &id : oldExtras) styleNode(id);
        for (const auto &id : additionalSelection) styleNode(id);
        statusBar()->clearMessage();
        updateActionStates();
        syncOutlineSelection();
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
        statusBar()->clearMessage();
        updateActionStates();
    }

    QStringList selectedRoots(bool includeRoot, const QString &skip = {}) const {
        if (additionalSelection.isEmpty())
            return (selected == doc.root && !includeRoot) || selected == skip ? QStringList() : QStringList{selected};
        QStringList roots;
        QStringList pending{doc.root};
        while (!pending.isEmpty()) {
            const QString id = pending.takeLast();
            if (id != skip && (id != doc.root || includeRoot) && (id == selected || additionalSelection.contains(id))) {
                roots.append(id);
                continue;
            }
            const auto &children = doc.nodes[id].children;
            for (auto it = children.crbegin(); it != children.crend(); ++it) pending.append(*it);
        }
        return roots;
    }

    void updateActionStates() {
        const bool editingActive = editor || (outlineTree && outlineTree->editingText());
        for (auto *action : canvasActions) action->setEnabled(!editingActive);
        const bool branch = !editingActive && selected != doc.root && doc.nodes.contains(selected);
        for (auto *action : {removeAction, cutAction})
            if (action) action->setEnabled(!editingActive && (branch || !additionalSelection.isEmpty()));
        if (collapseAction) collapseAction->setEnabled(!editingActive && !doc.nodes.value(selected).children.isEmpty());
        if (duplicateBranchAction)
            duplicateBranchAction->setEnabled(!editingActive && selected != viewRoot() && additionalSelection.isEmpty());
        for (auto *item : sortActions)
            item->setEnabled(!editingActive && doc.nodes.value(selected).children.size() > 1);
        if (undoAction) undoAction->setEnabled(!editingActive && !undoStack.isEmpty());
        if (redoAction) redoAction->setEnabled(!editingActive && !redoStack.isEmpty());
        if (focusAction) focusAction->setEnabled(!editingActive && selected != viewRoot());
        if (leaveFocusAction) leaveFocusAction->setEnabled(!editingActive && (!focusRoot.isEmpty() ||
            placingFloating || !relationshipSource.isEmpty() || !selectedDecoration.isEmpty()));
        if (!selectedDecoration.isEmpty()) {
            if (removeAction) removeAction->setEnabled(!editingActive);
            if (cutAction) cutAction->setEnabled(!editingActive);
        }
        if (leaveFocusButton) leaveFocusButton->setVisible(!focusRoot.isEmpty());
        if (pasteStyleAction) pasteStyleAction->setEnabled(!editingActive && hasCopiedStyle);
        const QStringList siblings = doc.nodes.value(parents.value(selected)).children;
        const int index = siblings.indexOf(selected);
        if (moveUpAction) moveUpAction->setEnabled(!editingActive && ((!additionalSelection.isEmpty()) || (branch && selected != viewRoot() && index > 0)));
        if (moveDownAction) moveDownAction->setEnabled(!editingActive && ((!additionalSelection.isEmpty()) || (branch && selected != viewRoot() && index >= 0 && index + 1 < siblings.size())));
        if (additionalSelection.isEmpty()) {
            workspaceStats->setText(items.size() == doc.nodes.size()
                ? QStringLiteral("%1 个主题").arg(items.size())
                : QStringLiteral("显示 %1 / %2 个主题").arg(items.size()).arg(doc.nodes.size()));
        } else {
            workspaceStats->setText(QStringLiteral("已选 %1 个主题").arg(additionalSelection.size() + 1));
        }
        updateShortcutHint();
        if (!editingActive) syncOutlineSelection();
    }

    void startEdit(int cursorPosition = -1) {
        if (!selectedDecoration.isEmpty()) { editDecoration(); return; }
        if (editor || !items.contains(selected)) return;
        selectNode(selected);
        // A large map can be fitted far below readable text size. Editing should
        // stay usable, without creating an editor as tall as a long topic.
        if (canvas->transform().m11() < 0.65)
            canvas->zoomBy(1 / canvas->transform().m11());
        editId = selected;
        auto *topicEditor = new TopicEditor;
        topicEditor->submit = [this] { finishEdit(); };
        topicEditor->split = [this] { splitEditedTopic(); };
        editor = topicEditor;
        editor->setObjectName("topicEditor");
        const auto &node = qAsConst(doc.nodes)[selected];
        editor->setFont(fontForTopic(node, selected == viewRoot(), parents.value(selected) == viewRoot()));
        alignTopicText(editor->document(), node.style.alignment);
        editor->setPlainText(node.text);
        editor->setFrameShape(QFrame::NoFrame);
        editor->setStyleSheet(QStringLiteral("QPlainTextEdit#topicEditor { background:transparent; border:0; border-radius:0; padding:0; color:%1; }")
            .arg(items.value(selected).label->defaultTextColor().name()));
        editor->setAttribute(Qt::WA_TranslucentBackground);
        editor->setAutoFillBackground(false);
        editor->viewport()->setAutoFillBackground(false);
        editor->viewport()->setAttribute(Qt::WA_TranslucentBackground);
        editor->setMinimumSize(0, 0);
        editor->document()->setDocumentMargin(items.value(selected).label->document()->documentMargin());
        editor->setTabChangesFocus(false);
        if (cursorPosition < 0) editor->selectAll();
        else editor->moveCursor(cursorPosition == 0 ? QTextCursor::Start : QTextCursor::End);
        editorProxy = scene->addWidget(editor);
        editorProxy->setZValue(5);
        QRectF bounds = items.value(selected).label->sceneBoundingRect();
        const qreal minimumHeight = editor->fontMetrics().height() + 8;
        const qreal maximumHeight = qMax(minimumHeight, qMin(320.0,
            canvas->viewport()->height() / canvas->transform().m22() - 60));
        bounds.setHeight(qBound(minimumHeight, bounds.height(), maximumHeight));
        editorProxy->setGeometry(bounds);
        items.value(selected).label->hide();
        canvas->ensureVisible(editorProxy, 30, 30);
        updateActionStates();
        editor->setFocus(Qt::OtherFocusReason);
        auto *cancel = new QShortcut(QKeySequence("Escape"), editor);
        connect(cancel, &QShortcut::activated, this, [this] { finishEdit(false); });
        connect(editor, &QPlainTextEdit::textChanged, this, [this] {
            statusBar()->clearMessage();
            scheduleRecovery();
        });
        statusBar()->clearMessage();
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
        if (items.contains(id)) items.value(id).label->show();
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
        statusBar()->clearMessage();
        updateActionStates();
        return true;
    }

    void splitEditedTopic() {
        if (!editor || editId == viewRoot()) return;
        const QString text = editor->toPlainText();
        const int cursor = editor->textCursor().position();
        if (cursor == 0 || cursor == text.size()) {
            statusBar()->showMessage(QStringLiteral("把光标放在文字中间，再拆分为两个同级主题"), 4000);
            return;
        }
        if (!topicTextError(text).isEmpty()) return;
        if (!topicTextError(text.left(cursor)).isEmpty() || !topicTextError(text.mid(cursor)).isEmpty()) return;
        Document next = doc;
        Node node;
        node.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        node.text = text.mid(cursor);
        node.style = doc.nodes.value(editId).style;
        node.rootLane = doc.nodes.value(editId).rootLane;
        const QString parent = parents.value(editId);
        next.nodes[editId].text = text.left(cursor);
        auto &siblings = next.nodes[parent].children;
        siblings.insert(siblings.indexOf(editId) + 1, node.id);
        next.nodes.insert(node.id, node);
        Document checked;
        QString error;
        if (!checked.parse(next.serialize(), &error)) { statusBar()->showMessage(error, 5000); return; }
        finishEdit(false);
        checkpoint();
        doc = std::move(checked);
        selected = node.id;
        changed();
        startEdit(0);
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
        if (!focusRoot.isEmpty()) {
            QString ancestor = lastMatch;
            while (!ancestor.isEmpty() && ancestor != focusRoot) ancestor = parents.value(ancestor);
            if (ancestor.isEmpty()) leaveFocus();
        }
        QStringList collapsed;
        for (QString id = lastMatch; id != viewRoot() && parents.contains(id);) {
            id = parents.value(id);
            if (doc.nodes.value(id).collapsed) collapsed.append(id);
        }
        if (!collapsed.isEmpty()) {
            checkpoint();
            for (const QString &id : collapsed) doc.nodes[id].collapsed = false;
            selected = lastMatch;
            changed();
        } else {
            selectNode(lastMatch);
        }
        canvas->centerOn(items.value(selected).body);
        statusBar()->showMessage(QStringLiteral("匹配 %1 / %2  ·  %3 下一个")
            .arg(index + 1).arg(matches.size()).arg(shortcutSettings.hint("Ctrl+Shift+F3")), 5000);
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

    QString exchangeInputFilter(bool native = false) const {
        const QString formats = QStringLiteral("*.mmap *.emmx *.mind *.mindnode *.mom *.pmom *.opml *.mm *.xmind *.drawio *.xml *.vsdx *.pptx *.md *.markdown *.txt *.mmd *.mermaid *.canvas");
        return QStringLiteral("支持的文件 (%1%2)").arg(native ? QStringLiteral("*.smap *.json ") : QString(), formats);
    }

    void importFile() {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("导入其他导图"), {},
            exchangeInputFilter());
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

    void updateShortcutHint() {
        QString hint, compact;
        if (editor) {
            hint = QStringLiteral("Enter 确认  ·  %1 换行  ·  Esc 取消").arg(shortcutText("Shift+Return"));
            compact = QStringLiteral("Enter 确认 · Esc 取消");
        } else if (outlineDock && outlineDock->isVisible() &&
                   (outlineTree->hasFocus() || outlineTree->isAncestorOf(QApplication::focusWidget()))) {
            if (outlineTree->editingText()) {
                hint = compact = QStringLiteral("Enter 确认 · Esc 取消");
            } else {
                compact = QStringLiteral("Enter 加同级 · Tab 缩进");
                hint = compact + QStringLiteral("  ·  Shift+Tab 提升一级  ·  F2 编辑文字");
            }
        } else if (gestureHint == GestureHint::Box) {
            compact = QStringLiteral("松开左键完成框选");
            hint = QStringLiteral("%1  ·  %2+单击增减选中项  ·  Esc 取消").arg(compact, copyModifierText());
        } else if (gestureHint == GestureHint::Move || gestureHint == GestureHint::Copy) {
            const QString copy = copyModifierText();
            compact = gestureHint == GestureHint::Copy
                ? QStringLiteral("松开左键完成复制")
                : QStringLiteral("按住 %1 复制").arg(copy);
            hint = gestureHint == GestureHint::Copy
                ? QStringLiteral("%1  ·  松开 %2 改为移动").arg(compact, copy)
                : QStringLiteral("移动分支  ·  %1  ·  松开左键完成").arg(compact);
            hint += QStringLiteral("  ·  Esc 取消");
        } else if (!additionalSelection.isEmpty()) {
            compact = QStringLiteral("%1 批量样式").arg(shortcutSettings.hint("Ctrl+Shift+F"));
            hint = QStringLiteral("已选 %1 项  ·  %2  ·  %3")
                .arg(additionalSelection.size() + 1)
                .arg(compact, hasCopiedStyle
                     ? QStringLiteral("%1 粘贴样式").arg(shortcutSettings.hint(pasteStyleShortcut))
                     : QStringLiteral("拖动任一项移动整组"));
        } else if (!focusRoot.isEmpty()) {
            compact = QStringLiteral("Esc 返回全图");
            hint = QStringLiteral("全图单击切换分支  ·  Esc 返回全图  ·  %1 导出当前分支").arg(shortcutSettings.hint("Ctrl+Shift+E"));
        } else if (canvas->areaSelectionEnabled()) {
            compact = QStringLiteral("拖动空白处框选");
            hint = QStringLiteral("空白处拖动框选  ·  中键拖动平移  ·  %1 退出框选").arg(shortcutSettings.hint(boxShortcut));
        } else if (selected != viewRoot()) {
            compact = QStringLiteral("%1 编辑 · %2 加子主题").arg(shortcutSettings.hint("F2"), shortcutSettings.hint("Tab"));
            hint = compact + QStringLiteral("  ·  %1 聚焦分支").arg(shortcutSettings.hint("F4"));
        } else {
            compact = QStringLiteral("%1 加子主题 · %2 帮助").arg(shortcutSettings.hint("Tab"), shortcutSettings.hint("F1"));
            hint = compact + QStringLiteral("  ·  Shift+拖动框选  ·  %1 格式转换").arg(shortcutSettings.hint(convertShortcut));
        }
        const QString visible = compactToolbar->isHidden() ? hint : compact;
        if (shortcutHint->text() != visible) shortcutHint->setText(visible);
        if (shortcutHint->toolTip() != hint) shortcutHint->setToolTip(hint);
    }

    void setGestureHint(GestureHint next) {
        if (gestureHint == next) return;
        gestureHint = next;
        if (next != GestureHint::None) statusBar()->clearMessage();
        updateShortcutHint();
    }

    void applyAppearance() {
        setProperty("zhiliHighContrast", appearance.highContrast);
        const QFont interfaceFont = fontFor(appearance.interfaceFont);
        if (QApplication::font() != interfaceFont) QApplication::setFont(interfaceFont);
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
            "QToolBar {background:%1;border:0;border-bottom:1px solid %2;padding:10px 12px;spacing:4px}"
            "QToolButton {color:%3;padding:5px 6px;border-radius:6px}"
            "QToolButton:hover,QToolButton:focus {background:%4}"
            "QToolButton:checked {background:%6;color:white}"
            "QToolButton#workspaceSave {background:%4}"
            "QToolButton#workspaceSave:hover {border:1px solid %6;padding:4px 5px}"
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
            "QLabel#focusPaneTitle {background:%7;color:%6;border-bottom:1px solid %2;padding:6px 10px;font-weight:bold}"
            "QSplitter::handle {background:%2}"
            "QGraphicsView {border:0}"
            "QDockWidget::title {background:%7;color:%6;padding:8px}"
            "QTreeWidget#outlineTree {background:%1;alternate-background-color:%7;color:%3;border:0;selection-background-color:%4;selection-color:%3}"
            "QTreeWidget#outlineTree::item {padding:5px 3px}"
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

    void makeZoomControls(QAction *fitAction, QAction *areaAction) {
        shortcutHint = new QLabel(statusBar());
        shortcutHint->setObjectName("shortcutHint");
        shortcutHint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        statusBar()->addWidget(shortcutHint, 1);
        workspaceStats = new QLabel(statusBar());
        workspaceStats->setObjectName("workspaceStats");
        statusBar()->addPermanentWidget(workspaceStats);
        auto *dock = new QFrame(statusBar());
        dock->setObjectName("zoomDock");
        auto *row = new QHBoxLayout(dock);
        row->setContentsMargins(3, 2, 3, 2);
        row->setSpacing(2);
        auto *areaButton = new QToolButton(dock);
        areaButton->setDefaultAction(areaAction);
        areaButton->setObjectName("areaSelectionButton");
        areaButton->setAccessibleName(areaAction->text());
        row->addWidget(areaButton);
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
        if (!focusRoot.isEmpty()) {
            Window fullMap(false, appearance);
            fullMap.loadExportView(doc);
            return fullMap.exportTo(path, format, error);
        }
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
        const QString previousDecoration = selectedDecoration;
        selectedDecoration.clear();
        renderDecorations(scene, items);
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
        selectedDecoration = previousDecoration;
        renderDecorations(scene, items);
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

    void loadExportView(Document document, bool expand = false) {
        if (expand)
            for (auto &node : document.nodes) node.collapsed = false;
        doc = std::move(document);
        selected = doc.root;
        render();
    }

    bool exportBranchTo(const QString &id, const QString &path, const QString &format, QString *error) {
        Document branch = extractBranch(doc, id);
        if (branch.root.isEmpty()) {
            *error = QStringLiteral("无法读取所选分支");
            return false;
        }
        return exportConverted(std::move(branch), path, format, error);
    }

    bool exportConverted(Document document, const QString &path, const QString &format, QString *error) {
        if (format == "smap") return writeAtomically(path, document.serialize(), error);
        if (format == "drawio" || format == "vsdx" || format == "opml" || format == "mm" || format == "canvas" || format == "txt")
            return exportExchange(document, path, format, error);
        Window preview(false, appearance);
        preview.loadExportView(std::move(document), true);
        return format == "pptx" ? preview.exportPowerPointTo(path, error) : preview.exportTo(path, format, error);
    }

    void convertFile() {
        const QString source = QFileDialog::getOpenFileName(this, QStringLiteral("选择要转换的文件"), {},
                                                            exchangeInputFilter(true));
        if (source.isEmpty()) return;
        const QFileInfo input(source);
        const QString extension = input.suffix().toLower();
        Document document;
        QString error;
        QStringList warnings;
        const bool loaded = extension == "smap" || extension == "json"
            ? document.readFile(source, &error) : importMindMap(source, &document, &warnings, &error);
        if (!loaded) {
            QMessageBox::warning(this, QStringLiteral("无法转换文件"), error);
            return;
        }
        if (!warnings.isEmpty()) warnings.removeFirst();
        if (!warnings.isEmpty()) {
            const auto decision = QMessageBox::information(this, QStringLiteral("转换内容"),
                warnings.join('\n'), QMessageBox::Ok | QMessageBox::Cancel);
            if (decision != QMessageBox::Ok) return;
        }
        const QStringList formats{"smap", "drawio", "vsdx", "pptx", "opml", "mm", "canvas", "txt", "svg", "png", "pdf"};
        const QStringList filters{QStringLiteral("枝理导图 (*.smap)"), QStringLiteral("draw.io 图形 (*.drawio)"),
            QStringLiteral("Visio 绘图 (*.vsdx)"), QStringLiteral("可编辑 PowerPoint (*.pptx)"), QStringLiteral("OPML 大纲 (*.opml)"),
            QStringLiteral("FreeMind 导图 (*.mm)"), QStringLiteral("JSON Canvas (*.canvas)"), QStringLiteral("纯文本大纲 (*.txt)"),
            QStringLiteral("SVG 矢量图 (*.svg)"),
            QStringLiteral("PNG 图片 (*.png)"), QStringLiteral("PDF 文档 (*.pdf)")};
        QString filter = filters.first();
        QString path = QFileDialog::getSaveFileName(this, QStringLiteral("选择转换格式并另存"),
            input.dir().filePath(input.completeBaseName() + QStringLiteral("-转换")), filters.join(";;"), &filter);
        if (path.isEmpty()) return;
        const QString format = formats.value(filters.indexOf(filter), formats.first());
        const bool appendSuffix = !path.endsWith("." + format, Qt::CaseInsensitive);
        if (appendSuffix) path += "." + format;
        const QFileInfo output(path);
        const QString original = input.canonicalFilePath(), destination = output.canonicalFilePath();
#ifdef Q_OS_WIN
        const auto pathCase = Qt::CaseInsensitive;
#else
        const auto pathCase = Qt::CaseSensitive;
#endif
        if (!destination.isEmpty() && original.compare(destination, pathCase) == 0) {
            QMessageBox::warning(this, QStringLiteral("请选择不同的文件名"), QStringLiteral("转换结果需要另存为新文件。"));
            return;
        }
        if (appendSuffix && output.exists() &&
            QMessageBox::question(this, QStringLiteral("替换文件"), QStringLiteral("“%1”已存在，是否替换？").arg(output.fileName()))
                != QMessageBox::Yes) return;
        if (!exportConverted(std::move(document), path, format, &error))
            QMessageBox::warning(this, QStringLiteral("转换失败"), error);
        else
            statusBar()->showMessage(QStringLiteral("已转换为 %1").arg(output.fileName()), 6000);
    }

    void exportBranch() {
        const QString id = selected;
        const QStringList formats{"png", "svg", "pdf", "pptx", "smap"};
        const QStringList filters{QStringLiteral("PNG 图片 (*.png)"), QStringLiteral("SVG 矢量图 (*.svg)"),
            QStringLiteral("PDF 文档 (*.pdf)"), QStringLiteral("可编辑 PowerPoint (*.pptx)"),
            QStringLiteral("枝理导图 (*.smap)")};
        QString filter = filters.first();
        QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出当前分支"), {},
                                                     filters.join(";;"), &filter);
        if (path.isEmpty()) return;
        const QString format = formats.value(filters.indexOf(filter), formats.first());
        if (!path.endsWith("." + format, Qt::CaseInsensitive)) path += "." + format;
        QString error;
        if (!exportBranchTo(id, path, format, &error)) QMessageBox::warning(this, QStringLiteral("分支导出失败"), error);
        else statusBar()->showMessage(QStringLiteral("已导出分支  %1").arg(QFileInfo(path).fileName()), 4000);
    }

    QImage branchImage(const QString &id) {
        Document branch = extractBranch(doc, id);
        if (branch.root.isEmpty()) return {};
        Window preview(false, appearance);
        preview.loadExportView(std::move(branch), true);
        preview.scene->setBackgroundBrush(Qt::NoBrush);
        preview.selected.clear();
        preview.styleNode(preview.doc.root);
        const QRectF bounds = preview.scene->itemsBoundingRect().adjusted(-16, -16, 16, 16);
        const QSize size(qCeil(bounds.width()), qCeil(bounds.height()));
        if (qint64(size.width()) * size.height() > 32000000 || size.width() > 32767 || size.height() > 32767) return {};
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        if (image.isNull()) return {};
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        preview.scene->render(&painter, QRectF(QPointF(), size), bounds);
        painter.end();
        return image;
    }

    void copyBranchImage() {
        const QImage image = branchImage(selected);
        if (image.isNull()) {
            statusBar()->showMessage(QStringLiteral("无法复制分支图片。分支过大时请导出 SVG 或 PDF。"), 5000);
            return;
        }
        QByteArray png;
        QBuffer buffer(&png);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
            statusBar()->showMessage(QStringLiteral("无法生成分支图片"), 5000);
            return;
        }
        if (!writeClipboard([&] {
            auto *data = new QMimeData;
            data->setImageData(image);
            data->setData("image/png", png);
            return data;
        })) {
            statusBar()->showMessage(QStringLiteral("剪贴板正忙，请重试复制图片"), 5000);
            return;
        }
        statusBar()->showMessage(QStringLiteral("已复制透明背景的分支图片"), 4000);
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
        item->setProperty("canvasOnly", canvasOnly);
        shortcutSettings.add(item, menu->title());
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

    void showHelp() {
        QDialog dialog(this);
        dialog.setObjectName("helpDialog");
        dialog.setWindowTitle(QStringLiteral("枝理使用帮助"));
        auto *layout = new QVBoxLayout(&dialog);
        auto *guide = new QTextBrowser(&dialog);
        guide->setOpenExternalLinks(true);
        guide->setSource(QUrl(QStringLiteral("qrc:/help/zh-CN.html")));
        layout->addWidget(guide);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);
        dialog.resize(QSize(680, 520).boundedTo(size()));
        dialog.exec();
    }

    void showAbout() {
        QMessageBox dialog(this);
        dialog.setObjectName("aboutDialog");
        dialog.setWindowTitle(QStringLiteral("关于枝理"));
        dialog.setIconPixmap(QApplication::windowIcon().pixmap(56, 56));
        dialog.setTextFormat(Qt::RichText);
        dialog.setText(QStringLiteral("<b>枝理 ZhiLi %1</b><br>离线桌面思维导图").arg(QCoreApplication::applicationVersion()));
        dialog.setInformativeText(QStringLiteral("使用开放的 SMAP 文件格式。<br>Qt %1 · %2<br>"
            "<a href='https://github.com/bhuyuqingguo/ZhiLi'>项目主页</a>")
            .arg(QString::fromLatin1(qVersion()), QSysInfo::currentCpuArchitecture()));
        dialog.setTextInteractionFlags(Qt::TextBrowserInteraction);
        dialog.addButton(QStringLiteral("关闭"), QMessageBox::AcceptRole);
        dialog.exec();
    }

    void makeMenus() {
        auto *file = menuBar()->addMenu(QStringLiteral("文件"));
        action(file, QStringLiteral("新建"), "Ctrl+N", [this] { if (confirmLeave()) resetDocument(); });
        action(file, QStringLiteral("从模板新建…"), "Ctrl+Shift+N", [this] { newFromTemplate(); })->setObjectName("newFromTemplateAction");
        auto *openAction = action(file, QStringLiteral("打开"), "Ctrl+O", [this] { openFile(); });
        action(file, QStringLiteral("导入其他导图…"), "Ctrl+Alt+O", [this] { importFile(); })->setObjectName("importMindMapAction");
        auto *convertAction = action(file, QStringLiteral("格式转换…"), convertShortcut, [this] { convertFile(); });
        convertAction->setObjectName("convertFileAction");
        convertAction->setToolTip(QStringLiteral("离线转换导图、Visio、draw.io 和 PowerPoint 文件"));
        auto *saveAction = action(file, QStringLiteral("保存"), "Ctrl+S", [this] { save(); });
        action(file, QStringLiteral("另存为"), "Ctrl+Shift+S", [this] { save(true); })
            ->setShortcuts({QKeySequence("Ctrl+Shift+S"), QKeySequence("F12")});
        file->addSeparator();
        action(file, QStringLiteral("关闭当前导图"), "Ctrl+W", [this] { if (confirmLeave()) resetDocument(); })
            ->setShortcuts({QKeySequence("Ctrl+W"), QKeySequence("Ctrl+F4")});
        for (const QString &format : {QString("png"), QString("svg"), QString("pdf")})
            action(file, QStringLiteral("导出 %1").arg(format.toUpper()), "", [this, format] { exportFile(format); });
        auto *exchangeMenu = file->addMenu(QStringLiteral("导出交换格式"));
        const QVector<QPair<QString, QString>> exchangeTypes{
            {"drawio", QStringLiteral("draw.io 图形")}, {"vsdx", QStringLiteral("Visio 绘图")},
            {"opml", QStringLiteral("OPML 大纲")}, {"mm", QStringLiteral("FreeMind 导图")},
            {"canvas", QStringLiteral("JSON Canvas")}, {"txt", QStringLiteral("纯文本大纲")}};
        for (const auto &type : exchangeTypes) {
            const QString format = type.first;
            action(exchangeMenu, type.second + QStringLiteral("…"), "", [this, format, type] {
                    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出交换格式"), {},
                                                                 QStringLiteral("%1 (*.%2)").arg(type.second, format));
                    if (path.isEmpty()) return;
                    if (!path.endsWith("." + format, Qt::CaseInsensitive)) path += "." + format;
                    QString error;
                    if (!exportExchange(doc, path, format, &error)) QMessageBox::warning(this, QStringLiteral("导出失败"), error);
                    else statusBar()->showMessage(QStringLiteral("已导出  %1").arg(QFileInfo(path).fileName()), 4000);
                });
        }
        auto *pptx = action(file, QStringLiteral("导出可编辑 PowerPoint…"), "", [this] { exportPowerPoint(); });
        pptx->setObjectName("exportPptxAction");
        pptx->setToolTip(QStringLiteral("导出可编辑形状和文字；图形符号可单独移动。包含折叠分支，大图自动分页，长文字附全文页。"));
        auto *branchExport = action(file, QStringLiteral("导出当前分支…"), "Ctrl+Shift+E", [this] { exportBranch(); }, true);
        branchExport->setObjectName("exportBranchAction");

        auto *edit = menuBar()->addMenu(QStringLiteral("编辑"));
        auto *relationAction = action(edit, QStringLiteral("插入关系线"), "Ctrl+R", [this] { beginRelationship(false); }, true);
        relationAction->setObjectName("relationshipAction");
        auto *straightRelation = action(edit, QStringLiteral("插入直线关系线"), "Ctrl+Shift+R", [this] { beginRelationship(true); }, true);
        auto *boundaryAction = action(edit, QStringLiteral("添加外框"), "Ctrl+Shift+B", [this] { addBoundary(); }, true);
        boundaryAction->setObjectName("boundaryAction");
        auto *floatingAction = action(edit, QStringLiteral("浮动主题：点击空白处放置"), "Alt+F", [this] {
            relationshipSource.clear(); placingFloating = true; canvas->setCursor(Qt::CrossCursor); updateActionStates();
            statusBar()->showMessage(QStringLiteral("点击空白处放置浮动主题 · Esc 取消"));
        }, true);
        floatingAction->setObjectName("floatingAction");
        action(edit, QStringLiteral("将选中分支转为浮动主题"), "", [this] { floatBranches(selectedRoots(false), QPointF(80, 80), false); }, true);
        auto *decorationEdit = action(edit, QStringLiteral("编辑关系线／外框标题…"), "", [this] { editDecoration(); }, true);
        auto *relationStyle = action(edit, QStringLiteral("关系线：切换直线／曲线"), "", [this] {
            for (int i = 0; i < doc.relationships.size(); ++i) if (doc.relationships[i].id == selectedDecoration) {
                checkpoint(); doc.relationships[i].straight = !doc.relationships[i].straight; changed(); break;
            }
        }, true);
        for (int axis = 0; axis < 4; ++axis) {
            const char *keys[] = {"Alt+Left", "Alt+Right", "Alt+Up", "Alt+Down"};
            const QPointF offsets[] = {QPointF(-10,0), QPointF(10,0), QPointF(0,-10), QPointF(0,10)};
            action(edit, QStringLiteral("微移浮动对象 %1").arg(axis + 1), keys[axis], [this, axis, offsets] {
                if (!selectedDecoration.isEmpty()) moveDecoration(offsets[axis]);
                else if (doc.floating.contains(selected)) floatBranches(selectedRoots(false), offsets[axis], false);
            }, true);
        }
        edit->addSeparator();
        auto *child = action(edit, QStringLiteral("新增子级"), "Tab", [this] { createNode(true); }, true);
        child->setShortcuts({QKeySequence("Tab"), QKeySequence("Insert"), QKeySequence("Ctrl+Return"),
                             QKeySequence("Ctrl+Enter")});
        siblingAction = action(edit, QStringLiteral("新增同级／主分支"), "Return",
                               [this] { createNode(selected == viewRoot()); }, true);
        siblingAction->setShortcuts({QKeySequence("Return"), QKeySequence("Enter")});
        action(edit, QStringLiteral("在前面新增同级"), "Shift+Return",
               [this] { createNode(selected == viewRoot(), true); }, true)
            ->setShortcuts({QKeySequence("Shift+Return"), QKeySequence("Shift+Enter")});
        action(edit, QStringLiteral("插入父主题"), "Shift+Insert", [this] { insertParent(); }, true);
        action(edit, QStringLiteral("添加多个主题…"), "Ctrl+M", [this] { addMultipleTopics(); }, true);
        auto *editTopic = action(edit, QStringLiteral("编辑主题"), "F2", [this] { startEdit(); }, true);
        action(edit, QStringLiteral("从文字末尾编辑"), "Space", [this] { startEdit(1); }, true);
        action(edit, QStringLiteral("从文字开头编辑"), "Shift+Space", [this] { startEdit(0); }, true);
        auto *notesAction = action(edit, QStringLiteral("主题备注…"), "Ctrl+T", [this] { editNotes(); }, true);
        notesAction->setObjectName("topicNotesAction");
        duplicateBranchAction = action(edit, QStringLiteral("就地复制分支"), "Ctrl+D",
                                       [this] { copyDraggedBranches({selected}, selected, 1); }, true);
        duplicateBranchAction->setObjectName("duplicateBranchAction");
        duplicateBranchAction->setShortcuts({QKeySequence("Ctrl+D"), QKeySequence("Ctrl+Alt+Down")});
        action(edit, QStringLiteral("向前复制分支"), "Ctrl+Alt+Up",
               [this] { copyDraggedBranches({selected}, selected, -1); }, true);
        auto *topicStyle = action(edit, QStringLiteral("主题样式…"), "Ctrl+Shift+F",
                                  [this] { changeTopicStyles(); }, true);
        auto *copyStyle = action(edit, QStringLiteral("复制主题样式"), "Ctrl+Shift+C", [this] {
            copiedStyle = doc.nodes.value(selected).style;
            hasCopiedStyle = true;
            updateActionStates();
            statusBar()->showMessage(QStringLiteral("已复制主题样式  ·  %1 粘贴").arg(shortcutSettings.hint(pasteStyleShortcut)), 4000);
        }, true);
        pasteStyleAction = action(edit, QStringLiteral("粘贴主题样式"), pasteStyleShortcut, [this] { pasteTopicStyle(); }, true);
        copyStyle->setObjectName("copyTopicStyleAction");
        pasteStyleAction->setObjectName("pasteTopicStyleAction");
        auto *symbolAction = action(edit, QStringLiteral("设置图形符号…"), "Ctrl+Shift+I", [this] { chooseSymbol(); }, true);
        symbolAction->setObjectName("symbolPickerAction");
        symbolAction->setShortcuts({QKeySequence("Ctrl+Shift+I"), QKeySequence("F9")});
        topicStyle->setShortcuts({QKeySequence("F6"), QKeySequence("Ctrl+Shift+F")});
        auto *textStyle = edit->addMenu(QStringLiteral("文字格式"));
        action(textStyle, QStringLiteral("加粗 / 取消加粗"), "Ctrl+B", [this] { quickTextStyle(0); }, true);
        action(textStyle, QStringLiteral("斜体 / 取消斜体"), "Ctrl+I", [this] { quickTextStyle(1); }, true);
        action(textStyle, QStringLiteral("下划线 / 取消下划线"), "Ctrl+U", [this] { quickTextStyle(2); }, true);
        action(textStyle, QStringLiteral("删除线 / 取消删除线"), "Ctrl+Shift+X", [this] { quickTextStyle(3); }, true);
        action(textStyle, QStringLiteral("增大字号"), "Ctrl+Shift+.", [this] { quickTextStyle(4); }, true);
        action(textStyle, QStringLiteral("减小字号"), "Ctrl+Shift+,", [this] { quickTextStyle(5); }, true);
        action(textStyle, QStringLiteral("文字颜色…"), "Ctrl+Shift+D", [this] { chooseTextColor(); }, true);
        removeAction = action(edit, QStringLiteral("删除分支"), "Delete", [this] { removeSelected(); }, true);
        removeAction->setShortcuts({QKeySequence("Delete"), QKeySequence("Backspace")});
        action(edit, QStringLiteral("移除主题并保留子级"), "Shift+Delete",
               [this] { removeTopicKeepChildren(); }, true);
        collapseAction = action(edit, QStringLiteral("折叠 / 展开"), "Ctrl+F3", [this] { toggleCollapse(); }, true);
        edit->addSeparator();
        auto *copy = action(edit, QStringLiteral("复制分支"), "Ctrl+C", [this] { copyBranch(); }, true);
        copy->setObjectName("copyBranchAction");
        action(edit, QStringLiteral("复制为子主题"), "Ctrl+Insert", [this] { duplicateAsChild(); }, true);
        auto *copyImage = action(edit, QStringLiteral("复制当前分支为图片"), "Ctrl+Alt+I", [this] { copyBranchImage(); }, true);
        copyImage->setObjectName("copyBranchImageAction");
        cutAction = action(edit, QStringLiteral("剪切分支"), "Ctrl+X", [this] { copyBranch(true); }, true);
        cutAction->setObjectName("cutBranchAction");
        auto *paste = action(edit, QStringLiteral("粘贴分支 / 文本大纲"), "Ctrl+V", [this] { pasteBranch(); }, true);
        paste->setObjectName("pasteBranchAction");
        action(edit, QStringLiteral("粘贴为同级"), "Ctrl+Alt+V", [this] { pasteBranch(true); }, true);
        edit->addSeparator();
        auto *undo = action(edit, QStringLiteral("撤销"), "Ctrl+Z", [this] { history(true); }, true);
        undoAction = undo;
        undoAction->setShortcuts({QKeySequence("Ctrl+Z"), QKeySequence("Alt+Backspace")});
        redoAction = action(edit, QStringLiteral("重做"), "Ctrl+Y", [this] { history(false); }, true);
        redoAction->setShortcuts({QKeySequence("Ctrl+Y"), QKeySequence("Ctrl+Shift+Z"), QKeySequence("Alt+Shift+Backspace")});
        auto *find = action(edit, QStringLiteral("查找"), "Ctrl+F", [this] { search(true); });
        action(edit, QStringLiteral("查找下一个"), "Ctrl+Shift+F3", [this] { search(false); }, true);

        auto *navigation = edit->addMenu(QStringLiteral("移动选中项"));
        action(navigation, QStringLiteral("向上"), "Up", [this] { navigate(Qt::Key_Up); }, true);
        action(navigation, QStringLiteral("向下"), "Down", [this] { navigate(Qt::Key_Down); }, true);
        action(navigation, QStringLiteral("向左"), "Left", [this] { navigate(Qt::Key_Left); }, true);
        action(navigation, QStringLiteral("向右"), "Right", [this] { navigate(Qt::Key_Right); }, true);
        action(navigation, QStringLiteral("向上扩展选择"), "Shift+Up", [this] { navigate(Qt::Key_Up, true); }, true);
        action(navigation, QStringLiteral("向下扩展选择"), "Shift+Down", [this] { navigate(Qt::Key_Down, true); }, true);
        action(navigation, QStringLiteral("向左扩展选择"), "Shift+Left", [this] { navigate(Qt::Key_Left, true); }, true);
        action(navigation, QStringLiteral("向右扩展选择"), "Shift+Right", [this] { navigate(Qt::Key_Right, true); }, true);
        action(navigation, QStringLiteral("选择同级首项"), "Home", [this] { navigateBoundary(false); }, true);
        action(navigation, QStringLiteral("选择同级末项"), "End", [this] { navigateBoundary(true); }, true);
        action(navigation, QStringLiteral("扩选到同级首项"), "Shift+Home", [this] { navigateBoundary(false, true); }, true);
        action(navigation, QStringLiteral("扩选到同级末项"), "Shift+End", [this] { navigateBoundary(true, true); }, true);
        action(navigation, QStringLiteral("选择中心主题"), "Ctrl+Home", [this] {
            selectNode(viewRoot());
            canvas->centerOn(items.value(selected).body);
        }, true);
        auto *selection = edit->addMenu(QStringLiteral("选择主题"));
        action(selection, QStringLiteral("选择所有可见主题"), "Ctrl+A", [this] {
            QStringList ids{selected};
            for (auto it = items.cbegin(); it != items.cend(); ++it)
                if (it.key() != selected) ids.append(it.key());
            replaceSelection(ids);
        }, true);
        action(selection, QStringLiteral("选择所有同级主题"), "Ctrl+Shift+A", [this] { selectRelated(SelectionKind::Siblings); }, true);
        action(selection, QStringLiteral("选择相同层级"), "Alt+A", [this] { selectRelated(SelectionKind::Level); }, true);
        action(selection, QStringLiteral("选择下一级"), "Ctrl+Alt+A", [this] { selectRelated(SelectionKind::Children); }, true);
        action(selection, QStringLiteral("选择所有父主题"), "", [this] { selectRelated(SelectionKind::Ancestors); }, true);
        action(selection, QStringLiteral("选择整个分支"), "", [this] { selectRelated(SelectionKind::Branch); }, true);
        auto *reorder = edit->addMenu(QStringLiteral("调整分支顺序"));
        moveUpAction = action(reorder, QStringLiteral("前移"), "Ctrl+Shift+Up", [this] { reorderSelected(true); }, true);
        moveDownAction = action(reorder, QStringLiteral("后移"), "Ctrl+Shift+Down", [this] { reorderSelected(false); }, true);
        action(reorder, QStringLiteral("移到同级开头"), "Ctrl+Shift+Home", [this] { reorderSelected(true, true); }, true);
        action(reorder, QStringLiteral("移到同级末尾"), "Ctrl+Shift+End", [this] { reorderSelected(false, true); }, true);
        action(reorder, QStringLiteral("提升一级"), "Ctrl+]", [this] { changeBranchLevel(true); }, true);
        action(reorder, QStringLiteral("降为前项的子主题"), "Ctrl+[", [this] { changeBranchLevel(false); }, true);
        action(reorder, QStringLiteral("合并所选同级主题"), "", [this] { mergeSelectedTopics(); }, true);
        reorder->addSeparator();
        sortActions.append(action(reorder, QStringLiteral("按标题升序排列子主题"), "",
                                  [this] { sortChildren(true); }, true));
        sortActions.append(action(reorder, QStringLiteral("按标题降序排列子主题"), "",
                                  [this] { sortChildren(false); }, true));

        auto *view = menuBar()->addMenu(QStringLiteral("视图"));
        action(view, QStringLiteral("大纲面板"), "F10", [this] { toggleOutline(); }, true)
            ->setShortcuts({QKeySequence("F10"), QKeySequence("Ctrl+Shift+O")});
        auto *detail = view->addMenu(QStringLiteral("分支展开层级"));
        action(detail, QStringLiteral("折叠整个分支"), "Alt+Shift+0", [this] { setBranchDetail(0); }, true);
        action(detail, QStringLiteral("展开一层"), "Alt+Shift+1", [this] { setBranchDetail(1); }, true);
        action(detail, QStringLiteral("展开两层"), "Alt+Shift+2", [this] { setBranchDetail(2); }, true);
        action(detail, QStringLiteral("展开全部层级"), "", [this] { setBranchDetail(-1); }, true);
        action(detail, QStringLiteral("逐层展开所选分支"), "Alt+D", [this] { revealNextLevel(); }, true);
        auto *levels = view->addMenu(QStringLiteral("显示导图层级"));
        for (int level = 1; level <= 9; ++level) {
            auto *item = action(levels, QStringLiteral("显示到第 %1 层").arg(level), "",
                               [this, level] { setBranchDetail(level, true); }, true);
            item->setShortcut(QKeySequence(QStringLiteral("Alt+%1").arg(level)));
        }
        action(levels, QStringLiteral("显示所有层级"), "Alt+0", [this] { setBranchDetail(-1, true); }, true);
        action(view, QStringLiteral("外观设置…"), "Ctrl+,", [this] { changeAppearance(); });
        focusAction = action(view, QStringLiteral("聚焦当前分支"), "", [this] { focusBranch(); }, true);
        action(view, QStringLiteral("下钻 / 上钻（双区聚焦）"), "F4", [this] {
            if (!focusRoot.isEmpty()) leaveFocus();
            else focusBranch();
        }, true);
        leaveFocusAction = action(view, QStringLiteral("返回全图／取消插入"), "Escape", [this] {
            relationshipSource.clear(); placingFloating = false; selectedDecoration.clear(); canvas->unsetCursor();
            leaveFocus(); render();
        }, true);
        focusAction->setObjectName("focusBranchAction");
        leaveFocusAction->setObjectName("leaveFocusAction");
        view->addSeparator();
        classicAction = action(view, QStringLiteral("经典双侧"), "Ctrl+Alt+1", [this] { chooseLayout(true); });
        hierarchyAction = action(view, QStringLiteral("结构图"), "Ctrl+Alt+2", [this] { chooseLayout(false); });
        rightLayoutAction = action(view, QStringLiteral("向右展开"), "Ctrl+Alt+3",
                                   [this] { chooseLayout(true, BranchDirection::Right); });
        leftLayoutAction = action(view, QStringLiteral("向左展开"), "Ctrl+Alt+4",
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
        auto *fitAction = action(view, QStringLiteral("适应窗口"), "Alt+Shift+F3", [this] { fit(); }, true);
        auto *areaAction = view->addAction(QStringLiteral("框选模式"));
        areaAction->setObjectName("areaSelectionAction");
        areaAction->setProperty("canvasOnly", true);
        shortcutSettings.add(areaAction, QStringLiteral("视图"));
        areaAction->setCheckable(true);
        canvasActions.append(areaAction);
        areaAction->setShortcut(QKeySequence(QString::fromLatin1(boxShortcut)));
        areaAction->setToolTip(QStringLiteral("框选模式 · 在画布空白处拖动"));
        areaAction->setIcon(toolbarIcon("M4 8V4h4 M16 4h4v4 M20 16v4h-4 M8 20H4v-4 M11 4h2 M11 20h2 M4 11v2 M20 11v2"));
        connect(areaAction, &QAction::toggled, this, [this](bool enabled) {
            canvas->setAreaSelection(enabled);
            statusBar()->clearMessage();
            updateShortcutHint();
        });
        auto *help = menuBar()->addMenu(QStringLiteral("帮助"));
        auto *startupAnimation = help->addAction(QStringLiteral("播放启动动画"));
        startupAnimation->setCheckable(true);
        startupAnimation->setChecked(QSettings().value("appearance/startupAnimation", true).toBool());
        connect(startupAnimation, &QAction::toggled, this, [](bool enabled) {
            QSettings().setValue("appearance/startupAnimation", enabled);
        });
        auto *guide = help->addAction(QStringLiteral("使用帮助"), this, &Window::showHelp);
        guide->setObjectName("helpAction");
        guide->setShortcut(QKeySequence(Qt::Key_F1));
        shortcutSettings.add(guide, QStringLiteral("帮助"));
        action(help, QStringLiteral("快捷键设置…"), "Ctrl+Alt+,", [this] { shortcutSettings.showDialog(this); })
            ->setObjectName("shortcutSettingsAction");
        help->addAction(QStringLiteral("关于枝理"), this, &Window::showAbout)->setObjectName("aboutAction");
        for (auto *item : {openAction, saveAction, convertAction, child, topicStyle, undo, find,
                           fitAction, areaAction, leaveFocusAction})
            item->setToolTip(QStringLiteral("%1 (%2)").arg(item->toolTip(), item->shortcut().toString(QKeySequence::NativeText)));
        action(view, QStringLiteral("居中所选主题"), "F3", [this] {
            if (items.contains(selected)) canvas->centerOn(items.value(selected).body);
        }, true);
        saveAction->setIcon(toolbarIcon("M4 4h13l3 3v13H4z M8 4v6h8V4 M8 20v-6h8v6"));
        openAction->setIcon(toolbarIcon("M3 7V5h7l2 2h9v3 M3 7v13h15l4-10H7l-4 10"));
        convertAction->setIcon(toolbarIcon("M3 8h17 M16 4l4 4-4 4 M21 16H4 M8 12l-4 4 4 4"));
        child->setIcon(toolbarIcon("M4 5v8a3 3 0 0 0 3 3h3 M15 11v10 M10 16h10"));
        undo->setIcon(toolbarIcon("M9 5l-5 5 5 5 M4 10h10a5 5 0 0 1 0 10h-3"));
        find->setIcon(toolbarIcon("M16 16l4 4 M18 10a8 8 0 1 1-16 0 8 8 0 0 1 16 0z"));
        fitAction->setIcon(toolbarIcon("M9 4H4v5 M15 4h5v5 M20 15v5h-5 M9 20H4v-5"));
        leaveFocusButton = new QToolButton(statusBar());
        leaveFocusButton->setDefaultAction(leaveFocusAction);
        leaveFocusButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
        statusBar()->addPermanentWidget(leaveFocusButton);
        makeZoomControls(fitAction, areaAction);

        auto *context = new QMenu(canvas);
        context->setObjectName("topicContextMenu");
        for (auto *item : {relationAction, straightRelation, boundaryAction, floatingAction, decorationEdit, relationStyle}) context->addAction(item);
        context->addSeparator();
        for (auto *item : {editTopic, notesAction, topicStyle, copyStyle, pasteStyleAction, symbolAction, child, siblingAction})
            context->addAction(item);
        context->addSeparator();
        context->addMenu(reorder);
        context->addMenu(selection);
        context->addMenu(textStyle);
        context->addSeparator();
        for (auto *item : {copy, cutAction, paste, duplicateBranchAction, copyImage, branchExport}) context->addAction(item);
        context->addSeparator();
        for (auto *item : {collapseAction, removeAction, focusAction, leaveFocusAction, fitAction}) context->addAction(item);
        context->addMenu(connectorMenu);
        context->addMenu(detail);
        canvas->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(canvas, &QWidget::customContextMenuRequested, this, [this, context](const QPoint &at) {
            if (!finishEdit()) return;
            const QString id = nodeAt(at);
            if (id.isEmpty()) { selectedDecoration = decorationAt(at); render(); }
            if (additionalSelection.contains(id)) {
                QStringList ids{id, selected};
                for (const auto &other : additionalSelection)
                    if (other != id) ids.append(other);
                replaceSelection(ids);
            } else if (!id.isEmpty() && id != selected) {
                selectNode(id);
            }
            context->popup(canvas->viewport()->mapToGlobal(at));
        });

        action(view, QStringLiteral("放大"), "Ctrl+=", [this] { canvas->zoomBy(1.25); }, true);
        action(view, QStringLiteral("缩小"), "Ctrl+-", [this] { canvas->zoomBy(1 / 1.25); }, true);
        action(view, QStringLiteral("实际大小"), "Ctrl+0", [this] {
            canvas->zoomBy(1 / canvas->transform().m11());
        }, true);
        action(view, QStringLiteral("适应窗口宽度"), "Alt+F3", [this] {
            const QRectF bounds = scene->itemsBoundingRect().adjusted(-45, -45, 45, 45);
            const qreal scale = qBound(0.05, canvas->viewport()->width() / qMax(1.0, bounds.width()), 1.25);
            canvas->zoomBy(scale / canvas->transform().m11());
            canvas->centerOn(bounds.center());
        }, true);
        for (int direction : {-1, 1}) {
            auto *vertical = action(view, direction < 0 ? QStringLiteral("向上翻页") : QStringLiteral("向下翻页"), "", [this, direction] {
                auto *bar = canvas->verticalScrollBar();
                bar->setValue(bar->value() + direction * bar->pageStep());
            }, true);
            vertical->setShortcut(QKeySequence(direction < 0 ? "PgUp" : "PgDown"));
            auto *horizontal = action(view, direction < 0 ? QStringLiteral("向左翻页") : QStringLiteral("向右翻页"), "", [this, direction] {
                auto *bar = canvas->horizontalScrollBar();
                bar->setValue(bar->value() + direction * bar->pageStep());
            }, true);
            horizontal->setShortcut(QKeySequence(direction < 0 ? "Ctrl+PgUp" : "Ctrl+PgDown"));
        }
        action(view, QStringLiteral("全屏 / 退出全屏"), "F11", [this] {
            if (isFullScreen()) showNormal(); else showFullScreen();
        })->setShortcuts({QKeySequence("F11"), QKeySequence("Ctrl+F5")});

        wideToolbar = addToolBar(QStringLiteral("常用"));
        auto *bar = wideToolbar;
        bar->setObjectName("wideToolbar");
        bar->setMovable(false);
        bar->setFloatable(false);
        bar->setIconSize(QSize(19, 19));
        auto *brand = new QLabel(QStringLiteral("枝理"), bar);
        auto *mark = new QLabel(bar);
        mark->setPixmap(QApplication::windowIcon().pixmap(25, 25));
        bar->addWidget(mark);
        brand->setObjectName("workspaceBrand");
        bar->addWidget(brand);
        documentLabel = new QLabel(bar);
        documentLabel->setObjectName("documentLabel");
        bar->addWidget(documentLabel);
        saveState = new QLabel(bar);
        saveState->setObjectName("saveState");
        bar->addWidget(saveState);
        bar->addSeparator();
        bar->addAction(openAction);
        bar->addAction(saveAction);
        auto *convertButton = new QToolButton(bar);
        convertButton->setDefaultAction(convertAction);
        convertButton->setText(QStringLiteral("格式转换"));
        convertButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        convertButton->setAccessibleName(QStringLiteral("格式转换"));
        convertButton->setObjectName("workspaceConvert");
        bar->addWidget(convertButton);
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
        for (auto *item : {child, relationAction, boundaryAction, floatingAction, topicStyle, undo, find}) bar->addAction(item);
        relationAction->setIcon(toolbarIcon("M4 17C4 3 20 3 20 17M16 13L20 17L22 12"));
        boundaryAction->setIcon(toolbarIcon("M8 4H5Q3 4 3 6V9M16 4H19Q21 4 21 6V9M3 15V18Q3 20 5 20H8M21 15V18Q21 20 19 20H16"));
        floatingAction->setIcon(toolbarIcon("M5 7H19Q21 7 21 9V15Q21 17 19 17H5Q3 17 3 15V9Q3 7 5 7M9 12H15M12 9V15"));
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
            button->setToolTip(action->toolTip());
            button->setEnabled(action->isEnabled());
            connect(button, &QToolButton::clicked, action, &QAction::trigger);
            connect(action, &QAction::changed, button, [button, action] {
                button->setEnabled(action->isEnabled());
                button->setToolTip(action->toolTip());
            });
            compactToolbar->addWidget(button);
            compactButtons.append(button);
        };
        compactButton(saveAction, QStringLiteral("保存"));
        compactButton(convertAction, QStringLiteral("转换"));
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

    void adjustFocusSplit(bool resetSizes = false) {
        const auto orientation = width() < 760 ? Qt::Vertical : Qt::Horizontal;
        if (focusSplitter->orientation() != orientation) {
            focusSplitter->setOrientation(orientation);
            resetSizes = true;
        }
        if (resetSizes) {
            const int span = orientation == Qt::Horizontal ? focusSplitter->width() : focusSplitter->height();
            focusSplitter->setSizes({span * 2 / 5, span * 3 / 5});
        }
    }

#include "decorations.inc"

    void render() {
        const bool lostFocus = !focusRoot.isEmpty() && !doc.nodes.contains(focusRoot);
        if (lostFocus) focusRoot.clear();
        parents.clear();
        const auto &nodes = qAsConst(doc.nodes);
        for (const Node &node : nodes)
            for (const auto &child : node.children) parents.insert(child, node.id);

        const QString rootId = viewRoot();
        QSet<QString> highlights = additionalSelection;
        highlights.insert(selected);
        renderMap(rootId, scene, rootRail, items, edges, leafRails, highlights);
        if (!items.contains(selected)) selected = rootId;
        additionalSelection.remove(selected);
        for (auto it = additionalSelection.begin(); it != additionalSelection.end();) {
            if (!items.contains(*it)) it = additionalSelection.erase(it);
            else ++it;
        }
        styleNode(selected);

        if (!focusRoot.isEmpty()) {
            const bool opening = overviewPane->isHidden();
            if (opening) {
                overviewRail = overviewScene->addPath(QPainterPath());
                overviewRail->setZValue(-1);
                overviewPane->show();
                focusTitle->show();
                adjustFocusSplit(true);
            }
            const QString title = QStringLiteral("聚焦分支  ·  %1").arg(nodes[focusRoot].text.section('\n', 0, 0).left(120));
            focusTitle->setText(title);
            focusTitle->setToolTip(title);
            overviewScene->setBackgroundBrush(paper);
            QMap<QString, qreal> overviewLeafRails;
            renderMap(doc.root, overviewScene, overviewRail, overviewItems, overviewEdges, overviewLeafRails, {focusRoot});
            overview->refresh(opening);
        } else if (!overviewPane->isHidden()) {
            overviewPane->hide();
            focusTitle->hide();
            overviewItems.clear();
            overviewEdges.clear();
            overviewScene->clear();
            overviewRail = nullptr;
        }
        updateLayoutActions();
        updateActionStates();
        updateTitle();
        syncOutline();
        if (lostFocus) restoreFullMapView();
    }

    void renderMap(const QString &rootId, QGraphicsScene *scene, QGraphicsPathItem *rootRail,
                   QMap<QString, NodeItem> &items, QMap<QString, QGraphicsPathItem *> &edges,
                   QMap<QString, qreal> &leafRails, const QSet<QString> &highlights) {
        const auto &nodes = qAsConst(doc.nodes);
        const QVector<QColor> colors{QColor("#258265"), QColor("#5478ac"), QColor("#a36730"),
                                    QColor("#8a609a"), QColor("#a95765"), QColor("#457a87")};
        QMap<QString, QColor> branchColors;
        auto branches = nodes[rootId].children;
        for (auto it = doc.floating.cbegin(); it != doc.floating.cend(); ++it) branches.removeAll(it.key());
        for (int i = 0; i < branches.size(); ++i)
            branchColors.insert(branches.at(i), appearance.highContrast ? line : colors.at(i % colors.size()));
        QMap<QString, QSizeF> sizes;
        QVector<QString> visible{rootId};
        for (int i = 0; i < visible.size(); ++i) {
            const QString id = visible.at(i);
            const Node &node = nodes[id];
            for (const auto &child : node.children)
                if (!node.collapsed || (id == doc.root && doc.floating.contains(child))) visible.append(child);
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
            const bool root = id == rootId;
            const QString parentId = parents.value(id);
            const bool branch = parentId == rootId;
            item.branchColor = root ? moss : (branch ? branchColors.value(id, moss) : items.value(parentId).branchColor);
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
            if (node.notes.isEmpty()) {
                delete item.note;
                item.note = nullptr;
            } else if (!item.note) {
                item.note = scene->addSimpleText(QStringLiteral("备注"));
                item.note->setZValue(3);
                item.note->setData(0, id);
                item.note->setAcceptedMouseButtons(Qt::NoButton);
            }
            if (item.note) {
                QFont noteFont = QApplication::font();
                noteFont.setPointSize(9);
                item.note->setFont(noteFont);
                item.note->setBrush(root ? QColor("#d5eee3") : appearance.highContrast ? ink : item.branchColor.darker(125));
                item.note->setToolTip(QStringLiteral("双击编辑主题备注（Ctrl+T）"));
            }
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
            const qreal noteWidth = item.note ? item.note->boundingRect().width() + 12 : 0;
            const QSizeF size(label.width() + padding + (asset ? 42 : 0) + noteWidth,
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
            delete it->note;
            delete it->symbol;
            it = items.erase(it);
        }
        for (auto it = edges.begin(); it != edges.end();) {
            if (it.key() != rootId && sizes.contains(it.key()) && !doc.floating.contains(it.key())) { ++it; continue; }
            delete it.value();
            it = edges.erase(it);
        }

        QVector<qreal> layerSizes;
        Document view = doc;
        view.root = rootId;
        const auto positions = layoutNodes(view, sizes, &leafRails, &layerSizes);
        QPainterPath rail;
        qreal rootRailY = 0;
        const bool automatic = doc.connectorStyle == ConnectorStyle::Automatic;
        const ConnectorStyle connectorStyle = automatic
            ? (doc.mindMap ? ConnectorStyle::Curved : ConnectorStyle::Orthogonal) : doc.connectorStyle;
        const bool useRootRail = !doc.mindMap && connectorStyle == ConnectorStyle::Orthogonal;
        if (useRootRail && !nodes[rootId].collapsed && !branches.isEmpty()) {
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
        QSet<QString> floatingTree;
        for (const QString &id : visible) {
            const QString parent = parents.value(id);
            if (doc.floating.contains(id) || floatingTree.contains(parent)) floatingTree.insert(id);
            const int level = id == rootId ? 0 : depths.value(parent) + 1;
            depths.insert(id, level);
            const QPointF point = positions.value(id);
            const NodeItem item = items.value(id);
            const QSizeF label = item.label->boundingRect().size();
            const QSizeF size = sizes.value(id);
            item.body->setPos(point);
            const bool symbolVisible = item.symbol && item.symbol->isVisible();
            const qreal noteWidth = item.note ? item.note->boundingRect().width() + 12 : 0;
            const QPointF contentCenter = point - QPointF(noteWidth / 2, 0);
            item.label->setPos(contentCenter - QPointF(label.width() / 2 - (symbolVisible ? 21 : 0), label.height() / 2));
            if (symbolVisible) {
                const auto iconSize = item.symbol->boundingRect().size() * item.symbol->scale();
                item.symbol->setPos(contentCenter + QPointF(-label.width() / 2 - 21 + (32 - iconSize.width()) / 2,
                                                     -iconSize.height() / 2));
            }
            if (item.note) {
                const QRectF bounds = item.note->boundingRect();
                item.note->setPos(point + QPointF(size.width() / 2 - bounds.width() - 8, -bounds.height() / 2));
            }
            item.badge->setPos(point + QPointF(size.width() / 2 + 5, -item.badge->boundingRect().height() / 2));
            paintNodeOutline(item, id == rootId, highlights.contains(id));
            if (id == rootId || doc.floating.contains(id)) continue;

            QPointF a = positions.value(parent);
            QPointF b = point;
            QPainterPath path;
            if (floatingTree.contains(id)) {
                if (doc.mindMap) {
                    a.rx() += sizes.value(parent).width() / 2;
                    b.rx() -= size.width() / 2;
                } else {
                    a.ry() += sizes.value(parent).height() / 2;
                    b.ry() -= size.height() / 2;
                }
                path = layerConnector(a, b, doc.mindMap ? a.x() : a.y(), doc.mindMap ? b.x() : b.y(),
                                      doc.mindMap, connectorStyle, false);
            } else if (doc.mindMap) {
                const qreal direction = b.x() > a.x() ? 1 : -1;
                const qreal fromLane = a.x() + direction * layerSizes.at(level - 1) / 2;
                const qreal toLane = b.x() - direction * layerSizes.at(level) / 2;
                a.rx() += direction * sizes.value(parent).width() / 2;
                b.rx() -= direction * size.width() / 2;
                path = layerConnector(a, b, fromLane, toLane, true, connectorStyle, false);
            } else if (useRootRail && parent == rootId) {
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
            const bool primary = parent == rootId;
            const QColor connector = appearance.highContrast ? line : tint(item.branchColor, primary ? 16 : 44);
            edges.value(id)->setPen(QPen(connector, primary ? 2.9 : 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        }
        renderDecorations(scene, items);
        scene->setSceneRect(scene->itemsBoundingRect().adjusted(-100, -100, 100, 100));
    }

    void updateToolbarMode(int width) {
        if (!wideToolbar || !compactToolbar) return;
        // Hidden/overflowed toolbars temporarily report a smaller hint. Keep the
        // largest measured natural width so changing mode cannot oscillate.
        wideToolbarNaturalWidth = qMax(wideToolbarNaturalWidth, wideToolbar->sizeHint().width());
        const bool compact = width < qMax(840, wideToolbarNaturalWidth + 24);
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
        updateShortcutHint();
    }

protected:
    bool event(QEvent *event) override {
        const bool result = QMainWindow::event(event);
        if (event->type() == QEvent::LayoutRequest && wideToolbar && compactToolbar)
            QTimer::singleShot(0, this, [this] { updateToolbarMode(width()); });
        return result;
    }

    void resizeEvent(QResizeEvent *event) override {
        QMainWindow::resizeEvent(event);
        if (!focusRoot.isEmpty()) adjustFocusSplit();
        updateToolbarMode(event->size().width());
        // Child toolbars settle their size hints after QMainWindow's layout pass.
        QTimer::singleShot(0, this, [this] { updateToolbarMode(width()); });
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
    friend int runEditingTest();
#endif
};

#ifdef ZHILI_TEST
#ifdef ZHILI_EDITING_TEST
#include "../tests/editing.inc"
#else
#include "../tests/smoke.inc"
#endif
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
    app.setApplicationVersion(QStringLiteral(ZHILI_VERSION));
    app.setOrganizationName("ZhiLi");
    registerEmbeddedFonts();
    QApplication::setWindowIcon(QIcon(":/branding/zhili.svg"));
    QGuiApplication::setDesktopFileName(QStringLiteral("org.zhili.simplemap"));
#ifdef ZHILI_TEST
#ifdef ZHILI_EDITING_TEST
    return runEditingTest();
#else
    return runSmokeTest();
#endif
#else
    Window window;
    window.showMaximized();
    const auto focusCanvas = [&window] {
        if (!QApplication::activeModalWidget())
            if (auto *canvas = window.findChild<QWidget *>("mapCanvas")) canvas->setFocus();
    };
    focusCanvas();
    if (QSettings().value("appearance/startupAnimation", true).toBool())
        new StartupOverlay(&window, focusCanvas);
    return app.exec();
#endif
}
