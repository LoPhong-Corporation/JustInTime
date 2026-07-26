// parentlinkdialog.cpp

#include "parentlinkdialog.h"

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
}

ParentLinkDialog::ParentLinkDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Được giám sát bởi...");
    setMinimumWidth(520);

    auto *note = new QLabel(
        "The list below shows ALL parent accounts that are requesting or have permission "
        "to view activity on this device, including any pending invites. "
        "You can revoke access at any time.",
        this
    );
    note->setWordWrap(true);
    note->setStyleSheet("color: #7e9ac0; font-size: 11px;");

    m_table = new QTableWidget(this);
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({"Parent's email", "Status"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto *approveBtn = new QPushButton("Approve", this);
    auto *revokeBtn  = new QPushButton("Revoke", this);
    auto *closeBtn   = new QPushButton("Close", this);

    connect(approveBtn, &QPushButton::clicked, this, &ParentLinkDialog::onApprove);
    connect(revokeBtn, &QPushButton::clicked, this, &ParentLinkDialog::onRevoke);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(approveBtn);
    btnRow->addWidget(revokeBtn);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(note);
    mainLayout->addWidget(m_table);
    mainLayout->addLayout(btnRow);

    resize(560, 360);

    refresh();
}

void ParentLinkDialog::refresh()
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
            statusText = "Waiting for you to agree";
        else if (strcmp(links[i].status, "approved") == 0)
            statusText = "Agreed - being viewed";
        else
            statusText = "Revoked";

        auto *statusItem = new QTableWidgetItem(statusText);

        m_table->setItem(i, 0, emailItem);
        m_table->setItem(i, 1, statusItem);
    }

    if (count == 0)
    {
        m_table->setRowCount(1);
        auto *emptyItem = new QTableWidgetItem("No parents have linked with this account yet.");
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

void ParentLinkDialog::onApprove()
{
    long id = selectedLinkId(m_table);

    if (id < 0)
    {
        QMessageBox::information(this, "JustInTime", "Select one row from the list first.");
        return;
    }

    char err[256] = {0};

    if (parentlink_approve(id, err, sizeof(err)))
    {
        QMessageBox::information(this, "JustInTime", "Already agreed to let this parent see the machine's activity.");
        refresh();
    }
    else
    {
        QMessageBox::warning(this, "JustInTime", QString::fromUtf8(err));
    }
}

void ParentLinkDialog::onRevoke()
{
    long id = selectedLinkId(m_table);

    if (id < 0)
    {
        QMessageBox::information(this, "JustInTime", "Select one row from the list first.");
        return;
    }

    if (
        QMessageBox::question(
            this, "JustInTime",
            "Revoke this parent's viewing rights? They won't be able to see the device's activity anymore."
        ) != QMessageBox::Yes
    )
        return;

    char err[256] = {0};

    if (parentlink_revoke(id, err, sizeof(err)))
    {
        QMessageBox::information(this, "JustInTime", "Revoked");
        refresh();
    }
    else
    {
        QMessageBox::warning(this, "JustInTime", QString::fromUtf8(err));
    }
}
