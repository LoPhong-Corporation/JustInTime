#include "ui/pages/localpage.h"
#include <QDateTime>

#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace jit::dash::ui {

namespace {
QString fmtDuration(qint64 sec)
{
    const qint64 h = sec / 3600, m = (sec % 3600) / 60;
    return h > 0 ? QString("%1h %2m").arg(h).arg(m) : QString("%1m").arg(m);
}
QString fmtEpoch(qint64 t)
{
    return QDateTime::fromSecsSinceEpoch(t).toString("yyyy-MM-dd HH:mm");
}
} // namespace

LocalPage::LocalPage(LocalDB* db, QWidget* parent) : QWidget(parent), db_(db)
{
    auto* root = new QVBoxLayout(this);

    noDbNotice_ = new QLabel(
        "No local activity database found yet. The JustInTime agent creates it automatically once it starts "
        "tracking activity on this machine.");
    noDbNotice_->setWordWrap(true);
    noDbNotice_->setStyleSheet("color:#f5a524; padding:8px;");
    noDbNotice_->hide();
    root->addWidget(noDbNotice_);

    auto* usageLbl = new QLabel("Time by application (all-time)");
    usageLbl->setStyleSheet("color:#eef4fb; font-weight:600;");
    root->addWidget(usageLbl);

    usageTable_ = new QTableWidget(0, 2);
    usageTable_->setHorizontalHeaderLabels({"Process", "Total time"});
    usageTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    usageTable_->verticalHeader()->hide();
    usageTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(usageTable_, 1);

    auto* recentLbl = new QLabel("Recent activity");
    recentLbl->setStyleSheet("color:#eef4fb; font-weight:600; margin-top:8px;");
    root->addWidget(recentLbl);

    recentTable_ = new QTableWidget(0, 4);
    recentTable_->setHorizontalHeaderLabels({"Process", "Window title", "Duration", "Started"});
    recentTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    recentTable_->verticalHeader()->hide();
    recentTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(recentTable_, 2);

    auto* aiNotice = new QLabel(
        "AI-powered insights and chat (via Gemini) are not available in this build yet.");
    aiNotice->setStyleSheet("color:#7e9ac0; font-style:italic; margin-top:8px;");
    root->addWidget(aiNotice);
}

void LocalPage::refresh()
{
    if (!db_)
    {
        noDbNotice_->show();
        return;
    }
    noDbNotice_->hide();

    std::string err;
    const auto usage = db_->usageInRange(0, static_cast<int64_t>(time(nullptr)) + 3600LL * 24 * 3650, &err);
    usageTable_->setRowCount(static_cast<int>(usage.size()));
    for (int i = 0; i < static_cast<int>(usage.size()); i++)
    {
        usageTable_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(usage[static_cast<size_t>(i)].processName)));
        usageTable_->setItem(i, 1, new QTableWidgetItem(fmtDuration(usage[static_cast<size_t>(i)].totalSeconds)));
    }

    const auto recent = db_->recentActivities(50, &err);
    recentTable_->setRowCount(static_cast<int>(recent.size()));
    for (int i = 0; i < static_cast<int>(recent.size()); i++)
    {
        const auto& a = recent[static_cast<size_t>(i)];
        recentTable_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(a.processName)));
        recentTable_->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(a.windowTitle)));
        recentTable_->setItem(i, 2, new QTableWidgetItem(fmtDuration(a.duration)));
        recentTable_->setItem(i, 3, new QTableWidgetItem(fmtEpoch(a.startTime)));
    }
}

} // namespace jit::dash::ui
