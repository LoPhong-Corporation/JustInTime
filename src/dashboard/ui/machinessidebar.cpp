#include "ui/machinessidebar.h"
#include "ui/asyncutil.h"
#include "cloud.h"
#include "dashsession.h"

#include <QDateTime>
#include <QColor>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QVBoxLayout>

namespace jit::dash::ui {

MachinesSidebar::MachinesSidebar(Config cfg, QWidget* parent) : QWidget(parent), cfg_(std::move(cfg))
{
    setFixedWidth(220);
    setObjectName("machinesSidebar");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 10, 8, 10);
    layout->setSpacing(6);

    filterBox_ = new QLineEdit;
    filterBox_->setPlaceholderText("Filter machines...");
    filterBox_->setClearButtonEnabled(true);
    connect(filterBox_, &QLineEdit::textChanged, this, [this](const QString&) { applyFilter(); });
    layout->addWidget(filterBox_);

    list_ = new QListWidget;
    list_->setObjectName("machinesList");
    list_->setWordWrap(true); // tên máy dài không bị cắt mất, xuống dòng thay vì ẩn
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // wrap thay vì cuộn ngang
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        if (onMachineSelected)
            onMachineSelected(it->data(Qt::UserRole).toString());
    });
    layout->addWidget(list_, 1);
}

void MachinesSidebar::refresh()
{
    const auto sess = loadDashSession();
    if (!sess)
    {
        rows_.clear();
        list_->clear();
        list_->addItem("Log in to see machines.");
        return;
    }

    const std::string token = sess->accessToken;
    runAsync(this,
        [token]() { return cloud::Client(token).listHeartbeats(); },
        [this](std::vector<cloud::Client::DeviceHeartbeat>* hb, const std::exception* e) {
            rows_.clear();
            if (e) { list_->clear(); list_->addItem("Failed to load machines."); return; }

            const QDateTime now = QDateTime::currentDateTimeUtc();
            for (const auto& m : *hb)
            {
                const QDateTime lastSeen = QDateTime::fromString(QString::fromStdString(m.lastSeen), Qt::ISODate);
                const bool offline = !lastSeen.isValid() || lastSeen.secsTo(now) > 300;
                const bool critical = !offline && (m.cpuPercent >= 85 || m.ramPercent >= 85 || m.diskPercent >= 90);

                Row r;
                r.deviceId = QString::fromStdString(m.deviceId);
                r.hostname = QString::fromStdString(m.hostname.empty() ? m.deviceId : m.hostname);
                r.status = offline ? "offline" : (critical ? "critical" : "online");
                if (r.hostname.isEmpty()) r.hostname = r.deviceId;
                if (r.deviceId == QString::fromStdString(cfg_.deviceId))
                    r.hostname += " (this PC)";
                rows_.push_back(r);
            }
            applyFilter();
        });
}

void MachinesSidebar::applyFilter()
{
    const QString needle = filterBox_->text().trimmed().toLower();
    list_->clear();

    for (const auto& r : rows_)
    {
        if (!needle.isEmpty() && !r.hostname.toLower().contains(needle))
            continue;

        const QColor statusColor = r.status == "online" ? QColor("#38b26c")
            : (r.status == "critical" ? QColor("#df5260") : QColor("#87919c"));
        QPixmap statusPixmap(12, 12);
        statusPixmap.fill(Qt::transparent);
        QPainter painter(&statusPixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(statusColor);
        painter.drawEllipse(1, 1, 10, 10);

        auto* item = new QListWidgetItem(QIcon(statusPixmap), r.hostname);
        item->setData(Qt::UserRole, r.deviceId);
        list_->addItem(item);
    }

    if (list_->count() == 0 && !rows_.empty())
        list_->addItem("No machines match.");
}

} // namespace jit::dash::ui
