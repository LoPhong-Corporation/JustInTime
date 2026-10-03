//
// localpage.h
// Thay #view-local: tổng hợp thời gian dùng app (mọi lúc, đọc SQLite cục
// bộ - không cần đăng nhập) dưới dạng bảng + biểu đồ cột ngang, và bảng
// hoạt động gần đây. KHÔNG có khối "AI Insights"/"Chatbot" của bản gốc -
// aiinsights.go chưa được cổng hoá (xem BAO_CAO_UI_CPP.md); 1 dòng ghi chú
// thay chỗ đó.
//
#ifndef UI_PAGES_LOCALPAGE_H
#define UI_PAGES_LOCALPAGE_H

#include "localdb.h"

#include <QWidget>

class QTableWidget;
class QLabel;

namespace jit::dash::ui {

class LocalPage : public QWidget
{
public:
    LocalPage(LocalDB* db, QWidget* parent = nullptr);
    void refresh();

private:
    LocalDB* db_;
    QLabel* noDbNotice_;
    QTableWidget* usageTable_;
    QTableWidget* recentTable_;
};

} // namespace jit::dash::ui

#endif
