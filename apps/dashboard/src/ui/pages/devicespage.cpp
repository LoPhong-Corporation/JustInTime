#include "ui/pages/devicespage.h"
#include "ui/asyncutil.h"
#include "cloud.h"
#include "dashsession.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace jit::dash::ui {

DevicesPage::DevicesPage(Config cfg, Collector* collector, QWidget* parent)
    : QWidget(parent), cfg_(std::move(cfg)), collector_(collector)
{
    auto* root = new QVBoxLayout(this);

    notice_ = new QLabel("Log in from the sidebar to message your other devices.");
    notice_->setStyleSheet("color:#f5a524; padding:8px;");
    notice_->setWordWrap(true);
    notice_->hide();
    root->addWidget(notice_);

    auto* split = new QHBoxLayout;

    auto* leftCol = new QVBoxLayout;
    leftCol->addWidget(new QLabel("Your devices"));
    deviceList_ = new QListWidget;
    deviceList_->setFixedWidth(220);
    connect(deviceList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        selectDevice(it->data(Qt::UserRole).toString());
    });
    leftCol->addWidget(deviceList_);
    split->addLayout(leftCol);

    auto* rightCol = new QVBoxLayout;
    rightCol->addWidget(new QLabel("Conversation"));
    threadList_ = new QListWidget;
    rightCol->addWidget(threadList_, 1);

    auto* sendRow = new QHBoxLayout;
    messageInput_ = new QLineEdit;
    messageInput_->setPlaceholderText("Type a message...");
    sendBtn_ = new QPushButton("Send");
    shareStatsBtn_ = new QPushButton("Share My Stats");
    connect(sendBtn_, &QPushButton::clicked, this, &DevicesPage::sendMessage);
    connect(messageInput_, &QLineEdit::returnPressed, this, &DevicesPage::sendMessage);
    connect(shareStatsBtn_, &QPushButton::clicked, this, &DevicesPage::shareStats);
    sendRow->addWidget(messageInput_, 1);
    sendRow->addWidget(sendBtn_);
    sendRow->addWidget(shareStatsBtn_);
    rightCol->addLayout(sendRow);

    split->addLayout(rightCol, 1);
    root->addLayout(split);

    sendBtn_->setEnabled(false);
    shareStatsBtn_->setEnabled(false);
    messageInput_->setEnabled(false);
}

void DevicesPage::refresh()
{
    if (!loadDashSession())
    {
        notice_->show();
        deviceList_->clear();
        threadList_->clear();
        return;
    }
    notice_->hide();
    loadDevices();
}

void DevicesPage::loadDevices()
{
    const std::string token = loadDashSession()->accessToken;

    runAsync(this,
        [token]() { return cloud::Client(token).listHeartbeats(); },
        [this](std::vector<cloud::Client::DeviceHeartbeat>* hb, const std::exception* e) {
            deviceList_->clear();
            if (e) return;
            for (const auto& m : *hb)
            {
                if (m.deviceId == cfg_.deviceId) continue; // không nhắn cho chính mình
                auto* item = new QListWidgetItem(QString::fromStdString(m.hostname.empty() ? m.deviceId : m.hostname));
                item->setData(Qt::UserRole, QString::fromStdString(m.deviceId));
                deviceList_->addItem(item);
            }
        });
}

void DevicesPage::selectDevice(const QString& deviceId)
{
    selectedDevice_ = deviceId;
    sendBtn_->setEnabled(true);
    shareStatsBtn_->setEnabled(true);
    messageInput_->setEnabled(true);
    loadThread();
}

void DevicesPage::loadThread()
{
    if (selectedDevice_.isEmpty()) return;
    const std::string token = loadDashSession()->accessToken;
    const std::string self = cfg_.deviceId, other = selectedDevice_.toStdString();

    runAsync(this,
        [token, self, other]() { return cloud::Client(token).thread(self, other, 100); },
        [this](std::vector<cloud::Client::Message>* msgs, const std::exception* e) {
            threadList_->clear();
            if (e) { threadList_->addItem("Failed to load: " + QString::fromStdString(e->what())); return; }
            for (const auto& m : *msgs)
            {
                const QString who = QString::fromStdString(m.senderDeviceId) == QString::fromStdString(cfg_.deviceId) ? "You" : "Them";
                QString text = m.kind == "data" ? "[shared stats]" : QString::fromStdString(m.payload);
                threadList_->addItem(QString("%1: %2").arg(who, text));
            }
            threadList_->scrollToBottom();
        });
}

void DevicesPage::sendMessage()
{
    const QString text = messageInput_->text().trimmed();
    if (text.isEmpty() || selectedDevice_.isEmpty()) return;

    const std::string token = loadDashSession()->accessToken;
    const std::string self = cfg_.deviceId, target = selectedDevice_.toStdString(), payload = text.toStdString();
    messageInput_->clear();

    runAsyncVoid(this,
        [token, self, target, payload]() {
            cloud::Client::Message m; m.senderDeviceId = self; m.targetDeviceId = target; m.kind = "message"; m.payload = payload;
            cloud::Client(token).sendMessage(m);
        },
        [this](const std::exception* e) { if (!e) loadThread(); });
}

void DevicesPage::shareStats()
{
    if (selectedDevice_.isEmpty()) return;
    const LiveStats live = collector_->live();
    const std::string token = loadDashSession()->accessToken;
    const std::string self = cfg_.deviceId, target = selectedDevice_.toStdString();

    nlohmann::json payload = {{"type", "stats_reply"}, {"cpu_percent", live.cpuPercent},
                                {"ram_percent", live.ramPercent}, {"disk_percent", live.diskPercent}};
    const std::string payloadStr = payload.dump();

    runAsyncVoid(this,
        [token, self, target, payloadStr]() {
            cloud::Client::Message m; m.senderDeviceId = self; m.targetDeviceId = target; m.kind = "data"; m.payload = payloadStr;
            cloud::Client(token).sendMessage(m);
        },
        [this](const std::exception* e) { if (!e) loadThread(); });
}

} // namespace jit::dash::ui
