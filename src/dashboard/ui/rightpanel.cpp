#include "ui/rightpanel.h"
#include "ui/asyncutil.h"
#include "dashsettings.h"

#include <QDateTime>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace jit::dash::ui {

RightPanel::RightPanel(Collector* collector, QWidget* parent) : QWidget(parent), collector_(collector)
{
    setFixedWidth(240);
    setObjectName("rightPanel");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 10, 8, 10);
    layout->setSpacing(10);

    auto* alertsLbl = new QLabel("Recent Alerts");
    alertsLbl->setObjectName("panelHeading");
    layout->addWidget(alertsLbl);
    alertsList_ = new QListWidget;
    alertsList_->setFixedHeight(130);
    alertsList_->setWordWrap(true);
    alertsList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(alertsList_);

    auto* snapshotLbl = new QLabel("This Machine");
    snapshotLbl->setObjectName("panelHeading");
    layout->addWidget(snapshotLbl);
    snapshotLabel_ = new QLabel("Loading...");
    snapshotLabel_->setObjectName("snapshotCard");
    snapshotLabel_->setWordWrap(true);
    snapshotLabel_->setMinimumHeight(160);
    snapshotLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(snapshotLabel_);

    auto* procLbl = new QLabel("Top Processes");
    procLbl->setObjectName("panelHeading");
    layout->addWidget(procLbl);
    processTable_ = new QTableWidget(0, 2);
    processTable_->setHorizontalHeaderLabels({"Process", "CPU"});
    processTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    processTable_->verticalHeader()->hide();
    processTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    processTable_->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(processTable_, 1);

    reloadThresholds();
}

void RightPanel::reloadThresholds()
{
    const DashSettings settings = loadDashSettings();
    cpuThreshold_ = settings.cpuThreshold;
    ramThreshold_ = settings.ramThreshold;
    diskThreshold_ = settings.diskThreshold;
}

void RightPanel::refreshStaticInfoOnce()
{
    runAsync(this, []() { return getMachineInfo(); },
        [this](MachineInfo* m, const std::exception* e) {
            if (e) { machineInfoLine_ = "Failed to load machine info."; return; }
            machineInfoLine_ = QString("<b>%1</b><br>%2<br>%3<br>%4 cores")
                .arg(QString::fromStdString(m->hostname), QString::fromStdString(m->os),
                     QString::fromStdString(m->processor)).arg(m->cpuCoresLogical);
        });
}

void RightPanel::refreshProcesses()
{
    runAsync(this, []() { return getProcesses(8); },
        [this](std::vector<ProcessInfo>* procs, const std::exception* e) {
            if (e) return;
            processTable_->setRowCount(static_cast<int>(procs->size()));
            for (int i = 0; i < static_cast<int>(procs->size()); i++)
            {
                const auto& p = (*procs)[static_cast<size_t>(i)];
                processTable_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(p.name)));
                processTable_->setItem(i, 1, new QTableWidgetItem(QString::number(p.cpuPercent, 'f', 0) + "%"));
            }
        });
}

void RightPanel::tick()
{
    const LiveStats s = collector_->live();

    snapshotLabel_->setText(QString("%1<br><br>CPU: <b>%2%</b><br>RAM: <b>%3%</b> (%4 GB)<br>Disk: <b>%5%</b>")
        .arg(machineInfoLine_.isEmpty() ? "..." : machineInfoLine_)
        .arg(s.cpuPercent, 0, 'f', 0).arg(s.ramPercent, 0, 'f', 0).arg(s.ramUsedGB, 0, 'f', 1).arg(s.diskPercent, 0, 'f', 0));

    const bool alerts[3] = {
        s.cpuPercent >= cpuThreshold_,
        s.ramPercent >= ramThreshold_,
        s.diskPercent >= diskThreshold_
    };
    const char* names[3] = {"CPU", "RAM", "Disk"};
    for (int i = 0; i < 3; i++)
    {
        if (alerts[i] && !prevAlert_[i])
        {
            alertsList_->insertItem(0, QString("[%1] %2 usage crossed alert threshold")
                .arg(QDateTime::currentDateTime().toString("HH:mm:ss"), names[i]));
            while (alertsList_->count() > 20)
                delete alertsList_->takeItem(alertsList_->count() - 1);
        }
        prevAlert_[i] = alerts[i];
    }
}

} // namespace jit::dash::ui
