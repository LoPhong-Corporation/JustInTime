// parentlinkpage.cpp

#include "parentlinkpage.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>

#include <cstring>

extern "C" {
#include "parentlink.h"
#include "i18n.h"
}

static QString tr_(const char *key)
{
    return QString::fromUtf8(i18n_t(key));
}

ParentLinkPage::ParentLinkPage(QWidget *parent) : QWidget(parent)
{
    auto *heading = new QLabel(tr_("plink.title"), this);
    heading->setStyleSheet("font-size: 16px; font-weight: 600;");

    auto *note = new QLabel(tr_("plink.note"), this);
    note->setWordWrap(true);
    note->setStyleSheet("color: #7e9ac0; font-size: 11px;");

    m_table = new QTableWidget(this);
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({tr_("plink.col_email"), tr_("plink.col_status")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);

    auto *approveBtn = new QPushButton(tr_("plink.approve_btn"), this);
    auto *revokeBtn  = new QPushButton(tr_("plink.revoke_btn"), this);

    connect(approveBtn, &QPushButton::clicked, this, &ParentLinkPage::onApprove);
    connect(revokeBtn, &QPushButton::clicked, this, &ParentLinkPage::onRevoke);

    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(approveBtn);
    btnRow->addWidget(revokeBtn);
    btnRow->addStretch();

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(heading);
    mainLayout->addWidget(note);
    mainLayout->addSpacing(8);
    mainLayout->addWidget(m_table);
    mainLayout->addLayout(btnRow);

    refresh();
}

void ParentLinkPage::refresh()
{
    ParentLink links[MAX_LINKS];
    int count = parentlink_list_as_child(links, MAX_LINKS);

    m_table->setRowCount(count);

    for (int i = 0; i < count; i++)
    {
        auto *emailItem = new QTableWidgetItem(QString::fromUtf8(links[i].other_email));
        emailItem->setData(Qt::UserRole, static_cast<qlonglong>(links[i].id));

        QString statusText;
        if (strcmp(links[i].status, "pending") == 0)
            statusText = tr_("plink.status_pending");
        else if (strcmp(links[i].status, "approved") == 0)
            statusText = tr_("plink.status_approved");
        else
            statusText = tr_("plink.status_revoked");

        auto *statusItem = new QTableWidgetItem(statusText);

        m_table->setItem(i, 0, emailItem);
        m_table->setItem(i, 1, statusItem);
    }

    if (count == 0)
    {
        m_table->setRowCount(1);
        auto *emptyItem = new QTableWidgetItem(tr_("plink.empty"));
        emptyItem->setFlags(Qt::NoItemFlags);
        m_table->setItem(0, 0, emptyItem);
        m_table->setSpan(0, 0, 1, 2);
    }
}

static long selectedLinkId(QTableWidget *table)
{
    auto items = table->selectedItems();

    if (items.isEmpty())
        return -1;

    QTableWidgetItem *first = items.first();
    QTableWidgetItem *idItem = table->item(first->row(), 0);

    if (!idItem)
        return -1;

    bool ok = false;
    long id = idItem->data(Qt::UserRole).toLongLong(&ok);

    return ok ? id : -1;
}

void ParentLinkPage::onApprove()
{
    long id = selectedLinkId(m_table);

    if (id < 0)
    {
        QMessageBox::information(this, "JustInTime", tr_("plink.select_row"));
        return;
    }

    char err[256] = {0};

    if (parentlink_approve(id, err, sizeof(err)))
    {
        QMessageBox::information(this, "JustInTime", tr_("plink.approved_msg"));
        refresh();
    }
    else
    {
        QMessageBox::warning(this, "JustInTime", QString::fromUtf8(err));
    }
}

void ParentLinkPage::onRevoke()
{
    long id = selectedLinkId(m_table);

    if (id < 0)
    {
        QMessageBox::information(this, "JustInTime", tr_("plink.select_row"));
        return;
    }

    if (
        QMessageBox::question(
            this, "JustInTime",
            tr_("plink.revoke_confirm")
        ) != QMessageBox::Yes
    )
        return;

    char err[256] = {0};

    if (parentlink_revoke(id, err, sizeof(err)))
    {
        QMessageBox::information(this, "JustInTime", tr_("plink.revoked_msg"));
        refresh();
    }
    else
    {
        QMessageBox::warning(this, "JustInTime", QString::fromUtf8(err));
    }
}
