#include "ui/pages/metricpages.h"
#include "ui/asyncutil.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace jit::dash::ui {

namespace {

QString fmtBytesPerSec(double bps)
{
    if (bps >= 1024.0 * 1024.0) return QString::number(bps / (1024.0 * 1024.0), 'f', 1) + " MB/s";
    if (bps >= 1024.0) return QString::number(bps / 1024.0, 'f', 1) + " KB/s";
    return QString::number(bps, 'f', 0) + " B/s";
}

QLabel* infoLabel()
{
    auto* l = new QLabel;
    l->setStyleSheet("color:#dce8f7;");
    l->setTextFormat(Qt::RichText);
    return l;
}

} // namespace

// -------------------------------------------------------------------- CPU --

CpuPage::CpuPage(Collector* collector, QWidget* parent) : QWidget(parent), collector_(collector)
{
    auto* root = new QVBoxLayout(this);
    auto* top = new QHBoxLayout;
    gauge_ = new GaugeWidget; gauge_->setThreshold(85);
    info_ = infoLabel();
    top->addWidget(gauge_);
    top->addWidget(info_, 1);
    root->addLayout(top);

    auto* coreLbl = new QLabel("Per-core usage");
    coreLbl->setStyleSheet("color:#eef4fb; font-weight:600; margin-top:8px;");
    root->addWidget(coreLbl);

    auto* coreContainer = new QWidget;
    coreGrid_ = new QGridLayout(coreContainer);
    root->addWidget(coreContainer);

    auto* historyLbl = new QLabel("History");
    historyLbl->setStyleSheet("color:#eef4fb; font-weight:600; margin-top:8px;");
    root->addWidget(historyLbl);
    history_ = new ChartWidget;
    history_->addSeries("CPU %", QColor("#5aa9ff"));
    history_->setFixedMax(100);
    history_->setFixedHeight(160);
    root->addWidget(history_);
    root->addStretch();
}

void CpuPage::refreshStaticInfo()
{
    if (staticLoaded_) return;
    staticLoaded_ = true;

    runAsync(this, []() { return getMachineInfo(); },
        [this](MachineInfo* m, const std::exception* e) {
            if (e) { info_->setText("Failed to load: " + QString::fromStdString(e->what())); return; }

            info_->setText(QString("<b>%1</b><br>%2 physical cores, %3 logical<br>Frequency: %4")
                .arg(QString::fromStdString(m->processor)).arg(m->cpuCoresPhysical).arg(m->cpuCoresLogical)
                .arg(m->cpuFreqMHz ? QString("%1 MHz").arg(*m->cpuFreqMHz) : "N/A"));

            // Lưới per-core: xoá bar cũ (nếu refreshStaticInfo lỡ gọi lại) rồi tạo mới đúng số lõi thật.
            for (auto* b : coreBars_) b->deleteLater();
            coreBars_.clear();

            for (int i = 0; i < m->cpuCoresLogical; i++)
            {
                auto* bar = new QLabel("0%");
                bar->setAlignment(Qt::AlignCenter);
                bar->setStyleSheet("background:#0f1c2e; border:1px solid #1c3a5e; border-radius:4px; color:#dce8f7; padding:4px;");
                coreGrid_->addWidget(bar, i / 8, i % 8);
                coreBars_.push_back(bar);
            }
        });
}

void CpuPage::tick()
{
    const LiveStats s = collector_->live();
    gauge_->setValue(s.cpuPercent);
    history_->pushValue(0, s.cpuPercent);

    for (size_t i = 0; i < coreBars_.size() && i < s.cpuPerCore.size(); i++)
    {
        const double v = s.cpuPerCore[i];
        coreBars_[i]->setText(QString::number(v, 'f', 0) + "%");
        coreBars_[i]->setStyleSheet(QString("background:%1; border:1px solid #1c3a5e; border-radius:4px; color:#eef4fb; padding:4px;")
            .arg(v >= 85 ? "#5a1f1f" : "#0f1c2e"));
    }
}

// -------------------------------------------------------------------- RAM --

RamPage::RamPage(Collector* collector, QWidget* parent) : QWidget(parent), collector_(collector)
{
    auto* root = new QVBoxLayout(this);
    auto* top = new QHBoxLayout;
    gauge_ = new GaugeWidget; gauge_->setThreshold(85);
    info_ = infoLabel();
    top->addWidget(gauge_);
    top->addWidget(info_, 1);
    root->addLayout(top);

    auto* historyLbl = new QLabel("History");
    historyLbl->setStyleSheet("color:#eef4fb; font-weight:600; margin-top:8px;");
    root->addWidget(historyLbl);
    history_ = new ChartWidget;
    history_->addSeries("RAM %", QColor("#22c55e"));
    history_->addSeries("Swap %", QColor("#a78bfa"));
    history_->setFixedMax(100);
    history_->setFixedHeight(160);
    root->addWidget(history_);
    root->addStretch();
}

