#include "ui/pages/familypage.h"
#include "ui/asyncutil.h"
#include "cloud.h"
#include "dashsession.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace jit::dash::ui {

namespace {
QString fmtDuration(qint64 sec)
{
    const qint64 h = sec / 3600, m = (sec % 3600) / 60;
    return h > 0 ? QString("%1h %2m").arg(h).arg(m) : QString("%1m").arg(m);
}
} // namespace

FamilyPage::FamilyPage(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);

    notice_ = new QLabel("Log in from the sidebar to manage family links.");
    notice_->setStyleSheet("color:#f5a524; padding:8px;");
    notice_->setWordWrap(true);
    notice_->hide();
    root->addWidget(notice_);

    // ------------------------------------------------------- Con của tôi --
    auto* childrenBox = new QGroupBox("My Children");
    auto* childrenLayout = new QVBoxLayout(childrenBox);

    auto* inviteRow = new QHBoxLayout;
    inviteEmail_ = new QLineEdit;
    inviteEmail_->setPlaceholderText("child@example.com");
    auto* inviteBtn = new QPushButton("Invite");
    connect(inviteBtn, &QPushButton::clicked, this, &FamilyPage::inviteChild);
    inviteRow->addWidget(inviteEmail_, 1);
    inviteRow->addWidget(inviteBtn);
    childrenLayout->addLayout(inviteRow);

    childrenTable_ = new QTableWidget(0, 4);
    childrenTable_->setHorizontalHeaderLabels({"Email", "Status", "Permission", "Action"});
    childrenTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    childrenTable_->verticalHeader()->hide();
    childrenTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    childrenTable_->setFixedHeight(160);
    childrenLayout->addWidget(childrenTable_);

    childrenLayout->addWidget(new QLabel("View child's activity / limits:"));
    childSelector_ = new QComboBox;
    connect(childSelector_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &FamilyPage::onChildSelected);
    childrenLayout->addWidget(childSelector_);

    childSummaryTable_ = new QTableWidget(0, 2);
    childSummaryTable_->setHorizontalHeaderLabels({"Process", "Time today"});
    childSummaryTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    childSummaryTable_->verticalHeader()->hide();
    childSummaryTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    childSummaryTable_->setFixedHeight(120);
    childrenLayout->addWidget(childSummaryTable_);

    childrenLayout->addWidget(new QLabel("App limits:"));
    limitsTable_ = new QTableWidget(0, 4);
    limitsTable_->setHorizontalHeaderLabels({"Process", "Daily limit", "Blocked", "Action"});
    limitsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    limitsTable_->verticalHeader()->hide();
    limitsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    limitsTable_->setFixedHeight(120);
    childrenLayout->addWidget(limitsTable_);

    auto* newLimitRow = new QHBoxLayout;
    limitProcessName_ = new QLineEdit; limitProcessName_->setPlaceholderText("process.exe");
    limitMinutes_ = new QSpinBox; limitMinutes_->setRange(0, 1440); limitMinutes_->setSuffix(" min/day"); limitMinutes_->setSpecialValueText("No limit");
    limitBlocked_ = new QCheckBox("Blocked entirely");
    auto* setLimitBtn = new QPushButton("Set Limit");
    connect(setLimitBtn, &QPushButton::clicked, this, &FamilyPage::setLimit);
    newLimitRow->addWidget(limitProcessName_);
    newLimitRow->addWidget(limitMinutes_);
    newLimitRow->addWidget(limitBlocked_);
    newLimitRow->addWidget(setLimitBtn);
    childrenLayout->addLayout(newLimitRow);

    root->addWidget(childrenBox);

    // --------------------------------------------------- Phụ huynh của tôi --
    auto* parentsBox = new QGroupBox("My Parents");
    auto* parentsLayout = new QVBoxLayout(parentsBox);
    parentsTable_ = new QTableWidget(0, 3);
    parentsTable_->setHorizontalHeaderLabels({"Email", "Status", "Since"});
    parentsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    parentsTable_->verticalHeader()->hide();
    parentsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    parentsLayout->addWidget(parentsTable_);
    root->addWidget(parentsBox);
}

