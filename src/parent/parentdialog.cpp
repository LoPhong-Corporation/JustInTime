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
}

ParentDialog::ParentDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Parent Dashboard");
    setMinimumWidth(640);

    // ---------------- Mời con ----------------

    auto *inviteNote = new QLabel(
        "Nhập email tài khoản JustInTime của con. Con sẽ thấy lời mời này "
        "trong mục \"Được giám sát bởi...\" và phải chủ động đồng ý thì bạn "
        "mới xem được hoạt động của con.",
        this
    );
    inviteNote->setWordWrap(true);
    inviteNote->setStyleSheet("color: #7e9ac0; font-size: 11px;");

    m_inviteEmail = new QLineEdit(this);
    m_inviteEmail->setPlaceholderText("email-cua-con@example.com");

    auto *inviteBtn = new QPushButton("Gửi lời mời", this);
    connect(inviteBtn, &QPushButton::clicked, this, &ParentDialog::onInvite);

    auto *inviteRow = new QHBoxLayout;
    inviteRow->addWidget(m_inviteEmail);
    inviteRow->addWidget(inviteBtn);

    auto *inviteBox = new QGroupBox("Mời con mới", this);
    auto *inviteBoxLayout = new QVBoxLayout(inviteBox);
    inviteBoxLayout->addWidget(inviteNote);
    inviteBoxLayout->addLayout(inviteRow);

    // ---------------- Danh sách con ----------------

    m_childrenTable = new QTableWidget(this);
    m_childrenTable->setColumnCount(2);
    m_childrenTable->setHorizontalHeaderLabels({"Email con", "Trạng thái"});
    m_childrenTable->horizontalHeader()->setStretchLastSection(true);
    m_childrenTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_childrenTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_childrenTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(
        m_childrenTable, &QTableWidget::itemSelectionChanged,
        this, &ParentDialog::onChildSelectionChanged
    );

    auto *childrenBox = new QGroupBox("Các con đã liên kết", this);
    auto *childrenBoxLayout = new QVBoxLayout(childrenBox);
    childrenBoxLayout->addWidget(m_childrenTable);

    // ---------------- Giới hạn app ----------------

    m_limitsHeader = new QLabel("Chọn 1 con đã \"Đã đồng ý\" ở trên để xem/đặt giới hạn.", this);
    m_limitsHeader->setStyleSheet("color: #7e9ac0; font-size: 11px;");

    m_limitsTable = new QTableWidget(this);
    m_limitsTable->setColumnCount(3);
    m_limitsTable->setHorizontalHeaderLabels({"Ứng dụng", "Giới hạn/ngày", "Trạng thái"});
    m_limitsTable->horizontalHeader()->setStretchLastSection(true);
    m_limitsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_limitsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_limitsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_processName = new QLineEdit(this);
    m_processName->setPlaceholderText("vd: RobloxPlayerBeta.exe");

    m_dailyMinutes = new QSpinBox(this);
    m_dailyMinutes->setRange(1, 1440);
    m_dailyMinutes->setValue(60);
    m_dailyMinutes->setSuffix(" phút/ngày");

    m_noLimitCheckbox = new QCheckBox("Không giới hạn thời gian", this);
    m_blockCheckbox   = new QCheckBox("Chặn hẳn (không cho mở app này)", this);

    connect(m_noLimitCheckbox, &QCheckBox::toggled, m_dailyMinutes, &QSpinBox::setDisabled);
    connect(m_blockCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        m_dailyMinutes->setDisabled(checked || m_noLimitCheckbox->isChecked());
        m_noLimitCheckbox->setDisabled(checked);
    });

    auto *limitForm = new QFormLayout;
    limitForm->addRow("Tên tiến trình:", m_processName);
    limitForm->addRow("Giới hạn:", m_dailyMinutes);
    limitForm->addRow(QString(), m_noLimitCheckbox);
    limitForm->addRow(QString(), m_blockCheckbox);

    auto *saveLimitBtn   = new QPushButton("Lưu giới hạn", this);
    auto *deleteLimitBtn = new QPushButton("Xoá giới hạn đã chọn", this);

    connect(saveLimitBtn, &QPushButton::clicked, this, &ParentDialog::onSaveLimit);
    connect(deleteLimitBtn, &QPushButton::clicked, this, &ParentDialog::onDeleteLimit);

    auto *limitBtnRow = new QHBoxLayout;
    limitBtnRow->addWidget(saveLimitBtn);
    limitBtnRow->addWidget(deleteLimitBtn);
    limitBtnRow->addStretch();

    auto *limitsBox = new QGroupBox("Giới hạn ứng dụng", this);
    auto *limitsBoxLayout = new QVBoxLayout(limitsBox);
    limitsBoxLayout->addWidget(m_limitsHeader);
    limitsBoxLayout->addWidget(m_limitsTable);
    limitsBoxLayout->addLayout(limitForm);
    limitsBoxLayout->addLayout(limitBtnRow);

    // ---------------- Tổng thể ----------------

    auto *closeBtn = new QPushButton("Đóng", this);
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
            statusText = "Đang chờ con đồng ý";
        else if (strcmp(links[i].status, "approved") == 0)
            statusText = "Đã đồng ý - đang xem được";
        else
            statusText = "Đã bị thu hồi";

        m_childrenTable->setItem(i, 0, emailItem);
        m_childrenTable->setItem(i, 1, new QTableWidgetItem(statusText));
    }

    if (count == 0)
    {
        m_childrenTable->setRowCount(1);
        auto *emptyItem = new QTableWidgetItem("Chưa mời con nào. Dùng ô phía trên để gửi lời mời đầu tiên.");
        emptyItem->setFlags(Qt::NoItemFlags);
        m_childrenTable->setItem(0, 0, emptyItem);
        m_childrenTable->setSpan(0, 0, 1, 2);
    }

    m_limitsTable->setRowCount(0);
    m_limitsHeader->setText("Chọn 1 con đã \"Đã đồng ý\" ở trên để xem/đặt giới hạn.");
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
        m_limitsHeader->setText("Chọn 1 con đã \"Đã đồng ý\" ở trên để xem/đặt giới hạn.");
        return;
    }

    QByteArray childIdUtf8 = childId.toUtf8();

    AppLimit limits[MAX_LIMITS];
    int count = applimits_list_for_child(childIdUtf8.constData(), limits, MAX_LIMITS);

    m_limitsHeader->setText(
        count > 0
            ? QString("%1 giới hạn đang áp dụng. Nếu con chưa đồng ý (\"pending\"), giới hạn sẽ không tải/lưu được.").arg(count)
            : "Chưa có giới hạn nào cho con này. Điền form bên dưới để thêm."
    );

    m_limitsTable->setRowCount(count);

    for (int i = 0; i < count; i++)
    {
        auto *nameItem = new QTableWidgetItem(QString::fromUtf8(limits[i].process_name));
        nameItem->setData(Qt::UserRole, static_cast<qlonglong>(limits[i].id));

        QString limitText = limits[i].blocked
            ? "-"
            : (limits[i].daily_limit_sec >= 0
                   ? QString("%1 phút/ngày").arg(limits[i].daily_limit_sec / 60)
                   : "Không giới hạn");

        QString statusText = limits[i].blocked ? "Bị chặn hẳn" : "Cho phép (có giới hạn)";

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
        QMessageBox::information(this, "JustInTime", "Nhập email của con trước.");
        return;
    }

    QByteArray emailUtf8 = email.toUtf8();
    char err[300] = {0};

    if (parentlink_invite_child(emailUtf8.constData(), err, sizeof(err)))
    {
        QMessageBox::information(
            this, "JustInTime",
            "Đã gửi lời mời. Con cần vào mục \"Được giám sát bởi...\" trên máy của con để đồng ý."
        );
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
        QMessageBox::information(this, "JustInTime", "Chọn 1 con trong danh sách phía trên trước.");
        return;
    }

    QString processName = m_processName->text().trimmed();

    if (processName.isEmpty())
    {
        QMessageBox::information(this, "JustInTime", "Nhập tên tiến trình (vd: RobloxPlayerBeta.exe).");
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
        QMessageBox::information(this, "JustInTime", "Đã lưu giới hạn.");
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
        QMessageBox::information(this, "JustInTime", "Chọn 1 giới hạn trong danh sách trước.");
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
