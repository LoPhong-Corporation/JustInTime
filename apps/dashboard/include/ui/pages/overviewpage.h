//
// overviewpage.h
// Tab "Overview" trong khu vực Home. Period picker + timeline hoạt động,
// 4 stat-card (CPU/RAM/Disk/Network) + sparkline, biểu đồ lịch sử hiệu
// năng, Quick Actions (Export/Print).
//
// Fleet status, danh sách Machines, Recent Alerts, Top Processes, thông
// tin máy đã CHUYỂN sang MachinesSidebar/RightPanel (xem
// include/ui/machinessidebar.h, include/ui/rightpanel.h) - hiện xuyên
// suốt mọi tab thay vì chỉ ở Overview, theo layout rail/sidebar/tabs/
// main/right-panel chuẩn.
//
#ifndef UI_PAGES_OVERVIEWPAGE_H
#define UI_PAGES_OVERVIEWPAGE_H

#include "dashconfig.h"
#include "localdb.h"
#include "sysstats.h"
#include "ui/chartwidget.h"
#include "ui/timelinewidget.h"

#include <QWidget>
#include <functional>

class QLabel;
class QPushButton;
class QScrollArea;

namespace jit::dash::ui {

class OverviewPage : public QWidget
{
public:
    struct Callbacks {
        std::function<void()> exportCsv, printReport;
    };

    OverviewPage(Config cfg, LocalDB* db, Collector* collector, Callbacks cb, QWidget* parent = nullptr);

    void tick();        // mỗi ~1 giây (chỉ khi tab này đang hiện - MainWindow tự quyết định khi nào gọi)
    void refreshAll();  // gọi 1 lần khi tab hiện lần đầu / period đổi

private:
    Config cfg_;
    LocalDB* db_;
    Collector* collector_;
    Callbacks cb_;

    QString currentPeriod_ = "today";
    QPushButton* periodButtons_[5];
    TimelineStrip* timelineStrip_;
    TimelineLegend* timelineLegend_;
    DailyBarsWidget* dailyBars_;
    QWidget* singleDayContainer_;
    QWidget* rangeContainer_;
    QScrollArea* timelineScroll_;
    double timelineZoom_ = 1.0;
    QLabel* timelineZoomLabel_;

    ChartWidget *sparkCpu_, *sparkRam_, *sparkDisk_, *sparkNet_;
    QLabel *cpuPercentLbl_, *ramPercentLbl_, *diskPercentLbl_, *netPercentLbl_;
    QLabel *cpuSubtitleLbl_, *ramSubtitleLbl_, *diskSubtitleLbl_, *netSubtitleLbl_;

    ChartWidget* historyChart_;

    void buildUi();
    void setPeriod(const QString& period);
    void loadPeriodAndTimeline();
    void applyLiveStats(const LiveStats& live);
};

} // namespace jit::dash::ui

#endif