void FamilyPage::refresh()
{
    if (!loadDashSession())
    {
        notice_->show();
        childrenTable_->setRowCount(0);
        parentsTable_->setRowCount(0);
        childSelector_->clear();
        return;
    }
    notice_->hide();
    loadChildren();
    loadParents();
}

void FamilyPage::inviteChild()
{
    const QString email = inviteEmail_->text().trimmed();
    if (email.isEmpty()) return;

    const std::string token = loadDashSession()->accessToken, emailStd = email.toStdString();
    runAsyncVoid(this,
        [token, emailStd]() { cloud::Client(token).inviteChild(emailStd); },
        [this](const std::exception* e) {
            if (e) QMessageBox::warning(this, "Invite failed", QString::fromStdString(e->what()));
            else { inviteEmail_->clear(); loadChildren(); }
        });
}

void FamilyPage::loadChildren()
{
    const std::string token = loadDashSession()->accessToken;

    runAsync(this,
        [token]() { return cloud::Client(token).listLinksAsParent(); },
        [this](std::vector<cloud::Client::ParentLink>* links, const std::exception* e) {
            if (e) return;

            childrenTable_->setRowCount(static_cast<int>(links->size()));
            childSelector_->clear();

            for (int i = 0; i < static_cast<int>(links->size()); i++)
            {
                const auto& l = (*links)[static_cast<size_t>(i)];
                childrenTable_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(l.otherEmail)));
                childrenTable_->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(l.status)));

                auto* permBox = new QComboBox;
                permBox->addItems({"full", "view_only"});
                const std::string linkId = std::to_string(l.id);
                const std::string token2 = loadDashSession() ? loadDashSession()->accessToken : "";
                connect(permBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [this, linkId, token2, permBox](int) {
                        const std::string level = permBox->currentText().toStdString();
                        runAsyncVoid(this,
                            [token2, linkId, level]() { cloud::Client(token2).setPermission(std::stoll(linkId), level); },
                            [](const std::exception*) {});
                    });
                childrenTable_->setCellWidget(i, 2, permBox);

                if (l.status == "pending")
                {
                    auto* approveBtn = new QPushButton("Approve");
                    const int64_t id = l.id;
                    connect(approveBtn, &QPushButton::clicked, this, [this, id] {
                        const std::string t = loadDashSession()->accessToken;
                        runAsyncVoid(this, [t, id]() { cloud::Client(t).approveLink(id); },
                                     [this](const std::exception*) { loadChildren(); });
                    });
                    childrenTable_->setCellWidget(i, 3, approveBtn);
                }
                else
                {
                    auto* revokeBtn = new QPushButton("Revoke");
                    const int64_t id = l.id;
                    connect(revokeBtn, &QPushButton::clicked, this, [this, id] {
                        const std::string t = loadDashSession()->accessToken;
                        runAsyncVoid(this, [t, id]() { cloud::Client(t).revokeLink(id); },
                                     [this](const std::exception*) { loadChildren(); });
                    });
                    childrenTable_->setCellWidget(i, 3, revokeBtn);
                }

                if (l.status == "approved")
                    childSelector_->addItem(QString::fromStdString(l.otherEmail), QString::fromStdString(l.otherUserId));
            }
        });
}

void FamilyPage::loadParents()
{
    const std::string token = loadDashSession()->accessToken;
    runAsync(this,
        [token]() { return cloud::Client(token).listLinksAsChild(); },
        [this](std::vector<cloud::Client::ParentLink>* links, const std::exception* e) {
            if (e) return;
            parentsTable_->setRowCount(static_cast<int>(links->size()));
            for (int i = 0; i < static_cast<int>(links->size()); i++)
            {
                const auto& l = (*links)[static_cast<size_t>(i)];
                parentsTable_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(l.otherEmail)));
                parentsTable_->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(l.status)));
                parentsTable_->setItem(i, 2, new QTableWidgetItem(QString::fromStdString(l.createdAt)));
            }
        });
}

