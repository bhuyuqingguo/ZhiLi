#include "../src/symbols.h"
#include "../src/symbolpicker.h"

#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QFontDatabase>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QScreen>
#include <QSlider>
#include <QSvgRenderer>
#include <QTest>

#define CHECK(condition) do { if (!(condition)) { qCritical() << __LINE__ << #condition; return 1; } } while (false)

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const QString reviewDir = qEnvironmentVariable("ZHILI_SYMBOL_REVIEW_DIR");
    if (!reviewDir.isEmpty()) {
        CHECK(QDir().mkpath(reviewDir));
        const QString fontFile = qEnvironmentVariable("ZHILI_SYMBOL_FONT_FILE");
        if (!fontFile.isEmpty()) {
            const int fontId = QFontDatabase::addApplicationFont(fontFile);
            CHECK(fontId >= 0);
            app.setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).first(), 11));
        }
    }
    CHECK(SymbolLibrary::loadError().isEmpty());
    CHECK(SymbolLibrary::assets().size() == 128);
    CHECK(SymbolLibrary::categories().size() == 8);
    CHECK(SymbolLibrary::search().size() == 128);
    CHECK(SymbolLibrary::search("", "nonexistent").isEmpty());
    CHECK(!SymbolLibrary::find(""));
    CHECK(!SymbolLibrary::find("unknown-future-symbol"));
    const auto *first = SymbolLibrary::find("air-quadcopter");
    CHECK(first && first->name == QStringLiteral("四旋翼平台"));
    CHECK(first == SymbolLibrary::find("air-quadcopter"));
    CHECK(SymbolLibrary::search(QStringLiteral("四旋翼")).size() == 1);
    CHECK(SymbolLibrary::search("  QUADCOPTER \t").size() == 1);
    CHECK(SymbolLibrary::search(QStringLiteral("空中 多旋翼")).size() == 3);
    CHECK(SymbolLibrary::search("AUV", "underwater").size() == 1);
    CHECK(SymbolLibrary::search("AUV", "air").isEmpty());
    CHECK(SymbolLibrary::search(QStringLiteral("通讯")).size() == 16);
    CHECK(SymbolLibrary::search(QStringLiteral("不存在的素材")).isEmpty());

    if (!reviewDir.isEmpty()) {
        QImage sheet(1536, 8 * 290 + 100, QImage::Format_ARGB32_Premultiplied);
        sheet.fill(QColor("#f3f5f9"));
        QPainter painter(&sheet);
        QFont font = app.font();
        font.setPixelSize(28);
        painter.setFont(font);
        painter.setPen(QColor("#24334b"));
        painter.drawText(QRect(24, 16, 1450, 42), QStringLiteral("枝理 · 128 个原创离线素材"));
        font.setPixelSize(14);
        painter.setFont(font);
        painter.drawText(QRect(24, 60, 1450, 28), QStringLiteral("8 类 × 16 个 · 矢量图形 · CC0-1.0 · 通用示意，不代表技术或运行参数"));
        int categoryRow = 0;
        for (const SymbolCategory &category : SymbolLibrary::categories()) {
            const int top = 100 + categoryRow++ * 290;
            font.setPixelSize(22);
            painter.setFont(font);
            painter.setPen(QColor("#24334b"));
            painter.drawText(QRect(24, top, 1400, 38), category.name);
            int index = 0;
            for (const SymbolAsset &asset : SymbolLibrary::search("", category.id)) {
                const int x = (index % 8) * 192 + 8;
                const int y = top + 44 + (index / 8) * 116;
                index++;
                painter.setPen(QColor("#dce2ed"));
                painter.setBrush(Qt::white);
                painter.drawRoundedRect(QRect(x, y, 176, 108), 7, 7);
                QSvgRenderer renderer(asset.resourcePath);
                renderer.render(&painter, QRectF(x + 62, y + 6, 52, 52));
                painter.setPen(QColor("#24334b"));
                font.setPixelSize(15);
                painter.setFont(font);
                painter.drawText(QRect(x + 3, y + 61, 170, 23), Qt::AlignCenter, asset.name);
                font.setPixelSize(10);
                painter.setFont(font);
                painter.setPen(QColor("#667287"));
                painter.drawText(QRect(x + 3, y + 85, 170, 18), Qt::AlignCenter, asset.englishName);
            }
        }
        painter.end();
        CHECK(sheet.save(reviewDir + "/symbol-contact-sheet.png"));
    }

    QSet<QString> ids;
    QSet<QByteArray> rendered;
    QMap<QString, int> categoryCounts;
    for (const SymbolAsset &asset : SymbolLibrary::assets()) {
        CHECK(!ids.contains(asset.id));
        ids.insert(asset.id);
        categoryCounts[asset.categoryId]++;
        CHECK(!asset.name.isEmpty() && !asset.englishName.isEmpty());
        QFile file(asset.resourcePath);
        CHECK(file.open(QIODevice::ReadOnly));
        const QByteArray svg = file.readAll();
        CHECK(!svg.contains("<script") && !svg.contains("<image") && !svg.contains("<text"));
        CHECK(!svg.contains("href=") && !svg.contains("url("));
        QSvgRenderer renderer(svg);
        CHECK(renderer.isValid());
        CHECK(renderer.viewBox() == QRect(0, 0, 64, 64));
        for (int size : {24, 32, 64, 128}) {
            QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            renderer.render(&painter);
            painter.end();
            int painted = 0;
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                    if (qAlpha(image.pixel(x, y)) > 20) painted++;
            CHECK(painted > size * size / 12);
            CHECK(painted < size * size * 9 / 10);
            // Nothing touches the SVG boundary, so icons cannot be clipped.
            for (int i = 0; i < size; ++i) {
                CHECK(qAlpha(image.pixel(0, i)) == 0);
                CHECK(qAlpha(image.pixel(size - 1, i)) == 0);
                CHECK(qAlpha(image.pixel(i, 0)) == 0);
                CHECK(qAlpha(image.pixel(i, size - 1)) == 0);
            }
            if (size == 64) {
                const QByteArray hash = QCryptographicHash::hash(
                    QByteArray(reinterpret_cast<const char *>(image.constBits()), image.bytesPerLine() * image.height()),
                    QCryptographicHash::Sha256);
                CHECK(!rendered.contains(hash));
                rendered.insert(hash);
            }
        }
    }
    for (const SymbolCategory &category : SymbolLibrary::categories()) {
        CHECK(categoryCounts.value(category.id) == 16);
        CHECK(SymbolLibrary::search("", category.id).size() == 16);
    }

    SymbolPickerDialog picker(nullptr, QStringLiteral("air-quadcopter"));
    picker.show();
    app.processEvents();
    if (!reviewDir.isEmpty()) CHECK(picker.grab().save(reviewDir + "/symbol-picker.png"));
    auto *search = picker.findChild<QLineEdit *>("symbolSearch");
    auto *category = picker.findChild<QComboBox *>("symbolCategory");
    auto *list = picker.findChild<QListWidget *>("symbolList");
    auto *apply = picker.findChild<QPushButton *>("symbolApply");
    auto *size = picker.findChild<QSlider *>("symbolSize");
    CHECK(search && category && list && apply && size);
    CHECK(list->count() == 128 && list->currentItem());
    CHECK(list->currentItem()->data(Qt::UserRole).toString() == "air-quadcopter");
    CHECK(apply->isEnabled());
    for (int i = 0; i < list->count(); ++i)
        CHECK(!list->item(i)->icon().pixmap(32, 32).isNull());
    search->setText("AUV");
    CHECK(list->count() == 1 && !apply->isEnabled());
    list->setCurrentRow(0);
    CHECK(apply->isEnabled());
    search->setText("no matching symbol");
    CHECK(list->count() == 0 && !apply->isEnabled());
    CHECK(picker.findChild<QLabel *>("symbolDescription")->text().contains(QStringLiteral("没有匹配")));
    // Clearing search restores the remembered selection without applying it.
    search->clear();
    CHECK(list->currentItem()->data(Qt::UserRole).toString() == "underwater-auv");
    CHECK(picker.selectedSymbolId() == "air-quadcopter");
    category->setCurrentIndex(category->findData("ground"));
    CHECK(list->count() == 16 && !apply->isEnabled());
    list->setCurrentRow(0);
    size->setValue(64);
    CHECK(list->iconSize() == QSize(64, 64));
    picker.resize(1800, 900);
    app.processEvents();
    CHECK(list->viewport()->width() > 1500);
    CHECK(list->gridSize().width() < 400);
    const QString chosen = list->currentItem()->data(Qt::UserRole).toString();
    apply->click();
    CHECK(picker.result() == QDialog::Accepted && picker.selectedSymbolId() == chosen);

    SymbolPickerDialog canceled(nullptr, "air-quadcopter");
    canceled.show();
    canceled.findChild<QListWidget *>("symbolList")->setCurrentRow(9);
    QTest::keyClick(&canceled, Qt::Key_Escape);
    CHECK(canceled.result() == QDialog::Rejected && canceled.selectedSymbolId() == "air-quadcopter");

    SymbolPickerDialog removed(nullptr, "air-quadcopter");
    removed.findChild<QPushButton *>("symbolRemove")->click();
    CHECK(removed.result() == QDialog::Accepted && removed.selectedSymbolId().isEmpty());

    SymbolPickerDialog blank;
    CHECK(!blank.findChild<QPushButton *>("symbolRemove")->isEnabled());
    CHECK(!blank.findChild<QPushButton *>("symbolApply")->isEnabled());
    blank.show();
    QTest::keyClick(blank.findChild<QLineEdit *>("symbolSearch"), Qt::Key_Return);
    CHECK(blank.isVisible());
    blank.close();
    CHECK(blank.result() == QDialog::Rejected);

    SymbolPickerDialog future(nullptr, "future-unrecognized-symbol");
    CHECK(!future.findChild<QPushButton *>("symbolApply")->isEnabled());
    future.reject();
    CHECK(future.selectedSymbolId() == "future-unrecognized-symbol");
    // Actual dialog geometry, not just a requested resize: large fonts and
    // 200% scaling used to force the dialog beyond a 400x300 logical screen.
    const QFont originalFont = app.font();
    for (int points : {11, 22}) {
        QFont font = originalFont;
        font.setPointSize(points);
        app.setFont(font);
        SymbolPickerDialog responsive(nullptr, "air-quadcopter");
        responsive.show();
        app.processEvents();
        const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
        CHECK(responsive.frameGeometry().width() <= screen.width());
        CHECK(responsive.frameGeometry().height() <= screen.height());
        const auto checkActions = [&]() {
            QList<QRect> buttonRects;
            for (const char *name : {"symbolApply", "symbolCancel", "symbolRemove"}) {
                auto *button = responsive.findChild<QPushButton *>(name);
                if (!button || !button->isVisible() || !button->isEnabled()) return false;
                const QRect rect(button->mapTo(&responsive, QPoint(0, 0)), button->size());
                if (!responsive.rect().contains(rect) || button->visibleRegion().isEmpty()) return false;
                if (rect.height() < button->fontMetrics().height()) return false;
                for (const QRect &other : buttonRects) if (rect.intersects(other)) return false;
                buttonRects.append(rect);
            }
            return true;
        };
        CHECK(checkActions());
        if (!reviewDir.isEmpty())
            CHECK(responsive.grab().save(reviewDir + QString("/symbol-picker-screen-%1pt.png").arg(points)));
        // Exercise expand-shrink-expand, not just compact construction.
        for (const QSize &requested : {QSize(400, 300), QSize(360, 270), QSize(1000, 800), QSize(400, 300)}) {
            responsive.resize(requested);
            app.processEvents();
            CHECK(responsive.size() == requested);
            CHECK(checkActions());
            auto *results = responsive.findChild<QListWidget *>("symbolList");
            CHECK(results->viewport()->height() >= results->fontMetrics().height() + 20);
            if (requested.height() <= 300) {
                CHECK(results->viewMode() == QListView::ListMode);
                CHECK(!responsive.findChild<QLabel *>("symbolPreview")->isVisible());
                CHECK(!responsive.findChild<QSlider *>("symbolSize")->isVisible());
            } else {
                CHECK(results->viewMode() == QListView::IconMode);
                CHECK(responsive.findChild<QLabel *>("symbolPreview")->isVisible());
            }
        }
        if (!reviewDir.isEmpty())
            CHECK(responsive.grab().save(reviewDir + QString("/symbol-picker-400x300-%1pt.png").arg(points)));
        QTest::mouseClick(responsive.findChild<QPushButton *>("symbolApply"), Qt::LeftButton);
        CHECK(responsive.result() == QDialog::Accepted && responsive.selectedSymbolId() == "air-quadcopter");
    }
    app.setFont(originalFont);
    qInfo() << "128 unique SVGs rendered at 24/32/64/128 px; 8 categories; search and picker flows passed.";
    return 0;
}
