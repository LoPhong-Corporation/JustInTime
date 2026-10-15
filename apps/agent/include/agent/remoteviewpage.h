// remoteviewpage.h
// Trước đây là remoteviewdialog.h (QDialog) - giờ nhúng trong
// ControlPanelWindow, logic giữ nguyên y hệt.
#pragma once

#include <QWidget>

class QCheckBox;
class QSpinBox;
class QLineEdit;
class QLabel;

class RemoteViewPage : public QWidget
{
    Q_OBJECT

public:
    explicit RemoteViewPage(QWidget *parent = nullptr);

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
