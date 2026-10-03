//
// metricpages.h
// Thay #view-cpu / #view-ram / #view-disk / #view-network trong index.html.
// 4 lớp nhỏ, mỗi lớp: gauge (trừ Network) + thông tin + phần riêng (lưới
// per-core / bảng phân vùng / bảng card mạng) + biểu đồ lịch sử. Dùng
// chung sysstats::Collector với OverviewPage (không gọi WinAPI lần 2).
//
#ifndef UI_PAGES_METRICPAGES_H
#define UI_PAGES_METRICPAGES_H

#include "sysstats.h"
#include "ui/chartwidget.h"
#include "ui/gaugewidget.h"

#include <QWidget>

class QLabel;
class QGridLayout;
class QTableWidget;

namespace jit::dash::ui {

class CpuPage : public QWidget
{
public:
    explicit CpuPage(Collector* collector, QWidget* parent = nullptr);
    void refreshStaticInfo(); // gọi 1 lần khi trang hiện lần đầu (đọc MachineInfo, không đổi theo thời gian)
    void tick();              // mỗi ~1 giây

private:
    Collector* collector_;
    GaugeWidget* gauge_;
    QLabel* info_;
    QGridLayout* coreGrid_;
    std::vector<QLabel*> coreBars_;
    ChartWidget* history_;
    bool staticLoaded_ = false;
};

class RamPage : public QWidget
{
public:
    explicit RamPage(Collector* collector, QWidget* parent = nullptr);
    void tick();

private:
    Collector* collector_;
    GaugeWidget* gauge_;
    QLabel* info_;
    ChartWidget* history_; // 2 đường: RAM %, Swap %
};

class DiskPage : public QWidget
{
public:
    explicit DiskPage(Collector* collector, QWidget* parent = nullptr);
    void tick();

private:
    Collector* collector_;
    QLabel* info_;
    QTableWidget* partitionsTable_;
    ChartWidget* history_; // 2 đường: read Bps, write Bps (thang tự co)
};

class NetworkPage : public QWidget
{
public:
    explicit NetworkPage(Collector* collector, QWidget* parent = nullptr);
    void refreshInterfacesOnce();
    void tick();

private:
    Collector* collector_;
    QLabel* speedLabel_;
    QTableWidget* interfacesTable_;
    ChartWidget* history_; // 2 đường: up Bps, down Bps
    bool interfacesLoaded_ = false;
};

} // namespace jit::dash::ui

#endif
