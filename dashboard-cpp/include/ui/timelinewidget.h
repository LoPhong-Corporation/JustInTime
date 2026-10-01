//
// timelinewidget.h
// Thay #timeline-visual-strip / #timeline-daily-bars trong index.html.
// 2 chế độ, khớp đúng logic dashboard.js:
//   - Single day: 1 thanh ngang 24h, mỗi activity là 1 đoạn màu theo app,
//     có thể zoom (kéo dài chiều ngang) - đây là lý do widget này cần
//     nằm trong QScrollArea khi nhúng vào page (xem overviewpage.cpp).
//   - Multi-day (tuần/tuỳ chọn): cột tổng theo từng ngày.
//
#ifndef UI_TIMELINEWIDGET_H
#define UI_TIMELINEWIDGET_H

#include <QWidget>
#include <QColor>
#include <QString>
#include <vector>

namespace jit::dash::ui {

struct TimelineSegment { QString processName, windowTitle; double startPct, widthPct; QColor color; };
struct LegendEntry { QString processName; QColor color; qint64 totalSeconds; };
struct DailyBar { QString date; qint64 totalSeconds; };

class TimelineStrip : public QWidget
{
public:
    explicit TimelineStrip(QWidget* parent = nullptr);
    void setSegments(const std::vector<TimelineSegment>& segs);
    void setZoom(double factor); // 1.0 = 100% (chiều rộng = viewport), 4.0 = 400%

protected:
    void paintEvent(QPaintEvent*) override;
    QSize sizeHint() const override;

private:
    std::vector<TimelineSegment> segs_;
    double zoom_ = 1.0;
};

// Dải chú giải màu (process_name + tổng thời gian) đặt dưới TimelineStrip.
class TimelineLegend : public QWidget
{
public:
    explicit TimelineLegend(QWidget* parent = nullptr);
    void setEntries(const std::vector<LegendEntry>& entries);

protected:
    void paintEvent(QPaintEvent*) override;
    QSize sizeHint() const override;

private:
    std::vector<LegendEntry> entries_;
};

// Cột tổng theo ngày - chế độ nhiều ngày (this_week/last_week/custom).
class DailyBarsWidget : public QWidget
{
public:
    explicit DailyBarsWidget(QWidget* parent = nullptr);
    void setBars(const std::vector<DailyBar>& bars);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    std::vector<DailyBar> bars_;
};

} // namespace jit::dash::ui

#endif
