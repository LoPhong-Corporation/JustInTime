#include "ui/pages/overviewpage.h"
#include "ui/asyncutil.h"
#include "cloud.h"
#include "dashsession.h"
#include "dashsettings.h"

#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>

namespace jit::dash::ui {

namespace {

QString fmtDuration(qint64 sec)
{
    const qint64 h = sec / 3600, m = (sec % 3600) / 60;
    return h > 0 ? QString("%1h %2m").arg(h).arg(m) : QString("%1m").arg(m);
}

QString fmtBytesPerSec(double bps)
{
    if (bps >= 1024.0 * 1024.0) return QString::number(bps / (1024.0 * 1024.0), 'f', 1) + " MB/s";
    if (bps >= 1024.0) return QString::number(bps / 1024.0, 'f', 1) + " KB/s";
    return QString::number(bps, 'f', 0) + " B/s";
}

QWidget* statCard(const QString& title, ChartWidget** sparkOut, QLabel** percentOut, QLabel** subtitleOut)
{
    auto* card = new QWidget;
    card->setObjectName("statCard");
    card->setStyleSheet("#statCard { background:#0f1c2e; border:1px solid #1c3a5e; border-radius:8px; }");
    auto* layout = new QVBoxLayout(card);

    auto* titleLbl = new QLabel(title);
    titleLbl->setStyleSheet("color:#7e9ac0; font-size:11px; font-weight:600;");
    layout->addWidget(titleLbl);

    auto* percentLbl = new QLabel("--%");
    percentLbl->setStyleSheet("color:#eef4fb; font-size:26px; font-weight:700;");
    layout->addWidget(percentLbl);

    auto* spark = new ChartWidget;
    spark->addSeries(title, QColor("#5aa9ff"));
    spark->setSparklineMode(true);
    spark->setFixedMax(100);
    spark->setFixedHeight(36);
    layout->addWidget(spark);

    auto* subtitleLbl = new QLabel("--");
    subtitleLbl->setStyleSheet("color:#7e9ac0; font-size:11px;");
    layout->addWidget(subtitleLbl);

    *sparkOut = spark; *percentOut = percentLbl; *subtitleOut = subtitleLbl;
    return card;
}

} // namespace

OverviewPage::OverviewPage(Config cfg, LocalDB* db, Collector* collector, Callbacks cb, QWidget* parent)
    : QWidget(parent), cfg_(std::move(cfg)), db_(db), collector_(collector), cb_(std::move(cb))
{
    buildUi();
}

void OverviewPage::buildUi()
{
    auto* root = new QVBoxLayout(this);

    // ---------------------------------------------------------- Fleet status --
    auto* fleetRow = new QHBoxLayout;
    auto mkFleet = [&](const QString& label, const char* color) {
        auto* l = new QLabel(label + ": 0");
        l->setStyleSheet(QString("color:%1; font-weight:600; padding:4px 10px; background:#0f1c2e; border-radius:6px;").arg(color));
        fleetRow->addWidget(l);
        return l;
    };
    fleetOnline_ = mkFleet("Online", "#22c55e");
    fleetWarning_ = mkFleet("Warning", "#f5a524");
    fleetCritical_ = mkFleet("Critical", "#ef4444");
    fleetOffline_ = mkFleet("Offline", "#7e9ac0");
    fleetRow->addStretch();
    root->addLayout(fleetRow);

    // ------------------------------------------------------- Period + timeline --
    auto* periodRow = new QHBoxLayout;
    const QString labels[5] = {"Today", "Yesterday", "This Week", "Last Week", "Custom"};
    const QString keys[5] = {"today", "yesterday", "this_week", "last_week", "custom"};
    for (int i = 0; i < 5; i++)
    {
        auto* btn = new QPushButton(labels[i]);
        btn->setCheckable(true);
        btn->setMinimumWidth(btn->fontMetrics().horizontalAdvance(labels[i]) + 36); // tránh chữ bị cắt (nút checked in đậm hơn - rộng hơn lúc đo)
        periodButtons_[i] = btn;
        periodRow->addWidget(btn);
        connect(btn, &QPushButton::clicked, this, [this, k = keys[i]] { setPeriod(k); });
    }
    periodButtons_[0]->setChecked(true);
    periodRow->addStretch();
    timelineZoomLabel_ = new QLabel("Zoom: 100%");
    auto* zoomOut = new QPushButton("-"), *zoomIn = new QPushButton("+");
    zoomOut->setFixedWidth(28); zoomIn->setFixedWidth(28);
    connect(zoomOut, &QPushButton::clicked, this, [this] {
        timelineZoom_ = std::max(1.0, timelineZoom_ - 0.5);
        timelineStrip_->setZoom(timelineZoom_);
        timelineZoomLabel_->setText(QString("Zoom: %1%").arg(static_cast<int>(timelineZoom_ * 100)));
    });
    connect(zoomIn, &QPushButton::clicked, this, [this] {
        timelineZoom_ = std::min(8.0, timelineZoom_ + 0.5);
        timelineStrip_->setZoom(timelineZoom_);
        timelineZoomLabel_->setText(QString("Zoom: %1%").arg(static_cast<int>(timelineZoom_ * 100)));
    });
    periodRow->addWidget(timelineZoomLabel_);
    periodRow->addWidget(zoomOut);
    periodRow->addWidget(zoomIn);
    root->addLayout(periodRow);

    singleDayContainer_ = new QWidget;
    auto* singleDayLayout = new QVBoxLayout(singleDayContainer_);
    timelineStrip_ = new TimelineStrip;
    timelineScroll_ = new QScrollArea;
    timelineScroll_->setWidget(timelineStrip_);
    timelineScroll_->setWidgetResizable(false);
    timelineScroll_->setFixedHeight(80);
    timelineScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    timelineScroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    singleDayLayout->addWidget(timelineScroll_);
    timelineLegend_ = new TimelineLegend;
    singleDayLayout->addWidget(timelineLegend_);

    rangeContainer_ = new QWidget;
    auto* rangeLayout = new QVBoxLayout(rangeContainer_);
    dailyBars_ = new DailyBarsWidget;
    rangeLayout->addWidget(dailyBars_);
    rangeContainer_->hide();

    root->addWidget(singleDayContainer_);
    root->addWidget(rangeContainer_);

    // -------------------------------------------------------------- Machines --
    auto* machinesLbl = new QLabel("Machines");
    machinesLbl->setStyleSheet("color:#eef4fb; font-weight:600; margin-top:8px;");
    root->addWidget(machinesLbl);
    machinesList_ = new QListWidget;
    machinesList_->setFixedHeight(90);
    root->addWidget(machinesList_);

    // ------------------------------------------------------------ Stat cards --
    auto* cardsGrid = new QGridLayout;
    cardsGrid->addWidget(statCard("CPU", &sparkCpu_, &cpuPercentLbl_, &cpuSubtitleLbl_), 0, 0);
    cardsGrid->addWidget(statCard("RAM", &sparkRam_, &ramPercentLbl_, &ramSubtitleLbl_), 0, 1);
    cardsGrid->addWidget(statCard("Disk", &sparkDisk_, &diskPercentLbl_, &diskSubtitleLbl_), 0, 2);
    cardsGrid->addWidget(statCard("Network", &sparkNet_, &netPercentLbl_, &netSubtitleLbl_), 0, 3);
    root->addLayout(cardsGrid);

    // ----------------------------------------------------------- History chart --
    auto* historyLbl = new QLabel("System Performance History");
    historyLbl->setStyleSheet("color:#eef4fb; font-weight:600; margin-top:8px;");
    root->addWidget(historyLbl);
    historyChart_ = new ChartWidget;
    historyChart_->addSeries("CPU %", QColor("#5aa9ff"));
    historyChart_->addSeries("RAM %", QColor("#22c55e"));
    historyChart_->addSeries("Disk %", QColor("#f5a524"));
    historyChart_->setFixedMax(100);
    historyChart_->setFixedHeight(160);
    root->addWidget(historyChart_);

    // --------------------------------------------------- Alerts + Processes --
    auto* midRow = new QHBoxLayout;

    auto* alertsCol = new QVBoxLayout;
    auto* alertsLbl = new QLabel("Recent Alerts");
    alertsLbl->setStyleSheet("color:#eef4fb; font-weight:600;");
    alertsCol->addWidget(alertsLbl);
    alertsList_ = new QListWidget;
    alertsCol->addWidget(alertsList_);
    midRow->addLayout(alertsCol, 1);

    auto* procCol = new QVBoxLayout;
    auto* procLbl = new QLabel("Top Processes");
    procLbl->setStyleSheet("color:#eef4fb; font-weight:600;");
    procCol->addWidget(procLbl);
    processTable_ = new QTableWidget(0, 3);
    processTable_->setHorizontalHeaderLabels({"Process", "CPU %", "Memory %"});
    processTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    processTable_->verticalHeader()->hide();
    processTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    procCol->addWidget(processTable_);
    midRow->addLayout(procCol, 1);

    root->addLayout(midRow);

    // ---------------------------------------------------------- Machine info --
    auto* infoRow = new QHBoxLayout;
    machineInfoLeft_ = new QLabel("Loading...");
    machineInfoLeft_->setStyleSheet("color:#dce8f7;");
    machineInfoDisks_ = new QLabel;
    machineInfoDisks_->setStyleSheet("color:#dce8f7;");
    infoRow->addWidget(machineInfoLeft_, 1);
    infoRow->addWidget(machineInfoDisks_, 1);
    root->addLayout(infoRow);

    // ------------------------------------------------------------ Quick actions --
    auto* actionsRow = new QHBoxLayout;
    auto mkAction = [&](const QString& label, std::function<void()> fn) {
        auto* btn = new QPushButton(label);
        connect(btn, &QPushButton::clicked, this, [fn] { if (fn) fn(); });
        actionsRow->addWidget(btn);
    };
    mkAction("Family", cb_.openFamily);
    mkAction("Devices", cb_.openDevices);
    mkAction("Settings", cb_.openSettings);
    mkAction("Export CSV", cb_.exportCsv);
    mkAction("Print Report", cb_.printReport);
    actionsRow->addStretch();
    root->addLayout(actionsRow);

    root->addStretch();
}

void OverviewPage::setPeriod(const QString& period)
{
    currentPeriod_ = period;
    const QString keys[5] = {"today", "yesterday", "this_week", "last_week", "custom"};
    for (int i = 0; i < 5; i++)
        periodButtons_[i]->setChecked(keys[i] == period);
    loadPeriodAndTimeline();
}

void OverviewPage::refreshAll()
{
    loadPeriodAndTimeline();
    loadMachines();
    loadProcesses();
    loadMachineInfoOnce();
}

void OverviewPage::loadMachineInfoOnce()
{
    if (machineInfoLoaded_)
        return;
    machineInfoLoaded_ = true;

    runAsync(this, []() { return getMachineInfo(); },
        [this](MachineInfo* m, const std::exception* e) {
            if (e) { machineInfoLeft_->setText("Failed to load: " + QString::fromStdString(e->what())); return; }

            machineInfoLeft_->setText(QString(
                "<b>%1</b><br>%2 (%3)<br>%4<br>%5 cores (%6 logical)<br>RAM: %7 GB")
                .arg(QString::fromStdString(m->hostname), QString::fromStdString(m->os),
                     QString::fromStdString(m->osVersion), QString::fromStdString(m->processor))
                .arg(m->cpuCoresPhysical).arg(m->cpuCoresLogical)
                .arg(m->ramTotalGB, 0, 'f', 1));

            QString disks = "<b>Disks</b><br>";
            for (const auto& d : m->disks)
                disks += QString("%1 (%2): %3 GB<br>").arg(QString::fromStdString(d.mountpoint),
                                                            QString::fromStdString(d.fstype)).arg(d.totalGB, 0, 'f', 1);
            machineInfoDisks_->setText(disks);
        });
}

void OverviewPage::loadMachines()
{
    if (!loadDashSession())
    {
        machinesList_->clear();
        machinesList_->addItem("Log in to see your machines.");
        fleetOnline_->setText("Online: -"); fleetOffline_->setText("Offline: -");
        fleetWarning_->setText("Warning: -"); fleetCritical_->setText("Critical: -");
        return;
    }

    const std::string token = loadDashSession()->accessToken;
    const DashSettings ds = loadDashSettings();

    runAsync(this,
        [token]() { return cloud::Client(token).listHeartbeats(); },
        [this, ds](std::vector<cloud::Client::DeviceHeartbeat>* hb, const std::exception* e) {
            machinesList_->clear();
            if (e) { machinesList_->addItem("Failed to load machines: " + QString::fromStdString(e->what())); return; }

            int online = 0, warning = 0, critical = 0, offline = 0;
            const QDateTime now = QDateTime::currentDateTimeUtc();

            for (const auto& m : *hb)
            {
                const QDateTime lastSeen = QDateTime::fromString(QString::fromStdString(m.lastSeen), Qt::ISODate);
                const bool isOffline = !lastSeen.isValid() || lastSeen.secsTo(now) > 300; // >5 phút không heartbeat = offline
                QString status;
                if (isOffline) { offline++; status = "offline"; }
                else if (m.cpuPercent >= ds.cpuThreshold || m.ramPercent >= ds.ramThreshold || m.diskPercent >= ds.diskThreshold)
                { critical++; status = "critical"; }
                else if (m.cpuPercent >= ds.cpuThreshold * 0.85 || m.ramPercent >= ds.ramThreshold * 0.85)
                { warning++; status = "warning"; }
                else { online++; status = "online"; }

                const QString self = m.deviceId == cfg_.deviceId ? " (this machine)" : "";
                machinesList_->addItem(QString("[%1] %2%3 - CPU %4%% RAM %5%% Disk %6%%")
                    .arg(status, QString::fromStdString(m.hostname), self)
                    .arg(m.cpuPercent, 0, 'f', 0).arg(m.ramPercent, 0, 'f', 0).arg(m.diskPercent, 0, 'f', 0));
            }

            fleetOnline_->setText(QString("Online: %1").arg(online));
            fleetOffline_->setText(QString("Offline: %1").arg(offline));
            fleetWarning_->setText(QString("Warning: %1").arg(warning));
            fleetCritical_->setText(QString("Critical: %1").arg(critical));
        });
}

void OverviewPage::loadProcesses()
{
    runAsync(this, []() { return getProcesses(15); },
        [this](std::vector<ProcessInfo>* procs, const std::exception* e) {
            if (e) return;
            processTable_->setRowCount(static_cast<int>(procs->size()));
            for (int i = 0; i < static_cast<int>(procs->size()); i++)
            {
                const auto& p = (*procs)[static_cast<size_t>(i)];
                processTable_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(p.name)));
                processTable_->setItem(i, 1, new QTableWidgetItem(QString::number(p.cpuPercent, 'f', 1)));
                processTable_->setItem(i, 2, new QTableWidgetItem(QString::number(p.memPercent, 'f', 1)));
            }
        });
}

