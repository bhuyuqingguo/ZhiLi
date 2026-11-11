#include "shortcuts.h"
#include <QtWidgets>

namespace {
QString keysText(const QList<QKeySequence> &keys, QKeySequence::SequenceFormat format = QKeySequence::NativeText) {
    QStringList result;
    for (const auto &key : keys) if (!key.isEmpty()) result.append(key.toString(format));
    return result.join(QStringLiteral("；"));
}
QString settingKey(const QString &id) {
    return QStringLiteral("shortcuts/v1/") + QString::fromLatin1(id.toUtf8().toBase64(QByteArray::Base64UrlEncoding));
}
}

void ShortcutSettings::add(QAction *action, const QString &group) {
    for (const auto &entry : entries) if (entry.action == action) return;
    entries.append({action, group, {}, {}, {}});
}

ShortcutSettings::Bindings ShortcutSettings::current() const {
    Bindings result;
    for (const auto &entry : entries) result.insert(entry.action, entry.action->shortcuts());
    return result;
}

ShortcutSettings::Bindings ShortcutSettings::defaults() const {
    Bindings result;
    for (const auto &entry : entries) result.insert(entry.action, entry.defaults);
    return result;
}

bool ShortcutSettings::validate(const Bindings &bindings, QString *error) const {
    const auto reject = [error](const QString &message) { if (error) *error = message; return false; };
    QVector<QPair<QKeySequence, QAction *>> assigned;
    for (const auto &entry : entries) {
        if (!bindings.contains(entry.action)) return reject(QStringLiteral("快捷键方案不完整"));
        const auto keys = bindings.value(entry.action);
        if (keys.size() > 4) return reject(QStringLiteral("每项最多设置四组快捷键"));
        for (const auto &key : keys) {
            if (key.isEmpty()) continue;
            if (key.count() != 1 || key[0] == Qt::Key_unknown)
                return reject(QStringLiteral("请使用单次按键组合，例如 Ctrl+Alt+K"));
            if (key == QKeySequence(Qt::Key_Escape) && !entry.defaults.contains(key))
                return reject(QStringLiteral("Esc 用于取消编辑、拖拽和关闭窗口，不能分配给其他命令"));
            // Commands that stay active during text entry must not intercept
            // ordinary typing, navigation, newlines or text clipboard aliases.
            const int stroke = key[0];
            const int bare = stroke & ~int(Qt::KeyboardModifierMask);
            const bool functionKey = bare >= Qt::Key_F1 && bare <= Qt::Key_F35;
            const bool modified = stroke & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
            if (!entry.action->property("canvasOnly").toBool() && !entry.defaults.contains(key) &&
                ((!modified && !functionKey) || key == QKeySequence("Ctrl+Return") || key == QKeySequence("Ctrl+Enter")))
                return reject(QStringLiteral("此命令在文字编辑时也可用，请设置带 Ctrl / Alt 的组合键或功能键"));
            for (const auto &other : assigned) {
                if (key.matches(other.first) != QKeySequence::NoMatch || other.first.matches(key) != QKeySequence::NoMatch)
                    return reject(QStringLiteral("%1 已用于“%2”，与“%3”冲突，请先修改或清除原绑定")
                                  .arg(key.toString(QKeySequence::NativeText), other.second->text(), entry.action->text()));
            }
            assigned.append(qMakePair(key, entry.action));
        }
    }
    return true;
}

bool ShortcutSettings::apply(const Bindings &bindings, QString *error, bool persist) {
    if (!validate(bindings, error)) return false;
    QSettings settings;
    for (const auto &entry : entries) {
        auto keys = bindings.value(entry.action);
        keys.removeAll(QKeySequence());
        entry.action->setShortcuts(keys);
        const QString text = keysText(keys);
        entry.action->setToolTip(entry.description + (text.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(text)));
        if (persist) {
            if (keys == entry.defaults) settings.remove(settingKey(entry.id));
            else {
                QStringList saved;
                for (const auto &key : keys) saved.append(key.toString(QKeySequence::PortableText));
                settings.setValue(settingKey(entry.id), saved);
            }
        }
    }
    if (persist) settings.sync();
    if (changed) changed();
    if (persist && settings.status() != QSettings::NoError) {
        if (error) *error = QStringLiteral("快捷键已在本次运行生效，但设置文件写入失败，重启后可能恢复原值");
        return false;
    }
    return true;
}

void ShortcutSettings::load() {
    QSettings settings;
    Bindings bindings;
    for (auto &entry : entries) {
        entry.id = entry.action->objectName().isEmpty() ? entry.action->text() : entry.action->objectName();
        entry.defaults = entry.action->shortcuts();
        entry.defaults.removeAll(QKeySequence());
        entry.description = entry.action->toolTip();
        const QString oldSuffix = QStringLiteral(" (%1)").arg(entry.action->shortcut().toString(QKeySequence::NativeText));
        if (entry.description.endsWith(oldSuffix)) entry.description.chop(oldSuffix.size());
        auto keys = entry.defaults;
        if (settings.contains(settingKey(entry.id))) {
            keys.clear();
            for (const auto &text : settings.value(settingKey(entry.id)).toStringList())
                keys.append(QKeySequence(text, QKeySequence::PortableText));
        }
        bindings.insert(entry.action, keys);
    }
    QString error;
    if (!apply(bindings, &error, false)) {
        // Version upgrades can introduce a new default that collides with an
        // older override. Use a coherent working map, retaining saved settings.
        qWarning().noquote() << QStringLiteral("快捷键设置未加载：") + error;
        apply(defaults(), nullptr, false);
    }
}

