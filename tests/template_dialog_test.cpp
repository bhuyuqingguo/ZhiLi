#include "../src/template_dialog.h"
#include "../src/appearance.h"
#include <QtWidgets>
#include <QtTest/QTest>

int main(int argc, char **argv) {
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ZhiLi template dialog test"));
    registerEmbeddedFonts();
    const auto families = embeddedFontFamilies();
    const int fontPoints = qEnvironmentVariableIntValue("ZHILI_TEMPLATE_FONT_POINTS");
    if (!families.isEmpty()) application.setFont(QFont(families.first(), fontPoints ? fontPoints : 11));
    bool passed = true;
    const auto check = [&](bool condition, const char *what) {
        if (!condition) { qCritical() << what; passed = false; }
        return condition;
    };
    QTimer::singleShot(0, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!check(dialog, "template dialog visible")) return;
        auto *search = dialog->findChild<QLineEdit *>(QStringLiteral("templateSearch"));
        auto *category = dialog->findChild<QComboBox *>(QStringLiteral("templateCategory"));
        auto *list = dialog->findChild<QListWidget *>(QStringLiteral("templateList"));
        auto *preview = dialog->findChild<QGraphicsView *>(QStringLiteral("templatePreview"));
        auto *tree = dialog->findChild<QTreeWidget *>(QStringLiteral("templateOutline"));
        auto *create = dialog->findChild<QPushButton *>(QStringLiteral("templateCreateButton"));
        auto *cancel = dialog->findChild<QPushButton *>(QStringLiteral("templateCancelButton"));
        if (!check(search && category && list && preview && tree && create && cancel, "dialog controls present")) {
            dialog->reject(); return;
        }
        const QRect available = QApplication::primaryScreen()->availableGeometry();
        check(dialog->width() <= available.width() && dialog->height() <= available.height(),
              "dialog fits logical screen with selected font");
        const auto reachable = [dialog](QWidget *widget) {
            return dialog->rect().contains(QRect(widget->mapTo(dialog, QPoint()), widget->size()));
        };
        check(reachable(create) && reachable(cancel), "Create and Cancel stay reachable without scrolling");
        check(list->count() >= 80 && category->count() == 5, "complete catalog and four categories");
        check(create->isEnabled() && preview->property("nodeCount").toInt() >= 17, "initial preview valid");
        check(tree->topLevelItemCount() == 1 && tree->topLevelItem(0)->childCount() >= 4,
              "readable outline preview available");
        auto *toggle = dialog->findChild<QPushButton *>(QStringLiteral("templateTogglePreview"));
        if (toggle) {
            QTest::mouseClick(toggle, Qt::LeftButton);
            check(list->isHidden(), "compact preview hides list");
            check(reachable(create) && reachable(cancel), "compact preview keeps actions reachable");
            QTest::mouseClick(toggle, Qt::LeftButton);
            check(!list->isHidden(), "compact preview returns to list");
        }
        category->setCurrentIndex(category->findData(QStringLiteral("架构视图")));
        check(list->count() >= 20, "category filter");
        search->setText(QStringLiteral("离线 桌面"));
        check(list->count() == 1, "multi-term Chinese search");
        check(list->currentItem() && list->currentItem()->data(Qt::UserRole).toString()
              == QStringLiteral("architecture-offline-desktop"), "correct template selected");
        const QString screenshot = qEnvironmentVariable("ZHILI_TEMPLATE_SCREENSHOT");
        if (!screenshot.isEmpty()) {
            QTest::qWait(100);
            check(dialog->grab().save(screenshot), "save dialog screenshot");
            auto *tabs = dialog->findChild<QTabWidget *>(QStringLiteral("templatePreviewTabs"));
            if (toggle) QTest::mouseClick(toggle, Qt::LeftButton);
            if (tabs) tabs->setCurrentIndex(1);
            QTest::qWait(30);
            check(dialog->grab().save(screenshot + QStringLiteral(".outline.png")), "save outline screenshot");
            if (toggle) QTest::mouseClick(toggle, Qt::LeftButton);
        }
        search->setText(QStringLiteral("no-such-template-987654"));
        check(list->count() == 0 && !create->isEnabled(), "empty result disables creation");
        check(preview->property("nodeCount").toInt() == 0 && preview->scene()->items().isEmpty(),
              "empty result clears old preview");
        check(tree->topLevelItemCount() == 0, "empty result clears outline");
        QTest::keyClick(search, Qt::Key_Return);
        check(dialog->isVisible(), "enter with no selection cannot accept");
        search->clear();
        category->setCurrentIndex(0);
        search->setText(QStringLiteral("  ADR  "));
        check(list->count() == 1, "case-insensitive trimmed tag search");
        search->clear();
        check(list->count() >= 80 && create->isEnabled(), "filter reset restores catalog");
        QTest::mouseClick(cancel, Qt::LeftButton);
    });
    check(zhili::chooseBuiltInTemplate().isEmpty(), "Cancel returns no selection");

    QTimer::singleShot(0, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!check(dialog, "second dialog visible")) return;
        auto *search = dialog->findChild<QLineEdit *>(QStringLiteral("templateSearch"));
        auto *create = dialog->findChild<QPushButton *>(QStringLiteral("templateCreateButton"));
        if (!check(search && create, "second dialog controls")) { dialog->reject(); return; }
        search->setText(QStringLiteral("activity-workshop"));
        QTest::mouseClick(create, Qt::LeftButton);
    });
    check(zhili::chooseBuiltInTemplate() == QStringLiteral("activity-workshop"), "accept returns catalog ID");

    QTimer::singleShot(0, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!check(dialog, "third dialog visible")) return;
        QTest::keyClick(dialog, Qt::Key_Escape);
    });
    check(zhili::chooseBuiltInTemplate().isEmpty(), "Escape returns no selection");
    return passed ? 0 : 1;
}
