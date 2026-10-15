//
// devicespage.h
// Thay #view-devices: danh sách thiết bị khác của cùng tài khoản (chip),
// chọn 1 thiết bị -> xem hội thoại (thread) + gửi tin/chia sẻ số liệu máy.
//
#ifndef UI_PAGES_DEVICESPAGE_H
#define UI_PAGES_DEVICESPAGE_H

#include "dashconfig.h"
#include "sysstats.h"

#include <QWidget>

class QLabel;
class QListWidget;
class QLineEdit;
class QPushButton;

namespace jit::dash::ui {

class DevicesPage : public QWidget
{
public:
    DevicesPage(Config cfg, Collector* collector, QWidget* parent = nullptr);
    void refresh();

private:
    Config cfg_;
    Collector* collector_;
    QLabel* notice_;
    QListWidget* deviceList_;
    QListWidget* threadList_;
    QLineEdit* messageInput_;
    QPushButton* sendBtn_;
    QPushButton* shareStatsBtn_;
    QString selectedDevice_;

    void loadDevices();
    void selectDevice(const QString& deviceId);
    void loadThread();
    void sendMessage();
    void shareStats();
};

} // namespace jit::dash::ui

#endif
