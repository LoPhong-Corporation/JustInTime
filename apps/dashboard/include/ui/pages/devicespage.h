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

    // Chọn sẵn 1 thiết bị theo device_id - dùng khi điều hướng từ
    // MachinesSidebar (bấm 1 máy ở sidebar -> nhảy sang đây, chọn sẵn
    // khung chat với máy đó). Nếu danh sách chưa tải xong hoặc không có
    // thiết bị khớp, chỉ ghi nhớ để áp dụng ngay khi loadDevices() xong.
    void selectDeviceById(const QString& deviceId);

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
    QString pendingSelectDeviceId_; // chờ loadDevices() xong rồi mới áp dụng

    void loadDevices();
    void selectDevice(const QString& deviceId);
    void loadThread();
    void sendMessage();
    void shareStats();
};

} // namespace jit::dash::ui

#endif
