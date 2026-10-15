#include "ui/timelinewidget.h"

#include <QPainter>
#include <algorithm>

namespace jit::dash::ui {

namespace {
QString fmtDuration(qint64 sec)
{
    const qint64 h = sec / 3600, m = (sec % 3600) / 60;
    return h > 0 ? QString("%1h %2m").arg(h).arg(m) : QString("%1m").arg(m);
}
} // namespace

// ------------------------------------------------------------ TimelineStrip --

TimelineStrip::TimelineStrip(QWidget* parent) : QWidget(parent) { setMinimumHeight(56); }

void TimelineStrip::setSegments(const std::vector<TimelineSegment>& segs) { segs_ = segs; updateGeometry(); update(); }
void TimelineStrip::setZoom(double factor) { zoom_ = std::clamp(factor, 1.0, 8.0); updateGeometry(); update(); }

QSize TimelineStrip::sizeHint() const { return {static_cast<int>(600 * zoom_), 56}; }

void TimelineStrip::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF track(0, 8, width(), 32);
    p.fillRect(track, QColor("#0a1420"));

    for (const auto& s : segs_)
    {
        const QRectF r(track.left() + track.width() * s.startPct / 100.0, track.top(),
                       std::max(1.0, track.width() * s.widthPct / 100.0), track.height());
        p.fillRect(r, s.color);
    }

    // Vạch chia giờ (0h, 6h, 12h, 18h, 24h) - giống #timeline-visual-ruler.
    p.setPen(QColor("#4a6584"));
    for (int h = 0; h <= 24; h += 6)
    {
        const double x = track.left() + track.width() * h / 24.0;
        p.drawLine(QPointF(x, track.bottom()), QPointF(x, track.bottom() + 4));
        p.drawText(QRectF(x - 20, track.bottom() + 4, 40, 14), Qt::AlignHCenter, QString("%1:00").arg(h, 2, 10, QChar('0')));
    }
}

// ----------------------------------------------------------- TimelineLegend --

TimelineLegend::TimelineLegend(QWidget* parent) : QWidget(parent) { setMinimumHeight(1); }

void TimelineLegend::setEntries(const std::vector<LegendEntry>& entries) { entries_ = entries; updateGeometry(); update(); }

QSize TimelineLegend::sizeHint() const
{
    const int rows = (static_cast<int>(entries_.size()) + 2) / 3;
    return {200, std::max(1, rows) * 22 + 4};
}

void TimelineLegend::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setFont(QFont(font().family(), 9));

    const int colW = width() / 3;
    for (size_t i = 0; i < entries_.size(); i++)
    {
        const int col = static_cast<int>(i) % 3, row = static_cast<int>(i) / 3;
        const double x = col * colW, y = row * 22.0;

        p.setPen(Qt::NoPen);
        p.setBrush(entries_[i].color);
        p.drawRect(QRectF(x, y + 5, 10, 10));

        p.setPen(QColor("#dce8f7"));
        p.drawText(QRectF(x + 16, y, colW - 16, 20), Qt::AlignVCenter,
                   QString("%1 (%2)").arg(entries_[i].processName, fmtDuration(entries_[i].totalSeconds)));
    }
}

// ---------------------------------------------------------- DailyBarsWidget --

DailyBarsWidget::DailyBarsWidget(QWidget* parent) : QWidget(parent) { setMinimumHeight(140); }

void DailyBarsWidget::setBars(const std::vector<DailyBar>& bars) { bars_ = bars; update(); }

void DailyBarsWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (bars_.empty())
        return;

    qint64 maxVal = 1;
    for (const auto& b : bars_) maxVal = std::max(maxVal, b.totalSeconds);

    const double barAreaH = height() - 34.0;
    const double slot = width() / static_cast<double>(bars_.size());
    const double barW = std::min(48.0, slot * 0.6);

    for (size_t i = 0; i < bars_.size(); i++)
    {
        const double frac = static_cast<double>(bars_[i].totalSeconds) / static_cast<double>(maxVal);
        const double h = barAreaH * frac;
        const double x = slot * i + (slot - barW) / 2.0;

        p.fillRect(QRectF(x, barAreaH - h, barW, h), QColor("#5aa9ff"));

        p.setPen(QColor("#7e9ac0"));
        p.setFont(QFont(font().family(), 8));
        p.drawText(QRectF(x - 10, barAreaH + 2, barW + 20, 14), Qt::AlignHCenter, bars_[i].date.right(5)); // "MM-DD"
        p.drawText(QRectF(x - 10, barAreaH - h - 16, barW + 20, 14), Qt::AlignHCenter, fmtDuration(bars_[i].totalSeconds));
    }
}

} // namespace jit::dash::ui
