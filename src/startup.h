#pragma once
#include <QWidget>
#include <QVariantAnimation>
#include <functional>

class StartupOverlay : public QWidget {
public:
    explicit StartupOverlay(QWidget *parent, std::function<void()> finished = {});
    void finish();
    static void paintFrame(QPainter &painter, const QSize &size, qreal progress, const QFont &font);
protected:
    bool event(QEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
private:
    QVariantAnimation animation;
    std::function<void()> completed;
    bool done = false;
};
