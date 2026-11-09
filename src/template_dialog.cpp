#include "template_dialog.h"

#include "layout.h"
#include "templates.h"
#include <QtWidgets>
#include <functional>

namespace {

class TemplatePreview : public QGraphicsView {
public:
    explicit TemplatePreview(QWidget *parent) : QGraphicsView(parent) {
        setObjectName(QStringLiteral("templatePreview"));
        setAccessibleName(QStringLiteral("模板只读预览"));
        setScene(new QGraphicsScene(this));
        setRenderHint(QPainter::Antialiasing);
        setDragMode(QGraphicsView::ScrollHandDrag);
        setBackgroundBrush(palette().base());
        setMinimumSize(260, 200);
    }

    void showDocument(const Document &document) {
        scene()->clear();
        resetTransform();
        if (document.nodes.isEmpty()) {
            scene()->setSceneRect(QRectF());
            setProperty("nodeCount", 0);
            return;
        }
        QMap<QString, QSizeF> sizes;
        QMap<QString, QGraphicsTextItem *> labels;
        for (const auto &node : document.nodes) {
            QFont font = QApplication::font();
            font.setPointSizeF(qMax(qreal(11), font.pointSizeF()));
            if (node.id == document.root) font.setBold(true);
            auto *label = scene()->addText(node.text, font);
            label->setTextWidth(qBound(qreal(150), label->boundingRect().width() + 4, qreal(260)));
            label->setDefaultTextColor(node.id == document.root ? palette().highlightedText().color()
                                                                          : palette().text().color());
            QTextOption option = label->document()->defaultTextOption();
            option.setAlignment(Qt::AlignCenter);
            label->document()->setDefaultTextOption(option);
            labels.insert(node.id, label);
            sizes.insert(node.id, label->boundingRect().size() + QSizeF(20, 12));
        }
        const auto positions = layoutNodes(document, sizes);
        const auto rectangle = [&](const QString &id) {
            const auto size = sizes.value(id);
            return QRectF(positions.value(id) - QPointF(size.width() / 2, size.height() / 2), size);
        };
        for (const auto &node : document.nodes) {
            const auto parentRect = rectangle(node.id);
            for (const auto &child : node.children) {
                const auto childRect = rectangle(child);
                QPointF start, end, control1, control2;
                if (document.mindMap) {
                    const bool right = childRect.center().x() > parentRect.center().x();
                    start = QPointF(right ? parentRect.right() : parentRect.left(), parentRect.center().y());
                    end = QPointF(right ? childRect.left() : childRect.right(), childRect.center().y());
                    const qreal middle = (start.x() + end.x()) / 2;
                    control1 = QPointF(middle, start.y());
                    control2 = QPointF(middle, end.y());
                } else {
                    start = QPointF(parentRect.center().x(), parentRect.bottom());
                    end = QPointF(childRect.center().x(), childRect.top());
                    const qreal middle = (start.y() + end.y()) / 2;
                    control1 = QPointF(start.x(), middle);
                    control2 = QPointF(end.x(), middle);
                }
                QPainterPath path(start);
                path.cubicTo(control1, control2, end);
                auto *edge = scene()->addPath(path, QPen(palette().mid().color(), 2));
                edge->setZValue(-2);
            }
            const auto rect = rectangle(node.id);
            auto *box = scene()->addRect(rect, QPen(palette().mid().color(), 1),
                node.id == document.root ? palette().highlight() : palette().alternateBase());
            box->setZValue(-1);
            labels.value(node.id)->setPos(rect.topLeft() + QPointF(10, 6));
        }
        scene()->setSceneRect(scene()->itemsBoundingRect().adjusted(-30, -30, 30, 30));
        setProperty("nodeCount", document.nodes.size());
        fitPreview();
    }

    void fitPreview() {
        if (!scene()->items().isEmpty()) fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
    }

    void readablePreview() {
        resetTransform();
        centerOn(0, 0);
    }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QGraphicsView::resizeEvent(event);
        fitPreview();
    }
};

class TemplateDialog : public QDialog {
public:
    explicit TemplateDialog(QWidget *parent) : QDialog(parent), entries(zhili::builtInTemplates()) {
        setObjectName(QStringLiteral("templateDialog"));
        setWindowTitle(QStringLiteral("从模板新建"));
        setSizeGripEnabled(true);
        resize(1040, 680);
        QScreen *screen = parent && parent->windowHandle() ? parent->windowHandle()->screen()
                                                         : QApplication::primaryScreen();
        const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1100, 800);
        resize(qMin(width(), qMax(240, available.width() - 32)),
               qMin(height(), qMax(220, available.height() - 32)));
        compact = available.width() < 720 || available.height() < 520;