void OverviewPage::loadPeriodAndTimeline()
{
    if (!db_)
    {
        singleDayContainer_->show(); rangeContainer_->hide();
        timelineLegend_->setEntries({});
        timelineStrip_->setSegments({});
        return;
    }

    if (currentPeriod_ == "today" || currentPeriod_ == "yesterday" || currentPeriod_ == "custom")
    {
        singleDayContainer_->show();
        rangeContainer_->hide();

        const time_t now = time(nullptr);
        const time_t dayStart = currentPeriod_ == "yesterday" ? (now - now % 86400) - 86400 : now - now % 86400;
        const time_t dayEnd = dayStart + 86400;

        std::string err;
        const auto activities = db_->activitiesForDay(dayStart, dayEnd, &err);

        std::vector<TimelineSegment> segs;
        std::map<std::string, qint64> totals;
        static const QColor kColors[] = {"#5aa9ff", "#22c55e", "#f5a524", "#a78bfa", "#f472b6", "#2dd4bf", "#fb923c", "#eab308"};

        for (const auto& a : activities)
        {
            if (a.duration <= 0) continue;
            const double startPct = static_cast<double>(a.startTime - dayStart) / 86400.0 * 100.0;
            double widthPct = static_cast<double>(a.duration) / 86400.0 * 100.0;
            if (widthPct < 0.15) widthPct = 0.15;

            size_t h = 0; for (unsigned char c : a.processName) h = h * 31 + c;
            segs.push_back({QString::fromStdString(a.processName), QString::fromStdString(a.windowTitle),
                             startPct, widthPct, kColors[h % 8]});
            totals[a.processName] += a.duration;
        }
        timelineStrip_->setSegments(segs);
        timelineStrip_->setZoom(timelineZoom_);

        std::vector<std::pair<std::string, qint64>> sorted(totals.begin(), totals.end());
        std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.second > b.second; });
        std::vector<LegendEntry> legend;
        for (auto& [name, secs] : sorted)
        {
            size_t h = 0; for (unsigned char c : name) h = h * 31 + c;
            legend.push_back({QString::fromStdString(name), kColors[h % 8], secs});
        }
        timelineLegend_->setEntries(legend);
    }
    else // this_week / last_week
    {
        singleDayContainer_->hide();
        rangeContainer_->show();

        const time_t now = time(nullptr);
        const time_t todayStart = now - now % 86400;
        struct tm tmBuf;
#ifdef _WIN32
        localtime_s(&tmBuf, &todayStart);
#else
        localtime_r(&todayStart, &tmBuf); // chỉ dùng khi biên dịch/test ngoài Windows
#endif
        int weekday = tmBuf.tm_wday; if (weekday == 0) weekday = 7;
        time_t mondayThis = todayStart - (weekday - 1) * 86400;
        const time_t start = currentPeriod_ == "last_week" ? mondayThis - 7 * 86400 : mondayThis;

        std::vector<int64_t> boundaries;
        for (int i = 0; i <= 7; i++) boundaries.push_back(start + i * 86400);

        std::string err;
        const auto totals = db_->dailyTotalsInRange(boundaries, &err);
        std::vector<DailyBar> bars;
        for (const auto& t : totals) bars.push_back({QString::fromStdString(t.date), t.totalSeconds});
        dailyBars_->setBars(bars);
    }
}

