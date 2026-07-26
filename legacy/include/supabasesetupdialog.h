// supabasesetupdialog.h
#pragma once

#include <QDialog>

class QLineEdit;

class SupabaseSetupDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SupabaseSetupDialog(QWidget *parent = nullptr);

private slots:
    void onSave();

private:
    QLineEdit *m_urlEdit;
    QLineEdit *m_keyEdit;
};