QString ShortcutSettings::hint(const char *defaultKey) const {
    const QKeySequence key(QString::fromLatin1(defaultKey));
    for (const auto &entry : entries) {
        if (entry.defaults.contains(key)) {
            const auto currentKey = entry.action->shortcut();
            return currentKey.isEmpty() ? QStringLiteral("菜单") : currentKey.toString(QKeySequence::NativeText);
        }
    }
    return key.toString(QKeySequence::NativeText);
}

void ShortcutSettings::showDialog(QWidget *parent) {
    QDialog dialog(parent);
    dialog.setObjectName("shortcutSettingsDialog");
    dialog.setWindowTitle(QStringLiteral("快捷键设置"));
    dialog.setFont(parent->font());
    if (!parent->property("zhiliHighContrast").toBool()) {
        dialog.setStyleSheet(QStringLiteral(
            "QDialog { background:#f3f6f0; color:#244d3d; }"
            "QLabel { color:#567365; padding:4px 0; }"
            "QTabWidget::pane { border:1px solid #d5dfd3; background:#fafcf8; border-radius:8px; }"
            "QTabBar::tab { padding:10px 18px; background:#e6ede3; color:#567365; border:0; margin-right:4px; }"
            "QTabBar::tab:selected { background:#285b48; color:#ffffff; }"
            "QLineEdit, QKeySequenceEdit { background:#ffffff; color:#244d3d; border:1px solid #c8d8c8; border-radius:6px; padding:8px; }"
            "QTableWidget { background:#ffffff; alternate-background-color:#f3f7f0; color:#244d3d; border:1px solid #d5dfd3; "
            "gridline-color:#e7ede3; selection-background-color:#dcebdd; selection-color:#193f31; }"
            "QHeaderView::section { background:#e9f0e5; color:#466753; border:0; border-bottom:1px solid #ccd9c9; padding:8px; }"
            "QPushButton { background:#ffffff; color:#285b48; border:1px solid #c8d8c8; border-radius:6px; padding:8px 13px; }"
            "QPushButton:hover { background:#e4efdf; border-color:#7ca68b; }"
            "QPushButton:pressed { background:#cfdfca; }"
            "QPushButton:default { background:#285b48; color:#ffffff; border-color:#285b48; }"));
    }
    auto *layout = new QVBoxLayout(&dialog);
    auto *tabs = new QTabWidget(&dialog);
    layout->addWidget(tabs);
    auto *page = new QWidget(tabs);
    auto *pageLayout = new QVBoxLayout(page);
    auto *filter = new QLineEdit(page);
    filter->setObjectName("shortcutSearch");
    filter->setPlaceholderText(QStringLiteral("搜索功能或快捷键"));
    filter->setClearButtonEnabled(true);
    pageLayout->addWidget(filter);
    auto *table = new QTableWidget(entries.size(), 4, page);
    table->setObjectName("shortcutTable");
    table->setHorizontalHeaderLabels({QStringLiteral("类别"), QStringLiteral("功能"), QStringLiteral("当前快捷键"), QStringLiteral("默认快捷键")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(qMax(34, dialog.fontMetrics().height() + 14));
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    pageLayout->addWidget(table, 1);
    Bindings pending = current();
    const auto refresh = [&] {
        for (int row = 0; row < entries.size(); ++row) {
            const auto &entry = entries.at(row);
            const QStringList columns{entry.group, entry.action->text(), keysText(pending.value(entry.action)), keysText(entry.defaults)};
            for (int col = 0; col < columns.size(); ++col) {
                auto *cell = new QTableWidgetItem(columns.at(col));
                cell->setToolTip(columns.at(col));
                table->setItem(row, col, cell);
            }
            table->setRowHidden(row, !columns.join(' ').contains(filter->text(), Qt::CaseInsensitive));
        }
    };
    refresh();
    QObject::connect(filter, &QLineEdit::textChanged, &dialog, [&](const QString &) { refresh(); });
    auto *keysRow = new QHBoxLayout;
    auto *keyEdit = new QKeySequenceEdit(page);
    keyEdit->setObjectName("shortcutKeyEdit");
    keyEdit->setAccessibleName(QStringLiteral("新快捷键"));
    keysRow->addWidget(keyEdit, 1);
    auto *replace = new QPushButton(QStringLiteral("替换"), page);
    auto *append = new QPushButton(QStringLiteral("添加备用键"), page);
    auto *clear = new QPushButton(QStringLiteral("清除"), page);
    auto *reset = new QPushButton(QStringLiteral("恢复此项"), page);
    for (auto *button : {replace, append, clear, reset}) keysRow->addWidget(button);
    pageLayout->addLayout(keysRow);
    auto *message = new QLabel(QStringLiteral("选中功能后录入快捷键；保存后生效。文字编辑中的换行和取消键保持通用习惯。"), page);
    message->setObjectName("shortcutMessage");
    message->setWordWrap(true);
    pageLayout->addWidget(message);
    QObject::connect(table, &QTableWidget::currentCellChanged, &dialog, [&](int row, int, int, int) {
        if (row >= 0) keyEdit->setKeySequence(pending.value(entries.at(row).action).value(0));
    });
    const auto update = [&](int operation) {
        const int row = table->currentRow();
        if (row < 0) { message->setText(QStringLiteral("请先选择功能")); return; }
        auto *action = entries.at(row).action;
        auto next = pending;
        if (operation == 0) next[action] = keyEdit->keySequence().isEmpty() ? QList<QKeySequence>() : QList<QKeySequence>{keyEdit->keySequence()};
        if (operation == 1 && !keyEdit->keySequence().isEmpty()) next[action].append(keyEdit->keySequence());
        if (operation == 2) next[action].clear();
        if (operation == 3) next[action] = entries.at(row).defaults;
        QString error;
        if (!validate(next, &error)) { message->setText(error); return; }
        pending = next;
        refresh();
        message->setText(QStringLiteral("已暂存修改，点击“保存”生效"));
    };
    QObject::connect(replace, &QPushButton::clicked, &dialog, [&] { update(0); });
    QObject::connect(append, &QPushButton::clicked, &dialog, [&] { update(1); });
    QObject::connect(clear, &QPushButton::clicked, &dialog, [&] { update(2); });
    QObject::connect(reset, &QPushButton::clicked, &dialog, [&] { update(3); });
    tabs->addTab(page, QStringLiteral("枝理快捷键"));

    auto *reference = new QWidget(tabs);
    auto *referenceLayout = new QVBoxLayout(reference);
    auto *referenceSearch = new QLineEdit(reference);
    referenceSearch->setPlaceholderText(QStringLiteral("搜索 MindMaster 功能、快捷键或实现状态"));
    referenceSearch->setClearButtonEnabled(true);
    referenceLayout->addWidget(referenceSearch);
    auto *catalogue = new QTableWidget(reference);
    catalogue->setObjectName("mindmasterShortcutCatalogue");
    catalogue->setColumnCount(4);
    catalogue->setHorizontalHeaderLabels({QStringLiteral("类别"), QStringLiteral("功能"), QStringLiteral("MindMaster 按键"), QStringLiteral("枝理状态")});
    catalogue->setEditTriggers(QAbstractItemView::NoEditTriggers);
    catalogue->verticalHeader()->hide();
    catalogue->verticalHeader()->setDefaultSectionSize(qMax(34, dialog.fontMetrics().height() + 14));
    catalogue->setAlternatingRowColors(true);
    catalogue->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    catalogue->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    QFile source(QStringLiteral(":/help/mindmaster-shortcuts.tsv"));
    if (source.open(QIODevice::ReadOnly)) {
        const auto lines = QString::fromUtf8(source.readAll()).split('\n');
        for (const auto &line : lines) {
            const auto fields = line.trimmed().split('\t');
            if (fields.size() < 4 || line.startsWith('#')) continue;
            const int row = catalogue->rowCount();
            catalogue->insertRow(row);
            for (int col = 0; col < 4; ++col) {
                auto *cell = new QTableWidgetItem(fields.at(col));
                cell->setToolTip(fields.at(col));
                catalogue->setItem(row, col, cell);
            }
        }
    }
    QObject::connect(referenceSearch, &QLineEdit::textChanged, &dialog, [catalogue](const QString &text) {
        for (int row = 0; row < catalogue->rowCount(); ++row) {
            QString values;
            for (int col = 0; col < 4; ++col) values += catalogue->item(row, col)->text() + ' ';
            catalogue->setRowHidden(row, !values.contains(text, Qt::CaseInsensitive));
        }
    });
    referenceLayout->addWidget(catalogue, 1);
    auto *sourceLabel = new QLabel(QStringLiteral("依据官方 Windows 快捷键说明（2026-11-11 核对）。待实现项目仅供对照，不注册空命令。"), reference);
    sourceLabel->setWordWrap(true);
    referenceLayout->addWidget(sourceLabel);
    tabs->addTab(reference, QStringLiteral("MindMaster 完整对照"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    buttons->button(QDialogButtonBox::RestoreDefaults)->setText(QStringLiteral("全部恢复默认"));
    layout->addWidget(buttons);
    QObject::connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dialog, [&] {
        pending = defaults(); refresh();
        message->setText(QStringLiteral("已暂存默认方案，点击“保存”生效"));
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        QString error;
        if (apply(pending, &error)) dialog.accept();
        else { tabs->setCurrentIndex(0); message->setText(error); }
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(QSize(940, 620).boundedTo(parent->size()));
    if (!entries.isEmpty()) table->setCurrentCell(0, 0);
    dialog.exec();
}
