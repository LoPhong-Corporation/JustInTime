//
// mainwindow.h
// Cửa sổ chính: sidebar điều hướng (thay <nav> trong index.html) + vùng
// nội dung QStackedWidget (thay các <section id="view-...">) + thanh trên
// cùng hiện trạng thái đăng nhập.
//
#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include "dashconfig.h"
#include "localdb.h"
#include "sysstats.h"

#include <QMainWindow>
#include <memory>

class QListWidget;
class QStackedWidget;
class QLabel;
class QPushButton;
class QTimer;

namespace jit::dash::ui {

class OverviewPage;
class CpuPage;
class RamPage;
class DiskPage;
class NetworkPage;
class LocalPage;
class CloudPage;
class DevicesPage;
class FamilyPage;

class MainWindow : public QMainWindow
{
public:
    MainWindow(Config cfg, std::unique_ptr<LocalDB> db);

    // Chuyển sang trang thứ `row` trong sidebar (0=Overview..8=Family).
    // Dùng cho test tự động (screenshot driver) - cũng tiện nếu sau này
    // cần "deep link" mở thẳng 1 trang từ dòng lệnh/tray.
    void selectPage(int row);

private:
    Config cfg_;
    std::unique_ptr<LocalDB> db_;
    Collector collector_;

    QListWidget* sidebar_;
    QStackedWidget* stack_;
    QLabel* sessionLabel_;
    QPushButton* loginBtn_;
    QTimer* liveTimer_;
    QTimer* heartbeatTimer_;

    OverviewPage* overviewPage_;
    CpuPage* cpuPage_;
    RamPage* ramPage_;
    DiskPage* diskPage_;
    NetworkPage* networkPage_;
    LocalPage* localPage_;
    CloudPage* cloudPage_;
    DevicesPage* devicesPage_;
    FamilyPage* familyPage_;

    void buildUi();
    void updateSessionLabel();
    void onSidebarRowChanged(int row);
    void onLoginLogoutClicked();
    void openSettings();
    void exportCsv();
    void printReport();
    void pushHeartbeat();
};

} // namespace jit::dash::ui

#endif
