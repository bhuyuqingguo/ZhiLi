#include "startup.h"
#include <QtWidgets>
#include <QtMath>

namespace {
qreal phase(qreal time, qreal begin, qreal end) { return qBound(0.0, (time - begin) / (end - begin), 1.0); }
qreal ease(qreal value) { return 1 - qPow(1 - value, 3); }
void growingPath(QPainter &painter, const QPainterPath &path, qreal amount) {
    if (amount <= 0) return;
    QPainterPath visible(path.pointAtPercent(0));
    const int steps = 70;
    for (int i = 1; i <= steps; ++i) visible.lineTo(path.pointAtPercent(amount * i / steps));
    painter.drawPath(visible);
}
}

StartupOverlay::StartupOverlay(QWidget *parent, std::function<void()> finished)
    : QWidget(parent), completed(std::move(finished)) {
    setObjectName("startupOverlay");
    setAccessibleName(QStringLiteral("枝理启动动画，按 Esc 或单击跳过"));
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_DeleteOnClose);
    setGeometry(parent->rect());
    parent->installEventFilter(this);
    animation.setStartValue(0.0);
    animation.setEndValue(1.0);
    animation.setDuration(1650);
    connect(&animation, &QVariantAnimation::valueChanged, this, [this] { update(); });
    connect(&animation, &QVariantAnimation::finished, this, [this] { finish(); });
    show();
    raise();
    setFocus(Qt::OtherFocusReason);
    animation.start();
}

bool StartupOverlay::eventFilter(QObject *object, QEvent *event) {
    if (object == parentWidget() && event->type() == QEvent::Resize) setGeometry(parentWidget()->rect());
    return QWidget::eventFilter(object, event);
}

bool StartupOverlay::event(QEvent *event) {
    if (event->type() == QEvent::ShortcutOverride) { event->accept(); return true; }
    return QWidget::event(event);
}

void StartupOverlay::finish() {
    if (done) return;
    done = true;
    animation.stop();
    hide();
    if (completed) completed();
    close();
}

void StartupOverlay::mousePressEvent(QMouseEvent *event) { event->accept(); finish(); }
void StartupOverlay::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Space || event->key() == Qt::Key_Return) {
        event->accept(); finish();
    } else QWidget::keyPressEvent(event);
}

void StartupOverlay::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    paintFrame(painter, size(), animation.currentValue().toReal(), font());
}

void StartupOverlay::paintFrame(QPainter &painter, const QSize &size, qreal progress, const QFont &font) {
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const qreal opacity = 1 - ease(phase(progress, 0.84, 1));
    painter.setOpacity(opacity);
    QLinearGradient background(0, 0, size.width(), size.height());
    background.setColorAt(0, QColor("#f6f8f1"));
    background.setColorAt(0.6, QColor("#edf3e9"));
    background.setColorAt(1, QColor("#dfece2"));
    painter.fillRect(QRect(QPoint(), size), background);
    // Quiet concentric ripples suggest a thought expanding into branches.
    const QPointF center(size.width() * 0.5, size.height() * 0.43);
    for (int i = 0; i < 3; ++i) {
        QColor color("#779d88"); color.setAlpha(15 - i * 3);
        painter.setPen(QPen(color, 1)); painter.setBrush(Qt::NoBrush);
        const qreal radius = (150 + i * 100 + ease(progress) * 24) * qMin(1.0, size.width() / 900.0);
        painter.drawEllipse(center, radius, radius);
    }
    const qreal unit = qMin(size.width() / 960.0, size.height() / 720.0);
    painter.save();
    painter.translate(center);
    painter.scale(qBound(0.5, unit, 1.35), qBound(0.5, unit, 1.35));
    const qreal entry = ease(phase(progress, 0, 0.28));
    painter.translate(0, (1 - entry) * 15);
    painter.setOpacity(opacity * entry);
    // The three growing branches match the application's SVG mark.
    const qreal tileScale = 0.92 + entry * 0.08;
    painter.save();
    painter.scale(tileScale, tileScale);
    QLinearGradient green(-90, -90, 90, 90);
    green.setColorAt(0, QColor("#183f34")); green.setColorAt(1, QColor("#2b6854"));
    painter.setPen(QPen(QColor("#87b69b"), 1)); painter.setBrush(green);
    painter.drawRoundedRect(QRectF(-92, -92, 184, 184), 47, 47);
    const QVector<QPointF> ends{{39, -40}, {44, 0}, {39, 40}};
    for (int i = 0; i < ends.size(); ++i) {
        QPainterPath branch(QPointF(-35, 0));
        branch.lineTo(-10, 0);
        branch.cubicTo(17, 0, 9, ends.at(i).y(), ends.at(i).x(), ends.at(i).y());
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor("#a9d2bd"), 8, Qt::SolidLine, Qt::RoundCap));
        const qreal growth = ease(phase(progress, 0.08 + i * 0.045, 0.42 + i * 0.045));
        growingPath(painter, branch, growth);
        const qreal pop = phase(progress, 0.28 + i * 0.055, 0.52 + i * 0.055);
        const qreal spring = QEasingCurve(QEasingCurve::OutBack).valueForProgress(pop);
        painter.setPen(Qt::NoPen);
        painter.setBrush(i == 1 ? QColor("#d9a261") : QColor("#d6eadc"));
        painter.drawEllipse(ends.at(i), 11 * spring, 11 * spring);
    }
    painter.setBrush(QColor("#f0f6ef")); painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(-40, 0), 16, 16);
    painter.restore();
    const qreal title = ease(phase(progress, 0.18, 0.55));
    painter.setOpacity(opacity * title);
    painter.setPen(QColor("#204d3d"));
    QFont titleFont = font;
    titleFont.setPixelSize(46); titleFont.setBold(true);
    titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 8);
    painter.setFont(titleFont);
    painter.drawText(QRectF(-180, 116 + (1 - title) * 10, 360, 65), Qt::AlignCenter, QStringLiteral("枝理"));
    QFont subtitleFont = font; subtitleFont.setPixelSize(15);
    subtitleFont.setLetterSpacing(QFont::AbsoluteSpacing, 2);
    painter.setFont(subtitleFont); painter.setPen(QColor("#608170"));
    painter.drawText(QRectF(-260, 191, 520, 35), Qt::AlignCenter, QStringLiteral("让思路，自然舒展"));
    painter.restore();
    painter.setOpacity(opacity * 0.65);
    QFont footerFont = font; footerFont.setPixelSize(12); painter.setFont(footerFont);
    painter.setPen(QColor("#597665"));
    painter.drawText(QRect(24, size.height() - 52, size.width() - 48, 28), Qt::AlignCenter,
                     QStringLiteral("点击或按 Esc 跳过"));
}
