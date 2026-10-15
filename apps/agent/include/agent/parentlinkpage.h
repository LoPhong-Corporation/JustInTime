// parentlinkpage.h
//
// Trước đây là parentlinkdialog.h (QDialog) - nay là trang "Supervised
// by" trong ControlPanelWindow, dành cho vai trò con (agent). Vẫn giữ
// nguyên nguyên tắc MINH BẠCH: liệt kê ĐẦY ĐỦ mọi phụ huynh đang xin/
// đã được xem máy này, người dùng luôn thấy rõ và thu hồi được bất kỳ
// lúc nào.
#pragma once

#include <QWidget>

class QTableWidget;

class ParentLinkPage : public QWidget
{
    Q_OBJECT

public:
    explicit ParentLinkPage(QWidget *parent = nullptr);

    void refresh();

private slots:
    void onApprove();
    void onRevoke();

private:
    QTableWidget *m_table;
};
