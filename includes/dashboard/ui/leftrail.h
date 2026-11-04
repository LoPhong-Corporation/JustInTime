//
// leftrail.h
// "left rail" trong wireframe layout: dải icon dọc hẹp, luôn hiện, chọn 1
// trong N khu vực chính của app (độc quyền - giống radio). Dưới cùng là
// nút tài khoản riêng (avatar), không nằm trong nhóm chọn-1 ở trên.
//
#ifndef UI_LEFTRAIL_H
#define UI_LEFTRAIL_H

#include <QWidget>
#include <QIcon>
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

    int addSection(const QIcon& icon, const QString& tooltip);
    void setCurrentIndex(int index);
    int currentIndex() const { return currentIndex_; }

    void setAccountGlyph(const QString& label, const QString& tooltip);

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
