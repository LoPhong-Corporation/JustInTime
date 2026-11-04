//
// machinessidebar.h
// "sidebar" trong wireframe layout: ô lọc + danh sách máy (cùng tài
// khoản) - LUÔN hiện bất kể đang ở khu vực nào của rail. Chọn 1 máy ->
// callback (MainWindow dùng để nhảy sang tab Devices và chọn sẵn máy đó
// cho khung chat).
//
#ifndef UI_MACHINESSIDEBAR_H
#define UI_MACHINESSIDEBAR_H

#include "dashconfig.h"

#include <QWidget>
#include <functional>
#include <vector>

class QLineEdit;
class QListWidget;

namespace jit::dash::ui {

class MachinesSidebar : public QWidget
{
public:
    MachinesSidebar(Config cfg, QWidget* parent = nullptr); // rộng cố định 220px - xem .cpp

    // Có gọi mạng (runAsync) - KHÔNG gọi trong constructor, xem asyncutil.h.
    void refresh();

    std::function<void(QString deviceId)> onMachineSelected;

private:
    Config cfg_;
    QLineEdit* filterBox_;
    QListWidget* list_;

    struct Row { QString deviceId, hostname, status; };
    std::vector<Row> rows_;

    void applyFilter();
};

} // namespace jit::dash::ui

#endif
