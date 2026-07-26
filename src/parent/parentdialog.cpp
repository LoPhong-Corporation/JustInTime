// parentdialog.cpp

#include "parentdialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>

#include <cstring>

extern "C" {
#include "parentlink.h"
#include "applimits.h"
#include "i18n.h"
}

static QString tr_(const char *key)
{
    return QString::fromUtf8(i18n_t(key));
}

ParentDialog::ParentDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr_("pdlg.title"));
    setMinimumWidth(640);

    // ---------------- Mời con ----------------

    auto *inviteNote = new QLabel(tr_("pdlg.invite_note"), this);
    inviteNote->setWordWrap(true);
    inviteNote->setStyleSheet("color: #7e9ac0; font-size: 11px;");

    m_inviteEmail = new QLineEdit(this);
    m_inviteEmail->setPlaceholderText(tr_("pdlg.invite_placeholder"));

    auto *inviteBtn = new QPushButton(tr_("pdlg.invite_btn"), this);
    connect(inviteBtn, &QPushButton::clicked, this, &ParentDialog::onInvite);

    auto *inviteRow = new QHBoxLayout;
    inviteRow->addWidget(m_inviteEmail);
    inviteRow->addWidget(inviteBtn);

    auto *inviteBox = new QGroupBox(tr_("pdlg.invite_box"), this);
    auto *inviteBoxLayout = new QVBoxLayout(inviteBox);
    inviteBoxLayout->addWidget(inviteNote);
    inviteBoxLayout->addLayout(inviteRow);

    // ---------------- Danh sách con ----------------

    m_childrenTable = new QTableWidget(this);
    m_childrenTable->setColumnCount(2);
    m_childrenTable->setHorizontalHeaderLabels({tr_("pdlg.col_email"), tr_("pdlg.col_status")});
    m_childrenTable->horizontalHeader()->setStretchLastSection(true);
    m_childrenTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_childrenTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_childrenTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(
        m_childrenTable, &QTableWidget::itemSelectionChanged,
        this, &ParentDialog::onChildSelectionChanged
    );

    auto *childrenBox = new QGroupBox(tr_("pdlg.children_box"), this);
    auto *childrenBoxLayout = new QVBoxLayout(childrenBox);
    childrenBoxLayout->addWidget(m_childrenTable);

    // ---------------- Giới hạn app ----------------

    m_limitsHeader = new QLabel(tr_("pdlg.limits_hint_select"), this);
    m_limitsHeader->setStyleSheet("color: #7e9ac0; font-size: 11px;");

    m_limitsTable = new QTableWidget(this);
    m_limitsTable->setColumnCount(3);
    m_limitsTable->setHorizontalHeaderLabels({tr_("pdlg.limit_process"), tr_("pdlg.limit_amount"), tr_("pdlg.col_status")});
    m_limitsTable->horizontalHeader()->setStretchLastSection(true);
    m_limitsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_limitsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_limitsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_processName = new QLineEdit(this);
    m_processName->setPlaceholderText(tr_("pdlg.limit_process_placeholder"));

    m_dailyMinutes = new QSpinBox(this);
    m_dailyMinutes->setRange(1, 1440);
    m_dailyMinutes->setValue(60);
    m_dailyMinutes->setSuffix(tr_("pdlg.limit_minutes_suffix"));

    m_noLimitCheckbox = new QCheckBox(tr_("pdlg.limit_nolimit"), this);
    m_blockCheckbox   = new QCheckBox(tr_("pdlg.limit_block"), this);

    connect(m_noLimitCheckbox, &QCheckBox::toggled, m_dailyMinutes, &QSpinBox::setDisabled);
    connect(m_blockCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        m_dailyMinutes->setDisabled(checked || m_noLimitCheckbox->isChecked());
        m_noLimitCheckbox->setDisabled(checked);
    });

    auto *limitForm = new QFormLayout;
    limitForm->addRow(tr_("pdlg.limit_process"), m_processName);
    limitForm->addRow(tr_("pdlg.limit_amount"), m_dailyMinutes);
    limitForm->addRow(QString(), m_noLimitCheckbox);
    limitForm->addRow(QString(), m_blockCheckbox);

    auto *saveLimitBtn   = new QPushButton(tr_("pdlg.limit_save_btn"), this);
    auto *deleteLimitBtn = new QPushButton(tr_("pdlg.limit_delete_btn"), this);

    connect(saveLimitBtn, &QPushButton::clicked, this, &ParentDialog::onSaveLimit);
    connect(deleteLimitBtn, &QPushButton::clicked, this, &ParentDialog::onDeleteLimit);

    auto *limitBtnRow = new QHBoxLayout;
    limitBtnRow->addWidget(saveLimitBtn);
    limitBtnRow->addWidget(deleteLimitBtn);
    limitBtnRow->addStretch();

    auto *limitsBox = new QGroupBox(tr_("pdlg.limits_box"), this);
    auto *limitsBoxLayout = new QVBoxLayout(limitsBox);
    limitsBoxLayout->addWidget(m_limitsHeader);
    limitsBoxLayout->addWidget(m_limitsTable);
    limitsBoxLayout->addLayout(limitForm);
    limitsBoxLayout->addLayout(limitBtnRow);

    // ---------------- Tổng thể ----------------

    auto *closeBtn = new QPushButton(tr_("pdlg.close_btn"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    auto *closeRow = new QHBoxLayout;
    closeRow->addStretch();
    closeRow->addWidget(closeBtn);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(inviteBox);
    mainLayout->addWidget(childrenBox);
    mainLayout->addWidget(limitsBox);
    mainLayout->addLayout(closeRow);

    resize(680, 640);

    refreshChildren();
}

void ParentDialog::refreshChildren()
{
    ParentLink links[MAX_LINKS];
    int count = parentlink_list_as_parent(links, MAX_LINKS);

    m_childrenTable->setRowCount(count);

    for (int i = 0; i < count; i++)
    {
        auto *emailItem = new QTableWidgetItem(QString::fromUtf8(links[i].other_email));
        emailItem->setData(Qt::UserRole, QString::fromUtf8(links[i].other_user_id));

        QString statusText;
        if (strcmp(links[i].status, "pending") == 0)
            statusText = tr_("pdlg.status_pending");
        else if (strcmp(links[i].status, "approved") == 0)
            statusText = tr_("pdlg.status_approved");
        else
            statusText = tr_("pdlg.status_revoked");

        m_childrenTable->setItem(i, 0, emailItem);
        m_childrenTable->setItem(i, 1, new QTableWidgetItem(statusText));
    }

    if (count == 0)
    {
        m_childrenTable->setRowCount(1);
        auto *emptyItem = new QTableWidgetItem(tr_("pdlg.children_empty"));
        emptyItem->setFlags(Qt::NoItemFlags);
        m_childrenTable->setItem(0, 0, emptyItem);
        m_childrenTable->setSpan(0, 0, 1, 2);
    }

    m_limitsTable->setRowCount(0);
    m_limitsHeader->setText(tr_("pdlg.limits_hint_select"));
}

QString ParentDialog::selectedChildUserId() const
{
    auto items = m_childrenTable->selectedItems();

    if (items.isEmpty())
        return QString();

    QTableWidgetItem *idItem = m_childrenTable->item(items.first()->row(), 0);

    if (!idItem)
        return QString();

    return idItem->data(Qt::UserRole).toString();
}

void ParentDialog::onChildSelectionChanged()
{
    refreshLimits();
}

void ParentDialog::refreshLimits()
{
    QString childId = selectedChildUserId();

    if (childId.isEmpty())
    {
        m_limitsTable->setRowCount(0);
        m_limitsHeader->setText(tr_("pdlg.limits_hint_select"));
        return;
    }

    QByteArray childIdUtf8 = childId.toUtf8();

    AppLimit limits[MAX_LIMITS];
    int count = applimits_list_for_child(childIdUtf8.constData(), limits, MAX_LIMITS);

    m_limitsHeader->setText(
        count > 0
            ? tr_("pdlg.limits_hint_count").arg(count)
            : tr_("pdlg.limits_none")
    );

    m_limitsTable->setRowCount(count);

    for (int i = 0; i < count; i++)
    {
        auto *nameItem = new QTableWidgetItem(QString::fromUtf8(limits[i].process_name));
        nameItem->setData(Qt::UserRole, static_cast<qlonglong>(limits[i].id));

        QString limitText = limits[i].blocked
            ? tr_("pdlg.limit_blocked_label")
            : (limits[i].daily_limit_sec >= 0
                   ? QString("%1%2").arg(limits[i].daily_limit_sec / 60).arg(tr_("pdlg.limit_minutes_suffix"))
                   : tr_("pdlg.limit_unlimited"));

        QString statusText = limits[i].blocked ? tr_("pdlg.limit_block") : tr_("pdlg.limit_allowed");

        m_limitsTable->setItem(i, 0, nameItem);
        m_limitsTable->setItem(i, 1, new QTableWidgetItem(limitText));
        m_limitsTable->setItem(i, 2, new QTableWidgetItem(statusText));
    }
}

void ParentDialog::onInvite()
{
    QString email = m_inviteEmail->text().trimmed();

    if (email.isEmpty())
    {
        QMessageBox::information(this, "JustInTime", tr_("pdlg.enter_child_email"));
        return;
    }

    QByteArray emailUtf8 = email.toUtf8();
    char err[300] = {0};

    if (parentlink_invite_child(emailUtf8.constData(), err, sizeof(err)))
    {
        QMessageBox::information(this, "JustInTime", tr_("pdlg.invite_sent"));
        m_inviteEmail->clear();
        refreshChildren();
    }
    else
    {
        QMessageBox::warning(this, "JustInTime", QString::fromUtf8(err));
    }
}

void ParentDialog::onSaveLimit()
{
    QString childId = selectedChildUserId();

    if (childId.isEmpty())
    {
        QMessageBox::information(this, "JustInTime", tr_("pdlg.select_child_first"));
        return;
    }

    QString processName = m_processName->text().trimmed();

    if (processName.isEmpty())
    {
        QMessageBox::information(this, "JustInTime", tr_("pdlg.enter_process"));
        return;
    }

    bool blocked = m_blockCheckbox->isChecked();
    bool noLimit = m_noLimitCheckbox->isChecked();

    int dailyLimitSec = (blocked || noLimit) ? -1 : m_dailyMinutes->value() * 60;

    QByteArray childIdUtf8 = childId.toUtf8();
    QByteArray processUtf8 = processName.toUtf8();

    char err[300] = {0};

    if (
        applimits_set(
            childIdUtf8.constData(),
            processUtf8.constData(),
            dailyLimitSec,
            blocked ? 1 : 0,
            err, sizeof(err)
        )
    )
    {
        QMessageBox::information(this, "JustInTime", tr_("pdlg.limit_saved"));
        m_processName->clear();
        refreshLimits();
    }
    else
    {
        QMessageBox::warning(this, "JustInTime", QString::fromUtf8(err));
    }
}

void ParentDialog::onDeleteLimit()
{
    auto items = m_limitsTable->selectedItems();

    if (items.isEmpty())
    {
        QMessageBox::information(this, "JustInTime", tr_("pdlg.select_limit_first"));
        return;
    }

    QTableWidgetItem *idItem = m_limitsTable->item(items.first()->row(), 0);

    if (!idItem)
        return;

    long limitId = idItem->data(Qt::UserRole).toLongLong();

    char err[300] = {0};

    if (applimits_delete(limitId, err, sizeof(err)))
    {
        refreshLimits();
    }
    else
    {
        QMessageBox::warning(this, "JustInTime", QString::fromUtf8(err));
    }
}
