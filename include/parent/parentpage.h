// parentpage.h
//
// Trước đây là parentdialog.h (QDialog) - nay là trang "Family" trong
// ControlPanelWindow, dành cho vai trò phụ huynh: gửi lời mời giám sát
// tới email của con, xem danh sách các con đã liên kết (mọi trạng
// thái), và với con đã "approved" - đặt giới hạn thời gian/chặn app
// cụ thể.
//
// Không có bất kỳ điều khiển trực tiếp nào lên máy con - chỉ ghi
// "luật" (app_limits) lên Supabase, máy con tự đọc và tự thực thi
// (xem core/activity.c: activity_check_limits()).
#pragma once

#include <QWidget>
#include <QString>

class QTableWidget;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QLabel;

class ParentPage : public QWidget
{
    Q_OBJECT

public:
    explicit ParentPage(QWidget *parent = nullptr);

    void refresh();

private slots:
    void onInvite();
    void onChildSelectionChanged();
    void onSaveLimit();
    void onDeleteLimit();

private:
    void refreshChildren();
    void refreshLimits();
    QString selectedChildUserId() const;

    QLineEdit    *m_inviteEmail;
    QTableWidget *m_childrenTable;

    QLabel       *m_limitsHeader;
    QTableWidget *m_limitsTable;
    QLineEdit    *m_processName;
    QSpinBox     *m_dailyMinutes;
    QCheckBox    *m_noLimitCheckbox;
    QCheckBox    *m_blockCheckbox;
};
