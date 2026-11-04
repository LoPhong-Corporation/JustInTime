#include "ui/pages/cloudpage.h"
#include "ui/asyncutil.h"
#include "ui/timelinewidget.h"
#include "cloud.h"
#include "dashsession.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <map>

namespace jit::dash::ui {

namespace {
QString fmtDuration(qint64 sec)
{
    const qint64 h = sec / 3600, m = (sec % 3600) / 60;
    return h > 0 ? QString("%1h %2m").arg(h).arg(m) : QString("%1m").arg(m);
}
} // namespace

CloudPage::CloudPage(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);

    notice_ = new QLabel("Log in from the sidebar to see your cloud-synced activity across devices.");
    notice_->setStyleSheet("color:#f5a524; padding:8px;");
    notice_->setWordWrap(true);
    notice_->hide();
    root->addWidget(notice_);

    auto* rangeRow = new QHBoxLayout;
    weekBtn_ = new QPushButton("Last 7 Days"); weekBtn_->setCheckable(true); weekBtn_->setChecked(true);
    monthBtn_ = new QPushButton("Last 30 Days"); monthBtn_->setCheckable(true);
    weekBtn_->setMinimumWidth(weekBtn_->fontMetrics().horizontalAdvance("Last 7 Days") + 36);
    monthBtn_->setMinimumWidth(monthBtn_->fontMetrics().horizontalAdvance("Last 30 Days") + 36);
    connect(weekBtn_, &QPushButton::clicked, this, [this] { setRange("week"); });
    connect(monthBtn_, &QPushButton::clicked, this, [this] { setRange("month"); });
    rangeRow->addWidget(weekBtn_);
    rangeRow->addWidget(monthBtn_);
    rangeRow->addStretch();
    root->addLayout(rangeRow);

    auto* summaryLbl = new QLabel("Time by application");
    summaryLbl->setStyleSheet("color:#eef4fb; font-weight:600;");
    root->addWidget(summaryLbl);
    summaryTable_ = new QTableWidget(0, 2);
    summaryTable_->setHorizontalHeaderLabels({"Process", "Total time"});
    summaryTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    summaryTable_->verticalHeader()->hide();
    summaryTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(summaryTable_, 1);

    auto* dailyLbl = new QLabel("Daily totals");
    dailyLbl->setStyleSheet("color:#eef4fb; font-weight:600; margin-top:8px;");
    root->addWidget(dailyLbl);
    dailyChart_ = new DailyBarsWidget;
    root->addWidget(dailyChart_);

    auto* recentLbl = new QLabel("Recent activity (all devices)");
    recentLbl->setStyleSheet("color:#eef4fb; font-weight:600; margin-top:8px;");
    root->addWidget(recentLbl);
    recentTable_ = new QTableWidget(0, 4);
    recentTable_->setHorizontalHeaderLabels({"Device", "Process", "Window title", "Duration"});
    recentTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    recentTable_->verticalHeader()->hide();
    recentTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(recentTable_, 2);
}

void CloudPage::setRange(const QString& r)
{
    range_ = r;
    weekBtn_->setChecked(r == "week");
    monthBtn_->setChecked(r == "month");
    loadSummary();
    loadDaily();
}

void CloudPage::refresh()
{
    if (!loadDashSession())
    {
        notice_->show();
        summaryTable_->setRowCount(0);
        recentTable_->setRowCount(0);
        dailyChart_->setBars({});
        return;
    }
    notice_->hide();
    loadSummary();
    loadDaily();
    loadRecent();
}

namespace {
std::pair<std::string, std::string> dayRangeFor(const QString& range)
{
    const time_t now = time(nullptr);
    struct tm tmBuf;
#ifdef _WIN32
    localtime_s(&tmBuf, &now);
#else
    localtime_r(&now, &tmBuf);
#endif
    char today[16]; strftime(today, sizeof(today), "%Y-%m-%d", &tmBuf);

    const int days = range == "month" ? 29 : 6;
    const time_t from = now - days * 86400;
    struct tm fromTm;
#ifdef _WIN32
    localtime_s(&fromTm, &from);
#else
    localtime_r(&from, &fromTm);
#endif
    char fromBuf[16]; strftime(fromBuf, sizeof(fromBuf), "%Y-%m-%d", &fromTm);
    return {fromBuf, today};
}
} // namespace

void CloudPage::loadSummary()
{
    const auto sess = loadDashSession();
    if (!sess) return;
    const std::string token = sess->accessToken;
    const auto [from, to] = dayRangeFor(range_);

    runAsync(this,
        [token, from, to]() { return cloud::Client(token).dailyTotals(from, to); },
        [this](std::vector<cloud::Client::DailyTotal>* totals, const std::exception* e) {
            if (e) return;
            std::map<std::string, int64_t> byApp;
            for (const auto& t : *totals) byApp[t.processName] += t.totalSeconds;
            std::vector<std::pair<std::string, int64_t>> sorted(byApp.begin(), byApp.end());
            std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.second > b.second; });

            summaryTable_->setRowCount(static_cast<int>(sorted.size()));
            for (int i = 0; i < static_cast<int>(sorted.size()); i++)
            {
                summaryTable_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(sorted[static_cast<size_t>(i)].first)));
                summaryTable_->setItem(i, 1, new QTableWidgetItem(fmtDuration(sorted[static_cast<size_t>(i)].second)));
            }
        });
}

void CloudPage::loadDaily()
{
    const auto sess = loadDashSession();
    if (!sess) return;
    const std::string token = sess->accessToken;
    const auto [from, to] = dayRangeFor(range_);

    runAsync(this,
        [token, from, to]() { return cloud::Client(token).dailyTotals(from, to); },
        [this](std::vector<cloud::Client::DailyTotal>* totals, const std::exception* e) {
            if (e) return;
            std::map<std::string, int64_t> byDay;
            for (const auto& t : *totals) byDay[t.day] += t.totalSeconds;

            std::vector<DailyBar> bars;
            for (const auto& [d, v] : byDay) bars.push_back({QString::fromStdString(d), v});
            dailyChart_->setBars(bars);
        });
}

void CloudPage::loadRecent()
{
    const auto sess = loadDashSession();
    if (!sess) return;
    const std::string token = sess->accessToken;

    runAsync(this,
        [token]() { return cloud::Client(token).recentLogs(100); },
        [this](std::vector<cloud::Client::RecentLog>* logs, const std::exception* e) {
            if (e) return;
            recentTable_->setRowCount(static_cast<int>(logs->size()));
            for (int i = 0; i < static_cast<int>(logs->size()); i++)
            {
                const auto& l = (*logs)[static_cast<size_t>(i)];
                recentTable_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(l.deviceId)));
                recentTable_->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(l.processName)));
                recentTable_->setItem(i, 2, new QTableWidgetItem(QString::fromStdString(l.windowTitle)));
                recentTable_->setItem(i, 3, new QTableWidgetItem(fmtDuration(l.duration)));
            }
        });
}

} // namespace jit::dash::ui
