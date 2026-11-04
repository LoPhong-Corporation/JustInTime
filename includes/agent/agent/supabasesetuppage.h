// supabasesetuppage.h
// Trước đây là supabasesetupdialog.h (QDialog) - nay là 1 trang
// "nâng cao" trong ControlPanelWindow.
#pragma once

#include <QWidget>

class QLineEdit;

class SupabaseSetupPage : public QWidget
{
    Q_OBJECT

public:
    explicit SupabaseSetupPage(QWidget *parent = nullptr);

private slots:
    void onSave();

private:
    QLineEdit *m_urlEdit;
    QLineEdit *m_keyEdit;
};
