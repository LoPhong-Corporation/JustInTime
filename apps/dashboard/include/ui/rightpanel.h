//
// rightpanel.h
// "right side bar" trong wireframe layout: 1 danh sách nhỏ (Recent
// Alerts), 1 khối lớn (snapshot máy đang dùng: hostname/OS/CPU/RAM/Disk),
// 1 danh sách nhỏ nữa (Top Processes). LUÔN hiện bất kể đang ở khu vực
// nào của rail - trước đây 3 khối này nằm trong OverviewPage, đã tách ra
// đây để hiện xuyên suốt, không chỉ lúc xem tab Overview.
//
#ifndef UI_RIGHTPANEL_H
#define UI_RIGHTPANEL_H

#include "sysstats.h"

#include <QWidget>

class QLabel;
class QListWidget;
class QTableWidget;

namespace jit::dash::ui {

class RightPanel : public QWidget
{
public:
    explicit RightPanel(Collector* collector, QWidget* parent = nullptr);

    void tick();                 // mỗi ~1 giây - cập nhật snapshot + dò cảnh báo mới
    void refreshStaticInfoOnce(); // 1 lần: hostname/OS/processor (không đổi theo thời gian)
    void refreshProcesses();     // định kỳ (nhẹ hơn tick, gọi mỗi vài giây là đủ)
    void reloadThresholds();

private:
    Collector* collector_;
    QListWidget* alertsList_;
    QLabel* snapshotLabel_;
    QTableWidget* processTable_;

    QString machineInfoLine_; // "hostname / OS / processor" - ghép với % live mỗi tick()
    bool prevAlert_[3] = {false, false, false}; // cpu, ram, disk - phát hiện cạnh lên
    int cpuThreshold_ = 85;
    int ramThreshold_ = 85;
    int diskThreshold_ = 90;
};

} // namespace jit::dash::ui

#endif
