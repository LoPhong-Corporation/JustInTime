//
// gaugewidget.h
// Thay "radial-gauge" canvas (gaugeCpuTab/gaugeRamTab) trong index.html:
// đồng hồ đo hình cung nửa vòng tròn, tô màu theo ngưỡng cảnh báo.
//
#ifndef UI_GAUGEWIDGET_H
#define UI_GAUGEWIDGET_H

#include <QWidget>

namespace jit::dash::ui {

class GaugeWidget : public QWidget
{
public:
    explicit GaugeWidget(QWidget* parent = nullptr);

    void setValue(double percent);      // 0..100
    void setThreshold(double percent);  // từ đây trở lên tô màu cảnh báo (đỏ)

protected:
    void paintEvent(QPaintEvent*) override;

private:
    double value_ = 0;
    double threshold_ = 85;
};

} // namespace jit::dash::ui

#endif
