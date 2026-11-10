#include "symbolpicker.h"
#include "symbols.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QPainter>
#include <QSvgRenderer>
#include <QMap>
#include <QScreen>
#include <QResizeEvent>
#include <QWindow>
#include <QSlider>
#include <QVBoxLayout>

namespace {
// Render directly so portable builds do not need the optional qsvgicon plugin.
QIcon symbolIcon(const SymbolAsset &asset, bool dimensional) {
    static QMap<QString, QIcon> cache;
    const QString key = dimensional ? asset.id + "/dimensional" : asset.id;
    const auto existing = cache.constFind(key);
    if (existing != cache.constEnd()) return existing.value();
    QSvgRenderer renderer(SymbolLibrary::svg(asset, dimensional));
    QIcon icon;
    for (int pixels : {32, 64, 128, 192}) {
        QPixmap pixmap(pixels, pixels);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        renderer.render(&painter);
        painter.end();
        icon.addPixmap(pixmap);
    }
    cache.insert(key, icon);
    return icon;
}
}

SymbolPickerDialog::SymbolPickerDialog(QWidget *parent, const QString &initialId, bool dimensional)
    : QDialog(parent), dimensional_(dimensional), currentId_(initialId), selectedId_(initialId) {
    setObjectName(QStringLiteral("symbolPickerDialog"));
    setWindowTitle(QStringLiteral("素材库 · 离线矢量符号"));
    auto *layout = new QVBoxLayout(this);
    // Secondary details collapse before the layout can impose an oversized
    // minimum. This also lets an already-open dialog shrink onto a small screen.
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    layout->setContentsMargins(8, 8, 8, 8);
    intro_ = new QLabel(QStringLiteral("按分类浏览，或搜索名称与关键词。选择素材后用于当前主题。"), this);
    intro_->setWordWrap(true);
    layout->addWidget(intro_);

    filters_ = new QHBoxLayout;
    category_ = new QComboBox(this);
    category_->setObjectName(QStringLiteral("symbolCategory"));
    category_->setAccessibleName(QStringLiteral("素材分类"));
    category_->addItem(QStringLiteral("全部分类"), QString());
    for (const SymbolCategory &category : SymbolLibrary::categories())
        category_->addItem(category.name, category.id);
    filters_->addWidget(category_);
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("symbolSearch"));
    search_->setAccessibleName(QStringLiteral("搜索素材"));
    search_->setPlaceholderText(QStringLiteral("搜索：无人机、通信、AUV、camera…"));
    search_->setClearButtonEnabled(true);
    filters_->addWidget(search_, 1);
    layout->addLayout(filters_);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("symbolList"));
    list_->setAccessibleName(QStringLiteral("素材搜索结果"));
    list_->setViewMode(QListView::IconMode);
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setWordWrap(true);
    list_->setSpacing(6);
    list_->setUniformItemSizes(true);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setMinimumSize(80, 48);
    layout->addWidget(list_, 1);

    auto *detail = new QHBoxLayout;
    preview_ = new QLabel(this);
    preview_->setObjectName(QStringLiteral("symbolPreview"));
    preview_->setMinimumSize(72, 72);
    preview_->setAlignment(Qt::AlignCenter);
    detail->addWidget(preview_);
    description_ = new QLabel(this);
    description_->setObjectName(QStringLiteral("symbolDescription"));
    description_->setWordWrap(true);
    detail->addWidget(description_, 1);
    layout->addLayout(detail);

    auto *options = new QHBoxLayout;
    count_ = new QLabel(this);
    count_->setObjectName(QStringLiteral("symbolCount"));
    options->addWidget(count_, 1);
    sizeLabel_ = new QLabel(QStringLiteral("缩略图"), this);
    options->addWidget(sizeLabel_);
    size_ = new QSlider(Qt::Horizontal, this);
    size_->setObjectName(QStringLiteral("symbolSize"));
    size_->setAccessibleName(QStringLiteral("素材缩略图大小"));
    size_->setRange(32, 64);
    size_->setValue(40);
    size_->setMaximumWidth(150);
    sizeLabel_->setBuddy(size_);
    options->addWidget(size_);
    layout->addLayout(options);

    note_ = new QLabel(QStringLiteral("原创示意符号 · 128 个 / 8 类 · CC0-1.0\n仅用于活动、体系、能力与架构表达，不代表技术参数或运行方案。"), this);
    note_->setWordWrap(true);
    layout->addWidget(note_);
    auto *buttons = new QDialogButtonBox(this);
    remove_ = buttons->addButton(QStringLiteral("移除素材"), QDialogButtonBox::ResetRole);
    remove_->setObjectName(QStringLiteral("symbolRemove"));
    remove_->setEnabled(!initialId.isEmpty());
    auto *cancel = buttons->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);
    cancel->setObjectName(QStringLiteral("symbolCancel"));
    apply_ = buttons->addButton(QStringLiteral("使用素材"), QDialogButtonBox::AcceptRole);
    apply_->setObjectName(QStringLiteral("symbolApply"));
    apply_->setAccessibleName(QStringLiteral("使用素材"));
    apply_->setToolTip(QStringLiteral("将所选素材用于当前主题"));
    remove_->setAccessibleName(QStringLiteral("移除素材"));
    remove_->setToolTip(QStringLiteral("移除当前主题的素材"));
    // Enter in the search box must not accidentally apply the first result.
    apply_->setAutoDefault(false);
    cancel->setAutoDefault(false);
    remove_->setAutoDefault(false);
    layout->addWidget(buttons);

    connect(search_, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(category_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, [this] { refresh(); });
    connect(size_, &QSlider::valueChanged, this, [this] { resizeTiles(); });
    connect(list_, &QListWidget::currentItemChanged, this, [this] { updateSelection(); });
    connect(list_, &QListWidget::itemActivated, this, [this] { applySelection(); });
    connect(apply_, &QPushButton::clicked, this, [this] { applySelection(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(remove_, &QPushButton::clicked, this, [this] {
        selectedId_.clear();
        accept();
    });

    refresh();
    resizeTiles();
    QSize available(1024, 768);
    QScreen *screen = QGuiApplication::primaryScreen();
    if (parent && parent->window()->windowHandle())
        screen = parent->window()->windowHandle()->screen();
    if (screen) available = screen->availableGeometry().size();
    // Width can grow freely on ultrawide displays; the grid reflows rather than
    // stretching labels or forcing a single row across the whole monitor.
    const QSize requested(qMin(920, available.width() * 9 / 10), qMin(700, available.height() * 9 / 10));
    updateResponsiveLayout(requested);
    resize(requested);
    search_->setFocus();
}

void SymbolPickerDialog::refresh() {
    const QString remember = currentId_;
    const QList<SymbolAsset> assets = SymbolLibrary::search(search_->text(), category_->currentData().toString());
    list_->blockSignals(true);
    list_->clear();
    QListWidgetItem *selected = nullptr;
    for (const SymbolAsset &asset : assets) {
        auto *item = new QListWidgetItem(symbolIcon(asset, dimensional_), asset.name, list_);
        item->setData(Qt::UserRole, asset.id);
        item->setData(Qt::AccessibleTextRole, asset.name + QStringLiteral("，") + asset.categoryName);
        item->setToolTip(asset.name + QStringLiteral(" · ") + asset.englishName + QLatin1Char('\n') + asset.id);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        if (asset.id == remember) selected = item;
    }
    if (selected) {
        list_->setCurrentItem(selected);
        list_->scrollToItem(selected);
    }
    list_->blockSignals(false);
    count_->setText(assets.isEmpty() ? QStringLiteral("没有匹配的素材")
                                  : QStringLiteral("%1 / %2 个素材").arg(assets.size()).arg(SymbolLibrary::assets().size()));
    resizeTiles();
    updateSelection();
}

void SymbolPickerDialog::updateSelection() {
    const QListWidgetItem *item = list_->currentItem();
    const SymbolAsset *asset = item ? SymbolLibrary::find(item->data(Qt::UserRole).toString()) : nullptr;
    apply_->setEnabled(asset != nullptr);
    if (asset) {
        currentId_ = asset->id;
        // Select a prepared raster at the screen DPR without an SVG icon plugin.
        preview_->setPixmap(symbolIcon(*asset, dimensional_).pixmap(windowHandle(), QSize(64, 64)));
        description_->setText(asset->name + QStringLiteral(" · ") + asset->englishName + QLatin1Char('\n') + asset->categoryName);
    } else {
        preview_->clear();
        if (!SymbolLibrary::loadError().isEmpty())
            description_->setText(SymbolLibrary::loadError());
        else if (!list_->count())
            description_->setText(QStringLiteral("没有匹配的素材。试试更短的关键词，或切换到全部分类。"));
        else
            description_->setText(QStringLiteral("选择一个素材预览，双击或按“使用素材”应用。"));
    }
}

void SymbolPickerDialog::resizeTiles() {
    const int icon = size_->value();
    const QFontMetrics metrics(list_->font());
    const int tileWidth = qMax(icon + 24, metrics.boundingRect(QStringLiteral("水下作业机械臂")).width() + 20);
    list_->setIconSize(QSize(icon, icon));
    const QSize tile = compact_ ? QSize(qMax(80, list_->viewport()->width() - 4), qMax(icon, metrics.height()) + 10)
                               : QSize(tileWidth, icon + metrics.height() * 2 + 18);
    list_->setGridSize(compact_ ? QSize() : tile);
    for (int index = 0; index < list_->count(); ++index) {
        list_->item(index)->setSizeHint(tile);
        list_->item(index)->setTextAlignment(compact_ ? Qt::AlignLeft | Qt::AlignVCenter
                                                     : Qt::AlignHCenter | Qt::AlignTop);
    }
}

void SymbolPickerDialog::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    updateResponsiveLayout(event->size());
}

void SymbolPickerDialog::updateResponsiveLayout(const QSize &available) {
    const QFontMetrics metrics(font());
    compact_ = available.height() < qMax(520, metrics.height() * 16) ||
               available.width() < qMax(560, metrics.height() * 12);
    intro_->setVisible(!compact_);
    note_->setVisible(!compact_);
    preview_->setVisible(!compact_);
    description_->setVisible(!compact_);
    size_->setVisible(!compact_);
    sizeLabel_->setVisible(!compact_);
    apply_->setText(compact_ ? QStringLiteral("使用") : QStringLiteral("使用素材"));
    remove_->setText(compact_ ? QStringLiteral("移除") : QStringLiteral("移除素材"));
    const int filterWidth = category_->minimumSizeHint().width() + search_->minimumSizeHint().width() + 32;
    filters_->setDirection(available.width() < filterWidth ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    list_->setViewMode(compact_ ? QListView::ListMode : QListView::IconMode);
    list_->setWordWrap(!compact_);
    list_->setSpacing(compact_ ? 2 : 6);
    resizeTiles();
}

void SymbolPickerDialog::applySelection() {
    const QListWidgetItem *item = list_->currentItem();
    if (!item) return;
    const QString id = item->data(Qt::UserRole).toString();
    if (!SymbolLibrary::find(id)) return;
    selectedId_ = id;
    accept();
}
