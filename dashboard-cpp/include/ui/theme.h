//
// theme.h
// Bảng màu tối dùng chung cho toàn app - CÙNG bảng màu với style.css của
// bản web gốc (nền #0a1420/#0f1c2e, viền #1c3a5e, chữ #eef4fb/#dce8f7,
// nhấn #5aa9ff) để giao diện Qt giữ đúng "cảm giác" của dashboard web cũ.
// Áp dụng 1 lần lúc khởi động qua QApplication::setStyleSheet() - từng
// widget lẻ (ChartWidget, GaugeWidget...) tự vẽ nền riêng bằng QPainter
// nên không cần lặp lại các màu này.
//
#ifndef UI_THEME_H
#define UI_THEME_H

#include <QString>

namespace jit::dash::ui {

inline QString darkThemeStyleSheet()
{
    return R"(
        QMainWindow, QDialog, QWidget { background:#000000; color:#dce8f7; }
        QLabel { color:#dce8f7; }
        QPushButton {
            background:#132436; color:#eef4fb; border:1px solid #1c3a5e;
            border-radius:5px; padding:6px 12px;
        }
        QPushButton:hover { background:#1c3a5e; }
        QPushButton:checked { background:#5aa9ff; color:#0a1420; font-weight:600; }
        QPushButton:disabled { color:#4a6584; background:#0f1c2e; }
        QLineEdit, QComboBox, QSpinBox {
            background:#0f1c2e; color:#eef4fb; border:1px solid #1c3a5e;
            border-radius:4px; padding:4px 6px;
        }
        QTableWidget, QListWidget {
            background:#0a1420; color:#dce8f7; border:1px solid #1c3a5e;
            gridline-color:#1c3a5e;
        }
        QHeaderView::section {
            background:#132436; color:#eef4fb; border:1px solid #1c3a5e; padding:4px;
        }
        QTableWidget::item:selected, QListWidget::item:selected { background:#1c3a5e; }
        QTabWidget::pane { border:1px solid #1c3a5e; }
        QTabBar::tab { background:#0f1c2e; color:#dce8f7; padding:6px 12px; }
        QTabBar::tab:selected { background:#1c3a5e; color:#eef4fb; }
        QGroupBox {
            border:1px solid #1c3a5e; border-radius:6px; margin-top:10px;
            color:#eef4fb; font-weight:600; padding-top:6px;
        }
        QGroupBox::title { subcontrol-origin:margin; left:10px; padding:0 4px; }
        QScrollBar:vertical, QScrollBar:horizontal { background:#0a1420; }
        QStatusBar { background:#0a1420; color:#7e9ac0; }
        QMenu { background:#132436; color:#eef4fb; border:1px solid #1c3a5e; }
        QMenu::item:selected { background:#1c3a5e; }
    )";
}

} // namespace jit::dash::ui

#endif