void FamilyPage::onChildSelected(int index)
{
    if (index < 0) return;
    const std::string childId = childSelector_->itemData(index).toString().toStdString();
    loadChildSummaryAndLimits(childId);
}

void FamilyPage::loadChildSummaryAndLimits(const std::string& childId)
{
    const std::string token = loadDashSession()->accessToken;
    const time_t now = time(nullptr);
    const time_t todayStart = now - now % 86400;

    runAsync(this,
        [token, childId, todayStart]() { return cloud::Client(token).activityLogsForChild(childId, todayStart, 500); },
        [this](std::vector<cloud::Client::RecentLog>* logs, const std::exception* e) {
            if (e) return;
            std::map<std::string, int64_t> byApp;
            for (const auto& l : *logs) byApp[l.processName] += l.duration;
            std::vector<std::pair<std::string, int64_t>> sorted(byApp.begin(), byApp.end());
            std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.second > b.second; });

            childSummaryTable_->setRowCount(static_cast<int>(sorted.size()));
            for (int i = 0; i < static_cast<int>(sorted.size()); i++)
            {
                childSummaryTable_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(sorted[static_cast<size_t>(i)].first)));
                childSummaryTable_->setItem(i, 1, new QTableWidgetItem(fmtDuration(sorted[static_cast<size_t>(i)].second)));
            }
        });

    runAsync(this,
        [token, childId]() { return cloud::Client(token).listLimitsForChild(childId); },
        [this, childId](std::vector<cloud::Client::AppLimit>* limits, const std::exception* e) {
            if (e) return;
            limitsTable_->setRowCount(static_cast<int>(limits->size()));
            for (int i = 0; i < static_cast<int>(limits->size()); i++)
            {
                const auto& l = (*limits)[static_cast<size_t>(i)];
                limitsTable_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(l.processName)));
                limitsTable_->setItem(i, 1, new QTableWidgetItem(
                    l.dailyLimitSec ? fmtDuration(*l.dailyLimitSec) : QString("No limit")));
                limitsTable_->setItem(i, 2, new QTableWidgetItem(l.blocked ? "Yes" : "No"));

                auto* delBtn = new QPushButton("Delete");
                const int64_t id = l.id;
                connect(delBtn, &QPushButton::clicked, this, [this, id, childId] {
                    const std::string t = loadDashSession()->accessToken;
                    runAsyncVoid(this, [t, id]() { cloud::Client(t).deleteLimit(id); },
                                 [this, childId](const std::exception*) { loadChildSummaryAndLimits(childId); });
                });
                limitsTable_->setCellWidget(i, 3, delBtn);
            }
        });
}

void FamilyPage::setLimit()
{
    const int idx = childSelector_->currentIndex();
    if (idx < 0) { QMessageBox::information(this, "No child selected", "Pick a child above first."); return; }

    const std::string childId = childSelector_->itemData(idx).toString().toStdString();
    const std::string proc = limitProcessName_->text().trimmed().toStdString();
    if (proc.empty()) return;

    std::optional<int> minutes;
    if (limitMinutes_->value() > 0)
        minutes = limitMinutes_->value() * 60;
    const bool blocked = limitBlocked_->isChecked();
    const std::string token = loadDashSession()->accessToken;

    runAsyncVoid(this,
        [token, childId, proc, minutes, blocked]() { cloud::Client(token).setLimit(childId, proc, minutes, blocked); },
        [this, childId](const std::exception* e) {
            if (e) QMessageBox::warning(this, "Failed", QString::fromStdString(e->what()));
            else { limitProcessName_->clear(); limitMinutes_->setValue(0); limitBlocked_->setChecked(false); loadChildSummaryAndLimits(childId); }
        });
}

} // namespace jit::dash::ui
