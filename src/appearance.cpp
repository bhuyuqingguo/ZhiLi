#include "appearance.h"

#include <QtWidgets>
#include <algorithm>

namespace {
QStringList embeddedFamilies;

void addEmbeddedFont(const QString &file) {
    const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/") + file);
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    if (!families.isEmpty()) embeddedFamilies.append(families.first());
    else qWarning() << "Unable to load bundled font" << file;
}

FontChoice readChoice(QSettings &settings, const QString &group, FontChoice fallback, int minimum, int maximum) {
    settings.beginGroup(group);
    QString family = settings.value("family", fallback.family).toString();
    if (!family.isEmpty() && !QFontDatabase().hasFamily(family)) family = fallback.family;
    bool ok = false;
    const int points = settings.value("points", fallback.points).toInt(&ok);
    FontChoice choice{family, ok ? qBound(minimum, points, maximum) : fallback.points,
                      settings.value("bold", fallback.bold).toBool()};
    settings.endGroup();
    return choice;
}

void writeChoice(QSettings &settings, const QString &group, const FontChoice &choice) {
    settings.beginGroup(group);
    settings.setValue("family", choice.family);
    settings.setValue("points", choice.points);
    settings.setValue("bold", choice.bold);
    settings.endGroup();
}

struct FontControls {
    QComboBox *family;
    QSpinBox *size;
    QComboBox *weight;

    FontChoice choice() const {
        return {family->currentData().toString(), size->value(), weight->currentIndex() == 1};
    }

    void select(const FontChoice &choice) const {
        family->setCurrentIndex(qMax(0, family->findData(choice.family)));
        size->setValue(choice.points);
        weight->setCurrentIndex(choice.bold ? 1 : 0);
    }
};

FontControls addFontControls(QFormLayout *form, const FontChoice &choice, int minimum, int maximum) {
    FontControls fields{new QComboBox, new QSpinBox, new QComboBox};
    fields.family->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    fields.family->setMinimumContentsLength(9);
    fields.family->addItem(QStringLiteral("系统默认字体"), QString());
    for (const auto &name : embeddedFamilies) {
        const QString label = name.contains("Serif") ? QStringLiteral("思源宋体（内置）") : QStringLiteral("思源黑体（内置）");
        fields.family->addItem(label, name);
    }
    for (const auto &name : QFontDatabase().families()) {
        if (!embeddedFamilies.contains(name)) fields.family->addItem(name, name);
    }
    fields.size->setRange(minimum, maximum);
    fields.size->setSuffix(" pt");
    fields.weight->addItems({QStringLiteral("常规"), QStringLiteral("粗体")});
    fields.select(choice);
    form->addRow(QStringLiteral("字体"), fields.family);
    form->addRow(QStringLiteral("字号"), fields.size);
    form->addRow(QStringLiteral("字重"), fields.weight);
    return fields;
}
}

void registerEmbeddedFonts() {
    Q_INIT_RESOURCE(fonts);
    addEmbeddedFont("SourceHanSansCN-Regular.otf");
    addEmbeddedFont("SourceHanSerifCN-Regular.otf");
}

QStringList embeddedFontFamilies() {
    return embeddedFamilies;
}

QFont fontFor(const FontChoice &choice) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    if (!choice.family.isEmpty()) font.setFamily(choice.family);
    font.setPointSize(choice.points);
    font.setBold(choice.bold);
    return font;
}

Appearance Appearance::defaults() {
    const QString family = embeddedFamilies.value(0);
    return {{family, 11, false}, {family, 11, false}, false, false};
}

Appearance Appearance::load() {
    QSettings settings;
    settings.beginGroup("appearance");
    const Appearance fallback = defaults();
    return {readChoice(settings, "interface", fallback.interfaceFont, 9, 22),
            readChoice(settings, "topics", fallback.topicFont, 9, 32),
            settings.value("highContrast", false).toBool(),
            settings.value("dimensionalSymbols", false).toBool()};
}

void Appearance::save() const {
    QSettings settings;
    settings.beginGroup("appearance");
    writeChoice(settings, "interface", interfaceFont);
    writeChoice(settings, "topics", topicFont);
    settings.setValue("highContrast", highContrast);
    settings.setValue("dimensionalSymbols", dimensionalSymbols);
}

