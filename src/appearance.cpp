#include "appearance.h"

#include <QtWidgets>

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
    return {{family, 11, false}, {family, 11, false}, false};
}

Appearance Appearance::load() {
    QSettings settings;
    settings.beginGroup("appearance");
    const Appearance fallback = defaults();
    return {readChoice(settings, "interface", fallback.interfaceFont, 9, 22),
            readChoice(settings, "topics", fallback.topicFont, 9, 32),
            settings.value("highContrast", false).toBool()};
}

void Appearance::save() const {
    QSettings settings;
    settings.beginGroup("appearance");
    writeChoice(settings, "interface", interfaceFont);
    writeChoice(settings, "topics", topicFont);
    settings.setValue("highContrast", highContrast);
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
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    outer->addWidget(buttons);
    dialog.resize(qMin(450, parent->width()), qMin(560, parent->height()));
    if (dialog.exec() != QDialog::Accepted) return false;
    *appearance = {ui.choice(), topic.choice(), theme->currentIndex() == 1};
    return true;
}
