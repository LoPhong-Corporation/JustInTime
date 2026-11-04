#include "ui/pages/overviewpage.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <map>

namespace jit::dash::ui {

namespace {

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

    // ------------------------------------------------------- Period + timeline --
    auto* periodRow = new QHBoxLayout;
    const QString labels[5] = {"Today", "Yesterday", "This Week", "Last Week", "Custom"};
    const QString keys[5] = {"today", "yesterday", "this_week", "last_week", "custom"};
    for (int i = 0; i < 5; i++)
    {
        auto* btn = new QPushButton(labels[i]);
        btn->setCheckable(true);
        btn->setMinimumWidth(btn->fontMetrics().horizontalAdvance(labels[i]) + 36);
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

    // ------------------------------------------------------------ Quick actions --
    auto* actionsRow = new QHBoxLayout;
    auto mkAction = [&](const QString& label, std::function<void()> fn) {
        auto* btn = new QPushButton(label);
        connect(btn, &QPushButton::clicked, this, [fn] { if (fn) fn(); });
        actionsRow->addWidget(btn);
    };
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
    sparkNet_->pushValue(0, std::min(100.0, netTotal / (1024.0 * 1024.0) * 10.0));

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
}

void OverviewPage::tick()
{
    applyLiveStats(collector_->live());
}

} // namespace jit::dash::ui