bool editAppearance(QWidget *parent, Appearance *appearance) {
    QDialog dialog(parent);
    dialog.setObjectName("appearanceDialog");
    dialog.setWindowTitle(QStringLiteral("外观设置"));
    auto *outer = new QVBoxLayout(&dialog);
    auto *scroller = new QScrollArea(&dialog);
    scroller->setWidgetResizable(true);
    scroller->setFrameShape(QFrame::NoFrame);
    auto *contents = new QWidget(scroller);
    auto *layout = new QVBoxLayout(contents);
    auto *theme = new QComboBox;
    theme->setObjectName("appearanceTheme");
    theme->addItems({QStringLiteral("枝理浅色"), QStringLiteral("高对比")});
    theme->setCurrentIndex(appearance->highContrast ? 1 : 0);
    auto *interfaceBox = new QGroupBox(QStringLiteral("应用界面"));
    auto *interfaceForm = new QFormLayout(interfaceBox);
    interfaceForm->addRow(QStringLiteral("主题"), theme);
    const auto ui = addFontControls(interfaceForm, appearance->interfaceFont, 9, 22);
    auto *topicBox = new QGroupBox(QStringLiteral("导图主题"));
    auto *topicForm = new QFormLayout(topicBox);
    const auto topic = addFontControls(topicForm, appearance->topicFont, 9, 32);
    auto *symbols = new QComboBox;
    symbols->setObjectName("symbolStyle");
    symbols->addItems({QStringLiteral("简约 2D"), QStringLiteral("立体拟物")});
    symbols->setCurrentIndex(appearance->dimensionalSymbols ? 1 : 0);
    topicForm->addRow(QStringLiteral("图标风格"), symbols);
    ui.family->setObjectName("interfaceFamily");
    ui.size->setObjectName("interfaceSize");
    topic.family->setObjectName("topicFamily");
    topic.size->setObjectName("topicSize");
    topic.weight->setObjectName("topicWeight");
    auto *preview = new QLabel(QStringLiteral("万象成枝，脉络见理 · Zhili 1.0"));
    preview->setWordWrap(true);
    topicForm->addRow(preview);
    auto *uiPreview = new QLabel(QStringLiteral("文件　编辑　视图"));
    uiPreview->setWordWrap(true);
    interfaceForm->addRow(uiPreview);
    const auto update = [=] {
        preview->setFont(fontFor(topic.choice()));
        uiPreview->setFont(fontFor(ui.choice()));
    };
    for (auto fields : {ui, topic}) {
        QObject::connect(fields.family, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, update);
        QObject::connect(fields.size, QOverload<int>::of(&QSpinBox::valueChanged), &dialog, update);
        QObject::connect(fields.weight, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, update);
    }
    update();
    layout->addWidget(interfaceBox);
    layout->addWidget(topicBox);
    layout->addStretch();
    scroller->setWidget(contents);
    outer->addWidget(scroller);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("应用"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    auto *reset = buttons->addButton(QStringLiteral("恢复默认"), QDialogButtonBox::ResetRole);
    QObject::connect(reset, &QPushButton::clicked, &dialog, [=] {
        const Appearance original = Appearance::defaults();
        ui.select(original.interfaceFont);
        topic.select(original.topicFont);
        theme->setCurrentIndex(0);
        symbols->setCurrentIndex(0);
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    outer->addWidget(buttons);
    dialog.resize(qMin(450, parent->width()), qMin(560, parent->height()));
    if (dialog.exec() != QDialog::Accepted) return false;
    *appearance = {ui.choice(), topic.choice(), theme->currentIndex() == 1, symbols->currentIndex() == 1};
    return true;
}

bool editTopicStyle(QWidget *parent, const TopicStyle &current, int selectionCount, TopicStyleChange *change) {
    QDialog dialog(parent);
    dialog.setObjectName("topicStyleDialog");
    dialog.setWindowTitle(QStringLiteral("主题样式"));
    dialog.setStyleSheet(QStringLiteral(
        "QDialog#topicStyleDialog,QWidget#topicStyleFields,QScrollArea {background:#f6f8f5;color:#203d36}"
        "QLabel#styleHeading {color:#225b49;font-weight:bold}"
        "QComboBox,QSpinBox,QLineEdit {background:white;border:1px solid #d2e2da;border-radius:6px;padding:5px 8px}"
        "QComboBox:disabled,QSpinBox:disabled,QLineEdit:disabled {background:#edf2ee;color:#657970}"
        "QComboBox::drop-down {border:0;width:24px}"
        "QPushButton {background:white;color:#225b49;border:1px solid #c5d8ce;border-radius:6px;padding:6px 13px}"
        "QPushButton:hover {background:#e2eee7}"
        "QPushButton#applyTopicStyle {background:#225b49;color:white;border-color:#225b49}"
        "QPushButton#applyTopicStyle:hover {background:#2e7059}"
        "QPushButton:disabled,QPushButton#applyTopicStyle:disabled {background:#e8eeea;color:#647870;border-color:#d2e2da}"));
    auto *outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(16, 14, 16, 14);
    outer->setSpacing(10);
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    auto *contents = new QWidget;
    contents->setObjectName("topicStyleFields");
    auto *form = new QFormLayout(contents);
    form->setContentsMargins(2, 2, 5, 2);
    form->setVerticalSpacing(8);
    auto *heading = new QLabel(selectionCount > 1 ? QStringLiteral("批量编辑主题") : QStringLiteral("主题样式"));
    heading->setObjectName("styleHeading");
    form->addRow(heading);
    auto *intro = new QLabel(QStringLiteral("已选 %1 个主题。勾选要修改的参数。").arg(selectionCount));
    intro->setWordWrap(true);
    form->addRow(intro);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    auto *apply = buttons->button(QDialogButtonBox::Apply);
    apply->setObjectName("applyTopicStyle");
    apply->setText(QStringLiteral("应用"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    QList<QCheckBox *> fields;
    const auto row = [&](const QString &label, const char *name, QWidget *editor) {
        auto *box = new QCheckBox(label);
        box->setObjectName(QString::fromLatin1(name));
        fields.append(box);
        editor->setEnabled(false);
        QObject::connect(box, &QCheckBox::toggled, editor, &QWidget::setEnabled);
        form->addRow(box, editor);
        return box;
    };

    auto *alignment = new QComboBox;
    alignment->setObjectName("topicStyleAlignment");
    alignment->addItems({QStringLiteral("居中"), QStringLiteral("左对齐"), QStringLiteral("右对齐")});
    alignment->setCurrentIndex(static_cast<int>(current.alignment));
    const auto *changeAlignment = row(QStringLiteral("文字对齐"), "applyAlignment", alignment);

    auto *family = new QComboBox;
    family->addItem(QStringLiteral("跟随全局字体"));
    family->addItems(QFontDatabase().families());
    if (!current.family.isEmpty()) {
        if (family->findText(current.family) == -1) family->addItem(current.family);
        family->setCurrentIndex(family->findText(current.family));
    }
    family->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    family->setMinimumContentsLength(12);
    const auto *changeFamily = row(QStringLiteral("字体"), "applyFamily", family);

    auto *points = new QSpinBox;
    points->setObjectName("topicStylePoints");
    points->setRange(8, 48);
    points->setSpecialValueText(QStringLiteral("跟随全局"));
    points->setSuffix(" pt");
    points->setValue(current.points ? current.points : 8);
    const auto *changePoints = row(QStringLiteral("字号"), "applyPoints", points);

    auto *weight = new QComboBox;
    weight->addItems({QStringLiteral("跟随默认"), QStringLiteral("常规"), QStringLiteral("加粗")});
    weight->setCurrentIndex(current.weight + 1);
    const auto *changeWeight = row(QStringLiteral("字重"), "applyWeight", weight);

    auto *colorControl = new QWidget;
    auto *colorRow = new QHBoxLayout(colorControl);
    colorRow->setContentsMargins(0, 0, 0, 0);
    auto *color = new QLineEdit(current.color);
    color->setMaxLength(7);
    color->setPlaceholderText(QStringLiteral("留空跟随主题色"));
    color->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("(#[0-9a-fA-F]{6})?")), color));
    colorRow->addWidget(color, 1);
    auto *pick = new QPushButton(QStringLiteral("选色…"));
    colorRow->addWidget(pick);
    QObject::connect(pick, &QPushButton::clicked, &dialog, [&] {
        const QColor initial(color->text());
        const auto chosen = QColorDialog::getColor(initial.isValid() ? initial : QColor("#203d36"), &dialog);
        if (chosen.isValid()) color->setText(chosen.name());
    });
    const auto *changeColor = row(QStringLiteral("文字颜色"), "applyColor", colorControl);

    auto *descendants = new QCheckBox(QStringLiteral("同时应用到所选主题的子树"));
    form->addRow(descendants);
    const auto validate = [&] {
        const bool any = std::any_of(fields.cbegin(), fields.cend(), [](const QCheckBox *field) {
            return field->isChecked();
        });
        apply->setEnabled(any && (!changeColor->isChecked() || color->hasAcceptableInput()));
    };
    for (const auto *field : fields) QObject::connect(field, &QCheckBox::toggled, &dialog, validate);
    QObject::connect(color, &QLineEdit::textChanged, &dialog, validate);
    QObject::connect(apply, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    validate();
    scroll->setWidget(contents);
    outer->addWidget(scroll);
    outer->addWidget(buttons);
    dialog.resize(qMin(490, parent->width()), qMin(416, parent->height()));
    if (dialog.exec() != QDialog::Accepted) return false;
    change->style.alignment = static_cast<TextAlignment>(alignment->currentIndex());
    change->style.family = family->currentIndex() > 0 ? family->currentText() : QString();
    change->style.points = points->value() == 8 ? 0 : points->value();
    change->style.weight = weight->currentIndex() - 1;
    change->style.color = color->text().toLower();
    change->alignment = changeAlignment->isChecked();
    change->family = changeFamily->isChecked();
    change->points = changePoints->isChecked();
    change->weight = changeWeight->isChecked();
    change->color = changeColor->isChecked();
    change->descendants = descendants->isChecked();
    return true;
}
