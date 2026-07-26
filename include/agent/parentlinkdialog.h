// parentlinkdialog.h
//
// Dialog MINH BẠCH cho vai trò con (agent) - "Được giám sát
// bởi...". Liệt kê ĐẦY ĐỦ mọi phụ huynh đang xin/đã được xem
// máy này (kể cả các lời mời đang chờ), để người dùng luôn biết
// rõ và có thể thu hồi bất kỳ lúc nào.
#pragma once

#include <QDialog>

class QTableWidget;

class ParentLinkDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ParentLinkDialog(QWidget *parent = nullptr);

private slots:
    void onApprove();
    void onRevoke();

private:
    void refresh();

    QTableWidget *m_table;
};