void OverviewPage::applyLiveStats(const LiveStats& live)
{
    sparkCpu_->pushValue(0, live.cpuPercent);
    sparkRam_->pushValue(0, live.ramPercent);
    sparkDisk_->pushValue(0, live.diskPercent);
    const double netTotal = live.netUploadBps + live.netDownloadBps;
    sparkNet_->pushValue(0, std::min(100.0, netTotal / (1024.0 * 1024.0) * 10.0)); // thang tương đối, chỉ để xem xu hướng

    cpuPercentLbl_->setText(QString::number(live.cpuPercent, 'f', 0) + "%");
    ramPercentLbl_->setText(QString::number(live.ramPercent, 'f', 0) + "%");
    diskPercentLbl_->setText(QString::number(live.diskPercent, 'f', 0) + "%");
    netPercentLbl_->setText(fmtBytesPerSec(netTotal));

    ramSubtitleLbl_->setText(QString("%1 / %2 GB").arg(live.ramUsedGB, 0, 'f', 1).arg(live.ramTotalGB, 0, 'f', 1));
    diskSubtitleLbl_->setText(QString("%1 / %2 GB").arg(live.diskUsedGB, 0, 'f', 0).arg(live.diskTotalGB, 0, 'f', 0));
    netSubtitleLbl_->setText(QString("↓%1 ↑%2").arg(fmtBytesPerSec(live.netDownloadBps), fmtBytesPerSec(live.netUploadBps)));
    cpuSubtitleLbl_->setText(QString("%1 cores").arg(live.cpuPerCore.size()));

    historyChart_->pushValue(0, live.cpuPercent);
    historyChart_->pushValue(1, live.ramPercent);
    historyChart_->pushValue(2, live.diskPercent);

    const DashSettings ds = loadDashSettings();
    const bool alerts[3] = {live.cpuPercent >= ds.cpuThreshold, live.ramPercent >= ds.ramThreshold, live.diskPercent >= ds.diskThreshold};
    const char* names[3] = {"CPU", "RAM", "Disk"};
    for (int i = 0; i < 3; i++)
    {
        if (alerts[i] && !prevAlert_[i]) // cạnh lên: mới vượt ngưỡng - ghi 1 dòng cảnh báo (không log lại mỗi giây)
        {
            alertsList_->insertItem(0, QString("[%1] %2 usage crossed alert threshold")
                .arg(QDateTime::currentDateTime().toString("HH:mm:ss"), names[i]));
            while (alertsList_->count() > 20)
                delete alertsList_->takeItem(alertsList_->count() - 1);
        }
        prevAlert_[i] = alerts[i];
    }
}

void OverviewPage::tick()
{
    applyLiveStats(collector_->live());
}

} // namespace jit::dash::ui