        auto *outer = new QVBoxLayout(this);
        auto *scroll = new QScrollArea(this);
        scroll->setObjectName(QStringLiteral("templateBodyScroll"));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setMinimumSize(0, 0);
        auto *body = new QWidget(scroll);
        auto *content = new QVBoxLayout(body);
        content->setContentsMargins(0, 0, 0, 0);
        scroll->setWidget(body);
        outer->addWidget(scroll, 1);
        auto *intro = new QLabel(QStringLiteral("选择一个起点，再按实际内容修改。模板完全离线，预览不会更改当前导图。"), this);
        intro->setWordWrap(true);
        content->addWidget(intro);
        intro->setVisible(!compact);
        auto *filters = new QHBoxLayout;
        category = new QComboBox(this);
        category->setObjectName(QStringLiteral("templateCategory"));
        category->setAccessibleName(QStringLiteral("模板分类"));
        category->addItem(QStringLiteral("全部分类"), QString());
        QSet<QString> seen;
        for (const auto &entry : entries) {
            if (!seen.contains(entry.category)) {
                seen.insert(entry.category);
                category->addItem(entry.category, entry.category);
            }
        }
        search = new QLineEdit(this);
        search->setObjectName(QStringLiteral("templateSearch"));
        search->setAccessibleName(QStringLiteral("搜索模板"));
        search->setPlaceholderText(QStringLiteral("搜索名称、说明或标签…"));
        search->setClearButtonEnabled(true);
        if (compact) category->setMaximumWidth(qMax(120, available.width() / 2));
        search->setMinimumWidth(60);
        filters->addWidget(category);
        filters->addWidget(search, 1);
        content->addLayout(filters);

        auto *splitter = new QSplitter(this);
        splitter->setChildrenCollapsible(false);
        list = new QListWidget(splitter);
        list->setObjectName(QStringLiteral("templateList"));
        list->setAccessibleName(QStringLiteral("模板列表"));
        list->setMinimumWidth(190);
        list->setMinimumHeight(100);
        list->setTextElideMode(Qt::ElideRight);
        list->setSpacing(3);
        auto *detail = new QWidget(splitter);
        auto *detailLayout = new QVBoxLayout(detail);
        detailLayout->setContentsMargins(8, 0, 0, 0);
        title = new QLabel(detail);
        title->setObjectName(QStringLiteral("templateTitle"));
        QFont titleFont = title->font();
        titleFont.setBold(true);
        title->setFont(titleFont);
        title->setWordWrap(true);
        description = new QLabel(detail);
        description->setObjectName(QStringLiteral("templateDescription"));
        description->setWordWrap(true);
        metadata = new QLabel(detail);
        metadata->setObjectName(QStringLiteral("templateMetadata"));
        metadata->setWordWrap(true);
        detailLayout->addWidget(title);
        detailLayout->addWidget(description);
        detailLayout->addWidget(metadata);
        auto *previewTabs = new QTabWidget(detail);
        previewTabs->setObjectName(QStringLiteral("templatePreviewTabs"));
        preview = new TemplatePreview(previewTabs);
        tree = new QTreeWidget(previewTabs);
        tree->setObjectName(QStringLiteral("templateOutline"));
        tree->setAccessibleName(QStringLiteral("模板节点大纲"));
        tree->setHeaderHidden(true);
        tree->setAlternatingRowColors(true);
        tree->setWordWrap(true);
        tree->setIndentation(18);
        previewTabs->addTab(preview, QStringLiteral("导图预览"));
        previewTabs->addTab(tree, QStringLiteral("节点大纲"));
        detailLayout->addWidget(previewTabs, 1);
        auto *previewTools = new QHBoxLayout;
        auto *fit = new QPushButton(QStringLiteral("适应预览"), detail);
        fit->setObjectName(QStringLiteral("templateFitPreview"));
        fit->setAutoDefault(false);
        auto *readable = new QPushButton(QStringLiteral("原大查看"), detail);
        readable->setObjectName(QStringLiteral("templateReadablePreview"));
        readable->setAutoDefault(false);
        auto *previewHint = new QLabel(QStringLiteral("只读 · 拖动平移"), detail);
        previewHint->setWordWrap(true);
        previewTools->addWidget(fit);
        previewTools->addWidget(readable);
        previewTools->addStretch();
        previewTools->addWidget(previewHint);
        detailLayout->addLayout(previewTools);
        splitter->addWidget(list);
        splitter->addWidget(detail);
        splitter->setStretchFactor(0, 0);
        splitter->setStretchFactor(1, 1);
        splitter->setSizes({270, 690});
        content->addWidget(splitter, 1);
        if (compact) {
            detail->hide();
            auto *toggle = new QPushButton(QStringLiteral("查看模板预览"), body);
            toggle->setObjectName(QStringLiteral("templateTogglePreview"));
            toggle->setAutoDefault(false);
            content->addWidget(toggle);
            connect(toggle, &QPushButton::clicked, this, [this, detail, toggle] {
                const bool showPreview = detail->isHidden();
                detail->setVisible(showPreview);
                list->setVisible(!showPreview);
                toggle->setText(showPreview ? QStringLiteral("返回模板列表") : QStringLiteral("查看模板预览"));
            });
        }

