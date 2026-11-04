//
// chartwidget.h
// Thay cho Chart.js (canvas + CDN) trong index.html: 1 widget QPainter vẽ
// 1 hoặc nhiều đường lịch sử theo thời gian. Dùng lại cho mọi biểu đồ
// "history" (CPU/RAM/Disk/Network) và sparkline nhỏ trong stat-card - chỉ
// khác nhau ở kích thước và có hiện lưới/nhãn trục hay không.
//
#ifndef UI_CHARTWIDGET_H
#define UI_CHARTWIDGET_H

#include <QWidget>
#include <QColor>
#include <QString>
#include <deque>
#include <vector>

namespace jit::dash::ui {

class ChartWidget : public QWidget
{
public:
    explicit ChartWidget(QWidget* parent = nullptr);

    // Số điểm tối đa giữ lại (mặc định 60 - khớp lịch sử 60 mẫu ~1 giây/mẫu
    // của dashboard.js gốc).
    void setMaxPoints(int n);

    // Cấu hình 1 đường: gọi 1 lần cho mỗi series trước khi push dữ liệu.
    void addSeries(const QString& name, const QColor& color);

    // Thêm 1 điểm mới vào series thứ `index` (0-based, theo thứ tự addSeries).
    void pushValue(int seriesIndex, double value);

    // Chế độ sparkline: ẩn lưới/trục/chú giải, nét mảnh, dùng cho canvas nhỏ
    // trong stat-card. Mặc định false (chart đầy đủ có lưới + chú giải).
    void setSparklineMode(bool on);

    // Cận trên cố định cho trục Y (vd 100 cho %); <=0 = tự co giãn theo dữ liệu.
    void setFixedMax(double max);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    struct Series { QString name; QColor color; std::deque<double> values; };
    std::vector<Series> series_;
    int maxPoints_ = 60;
    bool sparkline_ = false;
    double fixedMax_ = -1;
};

} // namespace jit::dash::ui

#endif
