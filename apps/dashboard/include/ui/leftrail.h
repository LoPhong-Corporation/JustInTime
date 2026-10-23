//
// leftrail.h
// "left rail" trong wireframe layout: dải icon dọc hẹp, luôn hiện, chọn 1
// trong N khu vực chính của app (độc quyền - giống radio). Dưới cùng là
// nút tài khoản riêng (avatar), không nằm trong nhóm chọn-1 ở trên.
//
#ifndef UI_LEFTRAIL_H
#define UI_LEFTRAIL_H

#include <QWidget>
#include <functional>
#include <vector>

class QPushButton;
class QButtonGroup;
class QVBoxLayout;

namespace jit::dash::ui {

class LeftRail : public QWidget
{
public:
    explicit LeftRail(QWidget* parent = nullptr);

    // glyph: 1 ký tự/emoji hiển thị trên nút (xem chú thích ở mainwindow.cpp
    // về lý do dùng glyph thay vì file icon). Trả về index (0-based) để
    // gọi setCurrentIndex() sau này.
    int addSection(const QString& glyph, const QString& tooltip);
    void setCurrentIndex(int index);
    int currentIndex() const { return currentIndex_; }

    // Nút tài khoản riêng ở đáy rail - glyph đổi được (vd chữ cái đầu
    // email khi đã đăng nhập) vì không thuộc nhóm chọn-1 ở trên.
    void setAccountGlyph(const QString& glyph, const QString& tooltip);

    std::function<void(int)> onSectionSelected;
    std::function<void()> onAccountClicked;
    std::function<void()> onSettingsClicked;

private:
    QButtonGroup* group_;
    QVBoxLayout* layout_; // lưu thẳng con trỏ - KHÔNG dùng qobject_cast(this->layout())
                          // để lấy lại nó (xem chú thích trong .cpp)
    std::vector<QPushButton*> buttons_;
    QPushButton* settingsBtn_;
    QPushButton* accountBtn_;
    int currentIndex_ = -1;
};

} // namespace jit::dash::ui

#endif
