//
// overviewpage.h
// Thay #view-overview trong index.html: fleet status, period picker +
// timeline hoạt động, danh sách machines, 4 stat-card (CPU/RAM/Disk/Net) +
// sparkline, biểu đồ lịch sử hiệu năng, Recent Alerts, Top Processes,
// Agent status, Quick Actions, thông tin máy/ổ đĩa.
//
// KHÔNG có: AI Insights + Chatbot (2 khối cuối #view-local trong bản gốc
// thực ra nằm ở trang Local, không phải Overview - và bị bỏ vì aiinsights
// chưa cổng hoá, xem localpage.h).
//
#ifndef UI_PAGES_OVERVIEWPAGE_H
#define UI_PAGES_OVERVIEWPAGE_H

#include "dashconfig.h"
#include "localdb.h"
#include "sysstats.h"
#include "ui/chartwidget.h"
#include "ui/gaugewidget.h"
#include "ui/timelinewidget.h"

#include <QWidget>
#include <functional>

class QLabel;
class QTableWidget;
class QPushButton;
class QListWidget;
class QScrollArea;

namespace jit::dash::ui {

class OverviewPage : public QWidget
{
public:
    // callbacks: điều hướng sang trang khác từ nút Quick Action (Family/Devices/Settings).
    struct Callbacks {
        std::function<void()> openFamily, openDevices, openSettings, exportCsv, printReport;
    };

    OverviewPage(Config cfg, LocalDB* db, Collector* collector, Callbacks cb, QWidget* parent = nullptr);

    // Gọi mỗi ~1 giây bởi MainWindow (dùng chung 1 QTimer cho mọi trang cần live update).
    void tick();

    // Gọi 1 lần sau khi trang đã hiện lần đầu (period/machines/processes - có
    // gọi mạng nên phải qua runAsync, và runAsync không được gọi trước
    // app.exec() - xem asyncutil.h).
    void refreshAll();

private:
    Config cfg_;
    LocalDB* db_; // có thể nullptr (chưa có DB agent) - các phần liên quan tự ẩn
    Collector* collector_;
    Callbacks cb_;

    // Fleet
    QLabel *fleetOnline_, *fleetOffline_, *fleetWarning_, *fleetCritical_;

    // Period + timeline
    QString currentPeriod_ = "today";
    QPushButton* periodButtons_[5]; // today, yesterday, this_week, last_week, custom
    TimelineStrip* timelineStrip_;
    TimelineLegend* timelineLegend_;
    DailyBarsWidget* dailyBars_;
    QWidget* singleDayContainer_;
    QWidget* rangeContainer_;
    QScrollArea* timelineScroll_;
    double timelineZoom_ = 1.0;
    QLabel* timelineZoomLabel_;

    // Machines
    QListWidget* machinesList_;

    // Stat cards
    ChartWidget *sparkCpu_, *sparkRam_, *sparkDisk_, *sparkNet_;
    QLabel *cpuPercentLbl_, *ramPercentLbl_, *diskPercentLbl_, *netPercentLbl_;
    QLabel *cpuSubtitleLbl_, *ramSubtitleLbl_, *diskSubtitleLbl_, *netSubtitleLbl_;

    // History chart (multi-series: CPU/RAM/Disk %)
    ChartWidget* historyChart_;

    // Alerts
    QListWidget* alertsList_;
    bool prevAlert_[3] = {false, false, false}; // cpu, ram, disk - phát hiện cạnh lên để ghi log alert

    // Processes
    QTableWidget* processTable_;

    // Machine info
    QLabel *machineInfoLeft_, *machineInfoDisks_;
    bool machineInfoLoaded_ = false;

    void buildUi();
    void setPeriod(const QString& period);
    void loadPeriodAndTimeline();
    void loadMachines();
    void loadProcesses();
    void loadMachineInfoOnce();
    void applyLiveStats(const LiveStats& live);
};

} // namespace jit::dash::ui

#endif