        count = new QLabel(this);
        count->setObjectName(QStringLiteral("templateCount"));
        count->setWordWrap(true);
        content->addWidget(count);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
        create = buttons->addButton(QStringLiteral("从模板新建"), QDialogButtonBox::AcceptRole);
        create->setObjectName(QStringLiteral("templateCreateButton"));
        create->setDefault(true);
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
        buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("templateCancelButton"));
        outer->addWidget(buttons);

        connect(search, &QLineEdit::textChanged, this, [this] { filter(); });
        connect(category, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                this, [this] { filter(); });
        connect(list, &QListWidget::currentRowChanged, this, [this] { updatePreview(); });
        connect(list, &QListWidget::itemDoubleClicked, this, [this] { acceptSelection(); });
        connect(buttons, &QDialogButtonBox::accepted, this, [this] { acceptSelection(); });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(fit, &QPushButton::clicked, preview, [this] { preview->fitPreview(); });
        connect(readable, &QPushButton::clicked, preview, [this] { preview->readablePreview(); });
        auto *find = new QShortcut(QKeySequence::Find, this);
        connect(find, &QShortcut::activated, search, [this] { search->setFocus(); search->selectAll(); });
        filter();
        search->setFocus();
    }

    QString selectedId() const { return chosen; }

private:
    QVector<zhili::TemplateInfo> entries;
    QComboBox *category;
    QLineEdit *search;
    QListWidget *list;
    QLabel *title, *description, *metadata, *count;
    TemplatePreview *preview;
    QTreeWidget *tree;
    QPushButton *create;
    QString chosen;
    bool previewValid = false;
    bool compact = false;

    void filter() {
        const QString previous = list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString();
        const QString selectedCategory = category->currentData().toString();
        const QString query = search->text().simplified();
        const QStringList words = query.isEmpty() ? QStringList() : query.split(' ');
        QSignalBlocker blocker(list);
        list->clear();
        int restore = 0;
        for (const auto &entry : entries) {
            if (!selectedCategory.isEmpty() && entry.category != selectedCategory) continue;
            const QString haystack = entry.title + ' ' + entry.description + ' ' + entry.category
                + ' ' + entry.id + ' ' + entry.tags.join(' ');
            bool matches = true;
            for (const auto &word : words) if (!haystack.contains(word, Qt::CaseInsensitive)) matches = false;
            if (!matches) continue;
            auto *item = new QListWidgetItem(entry.title + QStringLiteral("\n%1 · %2 个节点")
                                             .arg(entry.category).arg(entry.nodeCount), list);
            item->setData(Qt::UserRole, entry.id);
            item->setToolTip(entry.description);
            if (entry.id == previous) restore = list->count() - 1;
        }
        if (list->count()) list->setCurrentRow(restore);
        count->setText((compact ? QStringLiteral("显示 %1 / %2 个模板")
                                : QStringLiteral("显示 %1 / %2 个模板 · 所有模板可编辑，可切换两种布局"))
                       .arg(list->count()).arg(entries.size()));
        updatePreview();
    }

    void updatePreview() {
        previewValid = false;
        create->setEnabled(false);
        tree->clear();
        if (!list->currentItem()) {
            title->setText(entries.isEmpty() ? QStringLiteral("模板目录不可用") : QStringLiteral("没有匹配的模板"));
            description->setText(entries.isEmpty() ? QStringLiteral("请检查应用资源是否完整。")
                                                  : QStringLiteral("试试其他关键词，或切换到全部分类。"));
            metadata->clear();
            preview->showDocument(Document());
            return;
        }
        const QString id = list->currentItem()->data(Qt::UserRole).toString();
        for (const auto &entry : entries) {
            if (entry.id != id) continue;
            title->setText(entry.title);
            description->setText(entry.description);
            metadata->setText(QStringLiteral("%1 · %2 个节点 · %3\n标签：%4")
                .arg(entry.category).arg(entry.nodeCount)
                .arg(entry.mindMap ? QStringLiteral("经典导图") : QStringLiteral("结构图"))
                .arg(entry.tags.join(QStringLiteral("、"))));
            Document document;
            QString error;
            previewValid = zhili::loadBuiltInTemplate(id, &document, &error);
            preview->showDocument(document);
            if (previewValid) {
                std::function<QTreeWidgetItem *(const QString &)> add = [&](const QString &nodeId) {
                    const auto &node = document.nodes[nodeId];
                    auto *item = new QTreeWidgetItem(QStringList(node.text));
                    for (const auto &child : node.children) item->addChild(add(child));
                    return item;
                };
                tree->addTopLevelItem(add(document.root));
                tree->expandAll();
            }
            if (!previewValid) description->setText(error);
            create->setEnabled(previewValid);
            return;
        }
    }

    void acceptSelection() {
        if (!previewValid || !list->currentItem()) return;
        chosen = list->currentItem()->data(Qt::UserRole).toString();
        accept();
    }
};

} // namespace

QString zhili::chooseBuiltInTemplate(QWidget *parent) {
    TemplateDialog dialog(parent);
    return dialog.exec() == QDialog::Accepted ? dialog.selectedId() : QString();
}
