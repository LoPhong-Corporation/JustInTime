#include "ui/gaugewidget.h"

#include <QPainter>
#include <algorithm>

namespace jit::dash::ui {

GaugeWidget::GaugeWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(160, 90);
}

void GaugeWidget::setValue(double percent) { value_ = std::clamp(percent, 0.0, 100.0); update(); }
void GaugeWidget::setThreshold(double percent) { threshold_ = percent; update(); }

void GaugeWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int w = width(), h = height();
    const int side = std::min(w, h * 2);
    const QRectF arcRect((w - side) / 2.0 + 6, h - side / 2.0 - 6, side - 12, side - 12);

    // Cung nền (135°..405°, tức 3/4 vòng tròn) - khớp thẩm mỹ "radial gauge"
    // của bản JS gốc (không phải nửa vòng tròn đơn giản).
    constexpr double startAngle = 225 * 16, spanTotal = -270 * 16;

    p.setPen(QPen(QColor("#1c3a5e"), 12, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(arcRect, static_cast<int>(startAngle), static_cast<int>(spanTotal));

    const QColor valueColor = value_ >= threshold_ ? QColor("#ef4444") : QColor("#5aa9ff");
    p.setPen(QPen(valueColor, 12, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(arcRect, static_cast<int>(startAngle), static_cast<int>(spanTotal * value_ / 100.0));

    p.setPen(QColor("#eef4fb"));
    QFont f = font(); f.setPointSize(18); f.setBold(true);
    p.setFont(f);
    p.drawText(rect(), Qt::AlignCenter, QString::number(value_, 'f', 0) + "%");
}

} // namespace jit::dash::ui
