//
// familypage.h
// Thay #view-family: 2 vai trò trong cùng 1 trang (giống bản web) -
// "Con của tôi" (parent: mời/duyệt/thu hồi/phân quyền/xem tổng kết + đặt
// giới hạn app) và "Phụ huynh của tôi" (child: danh sách ai đang theo dõi
// mình).
//
#ifndef UI_PAGES_FAMILYPAGE_H
#define UI_PAGES_FAMILYPAGE_H

#include <QWidget>
#include <cstdint>

class QLabel;
class QLineEdit;
class QTableWidget;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QPushButton;

namespace jit::dash::ui {

class FamilyPage : public QWidget
{
public:
    explicit FamilyPage(QWidget* parent = nullptr);
    void refresh();

private:
    QLabel* notice_;

    // Con của tôi
    QLineEdit* inviteEmail_;
    QTableWidget* childrenTable_;
    QComboBox* childSelector_;
    QTableWidget* childSummaryTable_;
    QTableWidget* limitsTable_;
    QLineEdit* limitProcessName_;
    QSpinBox* limitMinutes_;
    QCheckBox* limitBlocked_;

    // Phụ huynh của tôi
    QTableWidget* parentsTable_;

    int64_t currentPermissionLevel_ = 0; // 0=full (mặc định lúc chưa tải xong), dùng để khoá nút Set/Delete nếu view_only

    void inviteChild();
    void loadChildren();
    void loadParents();
    void onChildSelected(int index);
    void loadChildSummaryAndLimits(const std::string& childId);
    void setLimit();
};

} // namespace jit::dash::ui

#endif
