// remoteviewdialog.h
#pragma once

#include <QDialog>

class QCheckBox;
class QSpinBox;
class QLineEdit;
class QLabel;

class RemoteViewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RemoteViewDialog(QWidget *parent = nullptr);

private slots:
    void onSave();
    void onRegenerateToken();
    void onCopyUrl();

private:
    void refreshUrlPreview();

    QCheckBox *m_enabledCheck;
    QSpinBox  *m_portSpin;
    QLineEdit *m_tokenEdit;
    QLabel    *m_urlPreview;
};