void RamPage::tick()
{
    const LiveStats s = collector_->live();
    gauge_->setValue(s.ramPercent);
    info_->setText(QString("Used: %1 GB<br>Available: %2 GB<br>Total: %3 GB<br>Swap: %4 / %5 GB")
        .arg(s.ramUsedGB, 0, 'f', 1).arg(s.ramAvailableGB, 0, 'f', 1).arg(s.ramTotalGB, 0, 'f', 1)
        .arg(s.swapUsedGB, 0, 'f', 1).arg(s.swapTotalGB, 0, 'f', 1));
    history_->pushValue(0, s.ramPercent);
    history_->pushValue(1, s.swapPercent);
}

// ------------------------------------------------------------------- Disk --

DiskPage::DiskPage(Collector* collector, QWidget* parent) : QWidget(parent), collector_(collector)
{
    auto* root = new QVBoxLayout(this);
    info_ = infoLabel();
    root->addWidget(info_);

    partitionsTable_ = new QTableWidget(0, 3);
    partitionsTable_->setHorizontalHeaderLabels({"Drive", "Used %", "Used / Total"});
    partitionsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    partitionsTable_->verticalHeader()->hide();
    partitionsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(partitionsTable_);

    auto* historyLbl = new QLabel("Read / Write throughput");
    historyLbl->setStyleSheet("color:#eef4fb; font-weight:600; margin-top:8px;");
    root->addWidget(historyLbl);
    history_ = new ChartWidget;
    history_->addSeries("Read", QColor("#5aa9ff"));
    history_->addSeries("Write", QColor("#f5a524"));
    history_->setFixedHeight(160);
    root->addWidget(history_);
    root->addStretch();
}

void DiskPage::tick()
{
    const LiveStats s = collector_->live();
    info_->setText(QString("Primary drive: %1% used (%2 / %3 GB)")
        .arg(s.diskPercent, 0, 'f', 0).arg(s.diskUsedGB, 0, 'f', 0).arg(s.diskTotalGB, 0, 'f', 0));

    partitionsTable_->setRowCount(static_cast<int>(s.disks.size()));
    for (int i = 0; i < static_cast<int>(s.disks.size()); i++)
    {
        const auto& d = s.disks[static_cast<size_t>(i)];
        partitionsTable_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(d.mountpoint)));
        partitionsTable_->setItem(i, 1, new QTableWidgetItem(QString::number(d.percent, 'f', 0)));
        partitionsTable_->setItem(i, 2, new QTableWidgetItem(
            QString("%1 / %2 GB").arg(d.usedGB, 0, 'f', 0).arg(d.totalGB, 0, 'f', 0)));
    }

    // Thang tự co (không setFixedMax) vì tốc độ đĩa có thể vượt xa 100 (MB/s).
    history_->pushValue(0, s.diskReadBps / 1024.0 / 1024.0);
    history_->pushValue(1, s.diskWriteBps / 1024.0 / 1024.0);
}

// ---------------------------------------------------------------- Network --

NetworkPage::NetworkPage(Collector* collector, QWidget* parent) : QWidget(parent), collector_(collector)
{
    auto* root = new QVBoxLayout(this);
    speedLabel_ = infoLabel();
    root->addWidget(speedLabel_);

    interfacesTable_ = new QTableWidget(0, 3);
    interfacesTable_->setHorizontalHeaderLabels({"Interface", "IPv4", "Status"});
    interfacesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    interfacesTable_->verticalHeader()->hide();
    interfacesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(interfacesTable_);

    auto* historyLbl = new QLabel("Upload / Download throughput");
    historyLbl->setStyleSheet("color:#eef4fb; font-weight:600; margin-top:8px;");
    root->addWidget(historyLbl);
    history_ = new ChartWidget;
    history_->addSeries("Upload", QColor("#5aa9ff"));
    history_->addSeries("Download", QColor("#22c55e"));
    history_->setFixedHeight(160);
    root->addWidget(history_);
    root->addStretch();
}

void NetworkPage::refreshInterfacesOnce()
{
    if (interfacesLoaded_) return;
    interfacesLoaded_ = true;

    runAsync(this, []() { return getNetworkInterfaces(); },
        [this](std::vector<NetInterface>* ifaces, const std::exception* e) {
            if (e) return;
            interfacesTable_->setRowCount(static_cast<int>(ifaces->size()));
            for (int i = 0; i < static_cast<int>(ifaces->size()); i++)
            {
                const auto& n = (*ifaces)[static_cast<size_t>(i)];
                interfacesTable_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(n.name)));
                interfacesTable_->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(n.ipv4)));
                interfacesTable_->setItem(i, 2, new QTableWidgetItem(n.isUp ? "Up" : "Down"));
            }
        });
}

void NetworkPage::tick()
{
    const LiveStats s = collector_->live();
    speedLabel_->setText(QString("Upload: %1 &nbsp;&nbsp; Download: %2")
        .arg(fmtBytesPerSec(s.netUploadBps), fmtBytesPerSec(s.netDownloadBps)));
    history_->pushValue(0, s.netUploadBps / 1024.0);
    history_->pushValue(1, s.netDownloadBps / 1024.0);
}

} // namespace jit::dash::ui
