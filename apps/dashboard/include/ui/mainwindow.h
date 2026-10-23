//
// mainwindow.h
// Cửa sổ chính, theo layout chuẩn 5 vùng (xem chú thích đầu mainwindow.cpp
// cho sơ đồ ASCII + lý do ghép từng trang vào từng vùng):
//   left rail (LeftRail) - sidebar (MachinesSidebar) - tabs (QTabWidget,
//   nội dung đổi theo rail) - right panel (RightPanel) - bottom bar
//   (QStatusBar, kế thừa từ QMainWindow).
//
#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include "dashconfig.h"
#include "localdb.h"
#include "sysstats.h"

#include <QMainWindow>
#include <memory>
#include <vector>

class QTabWidget;
class QTimer;

namespace jit::dash::ui {

class LeftRail;
class MachinesSidebar;
class RightPanel;
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

    // Chuyển sang khu vực rail thứ `index` (0=Home..3=Family) - dùng cho
    // test tự động (screenshot driver) và tiện nếu sau này cần "deep
    // link" mở thẳng 1 khu vực từ dòng lệnh/tray.
    void selectSection(int index);

private:
    Config cfg_;
    std::unique_ptr<LocalDB> db_;
    Collector collector_;

    LeftRail* rail_;
    MachinesSidebar* sidebar_;
    QTabWidget* centerTabs_;
    RightPanel* rightPanel_;
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

    // 1 "section" = 1 icon trên rail = N tab trong centerTabs_. Lưu con
    // trỏ + nhãn tab để populate lại centerTabs_ mỗi lần đổi section
    // (QTabWidget không sở hữu/huỷ widget khi removeTab(), nên đổi qua
    // lại không mất trạng thái từng trang - xem buildSections()).
    struct Section { std::vector<QWidget*> pages; std::vector<QString> labels; };
    std::vector<Section> sections_;

    void buildUi();
    void buildSections();
    void showSection(int index);
    void updateAccountGlyph();
    void onAccountClicked();
    void openSettings();
    void exportCsv();
    void printReport();
    void pushHeartbeat();
};

} // namespace jit::dash::ui

#endif
