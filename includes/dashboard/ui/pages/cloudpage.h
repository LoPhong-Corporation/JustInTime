//
// cloudpage.h
// Thay #view-cloud: chọn khoảng ngày (week/month), bảng tổng theo app,
// biểu đồ cột theo ngày, bảng hoạt động gần đây (từ Supabase) - cần đăng
// nhập, tự hiện thông báo nếu chưa.
//
#ifndef UI_PAGES_CLOUDPAGE_H
#define UI_PAGES_CLOUDPAGE_H

#include <QWidget>

class QLabel;
class QTableWidget;
class QPushButton;

namespace jit::dash::ui {
class DailyBarsWidget;

class CloudPage : public QWidget
{
public:
    explicit CloudPage(QWidget* parent = nullptr);
    void refresh(); // gọi khi trang được hiện (cần runAsync -> KHÔNG gọi trong constructor)

private:
    QLabel* notice_;
    QPushButton* weekBtn_;
    QPushButton* monthBtn_;
    QString range_ = "week";
    QTableWidget* summaryTable_;
    DailyBarsWidget* dailyChart_;
    QTableWidget* recentTable_;

    void setRange(const QString& r);
    void loadSummary();
    void loadDaily();
    void loadRecent();
};

} // namespace jit::dash::ui

#endif
