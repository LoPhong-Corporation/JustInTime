#include "ui/chartwidget.h"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace jit::dash::ui {

ChartWidget::ChartWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(60);
}

void ChartWidget::setMaxPoints(int n) { maxPoints_ = std::max(2, n); }
void ChartWidget::setSparklineMode(bool on) { sparkline_ = on; update(); }
void ChartWidget::setFixedMax(double max) { fixedMax_ = max; update(); }

void ChartWidget::addSeries(const QString& name, const QColor& color)
{
    series_.push_back({name, color, {}});
}

void ChartWidget::pushValue(int seriesIndex, double value)
{
    if (seriesIndex < 0 || seriesIndex >= static_cast<int>(series_.size()))
        return;

    auto& v = series_[static_cast<size_t>(seriesIndex)].values;
    v.push_back(value);
    while (static_cast<int>(v.size()) > maxPoints_)
        v.pop_front();

    update();
}

void ChartWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#000000"));

    const int legendH = (!sparkline_ && !series_.empty()) ? 20 : 0;
    const QRectF plot(sparkline_ ? 1 : 36, 4, width() - (sparkline_ ? 2 : 44), height() - legendH - (sparkline_ ? 4 : 20));

    if (plot.width() <= 0 || plot.height() <= 0)
        return;

    // Trục Y: cận trên cố định (vd 100%) hoặc tự co theo giá trị lớn nhất đang có.
    double maxVal = fixedMax_ > 0 ? fixedMax_ : 1.0;
    if (fixedMax_ <= 0)
        for (const auto& s : series_)
            for (double v : s.values)
                maxVal = std::max(maxVal, v);

    if (!sparkline_)
    {
        p.setPen(QColor("#1c3a5e"));
        for (int i = 0; i <= 4; i++)
        {
            const double y = plot.top() + plot.height() * i / 4.0;
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            p.setPen(QColor("#4a6584"));
            // 1 chữ số thập phân nếu cận trên nhỏ (vd tốc độ đĩa tính theo
            // MB/s) để 4 mốc chia không bị làm tròn trùng nhau thành "2 1 1 0 0".
            const int decimals = maxVal < 10 ? 1 : 0;
            p.drawText(QRectF(0, y - 8, plot.left() - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(maxVal * (4 - i) / 4.0, 'f', decimals));
            p.setPen(QColor("#1c3a5e"));
        }
    }

    for (const auto& s : series_)
    {
        if (s.values.size() < 2)
            continue;

        QPainterPath path;
        const int n = static_cast<int>(s.values.size());
        for (int i = 0; i < n; i++)
        {
            const double x = plot.left() + plot.width() * i / static_cast<double>(maxPoints_ - 1);
            const double frac = maxVal > 0 ? std::clamp(s.values[static_cast<size_t>(i)] / maxVal, 0.0, 1.0) : 0.0;
            const double y = plot.bottom() - plot.height() * frac;
            if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }

        p.setPen(QPen(s.color, sparkline_ ? 1.5 : 2.0));
        p.drawPath(path);

        if (sparkline_)
        {
            // Vùng tô nhẹ dưới đường - giống "fill" của Chart.js gốc, giúp
            // sparkline dễ đọc ở kích thước rất nhỏ.
            QPainterPath fillPath = path;
            fillPath.lineTo(plot.right(), plot.bottom());
            fillPath.lineTo(plot.left(), plot.bottom());
            fillPath.closeSubpath();
            QColor fill = s.color; fill.setAlpha(40);
            p.fillPath(fillPath, fill);
        }
    }

    if (legendH > 0)
    {
        double x = plot.left();
        p.setFont(QFont(font().family(), 9));
        for (const auto& s : series_)
        {
            p.setPen(s.color);
            p.drawRect(QRectF(x, height() - legendH + 6, 8, 8));
            p.drawText(QRectF(x + 12, height() - legendH, 120, legendH), Qt::AlignVCenter, s.name);
            x += 12 + p.fontMetrics().horizontalAdvance(s.name) + 20;
        }
    }
}

} // namespace jit::dash::ui
