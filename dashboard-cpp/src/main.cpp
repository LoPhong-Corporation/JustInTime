//
// main.cpp
//
// Điểm vào - Qt Widgets app THUẦN, không còn HTTP server/trình duyệt.
// Toàn bộ giao diện là MainWindow (Qt) thay cho index.html/dashboard.js;
// tray dùng QSystemTrayIcon (Qt) thay cho Win32 Shell_NotifyIcon thủ công
// trước đây - nhất quán, không cần tự viết wndproc.
//
#include "dashconfig.h"
#include "localdb.h"
#include "ui/mainwindow.h"
#include "ui/theme.h"

#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QStyle>
#include <QSystemTrayIcon>

#include <memory>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false); // đóng cửa sổ = ẩn xuống tray, không thoát app (giống agent)
    app.setApplicationName("JustInTime Dashboard");
    app.setStyleSheet(jit::dash::ui::darkThemeStyleSheet());

    const bool trayFlag = argc > 1 && std::string(argv[1]) == "--tray";

    jit::dash::Config cfg = jit::dash::load();
    auto db = jit::dash::LocalDB::open(cfg.localDbPathUtf8);
    // db == nullptr là bình thường lúc mới cài (agent chưa tạo DB lần nào) -
    // các trang liên quan tự hiện thông báo, không phải crash.

    auto* window = new jit::dash::ui::MainWindow(cfg, std::move(db));

    QSystemTrayIcon* tray = nullptr;
    if (QSystemTrayIcon::isSystemTrayAvailable())
    {
        tray = new QSystemTrayIcon(window->style()->standardIcon(QStyle::SP_ComputerIcon), &app);
        tray->setToolTip("JustInTime Dashboard");

        auto* menu = new QMenu;
        QObject::connect(menu->addAction("Open Dashboard"), &QAction::triggered, window, [window] {
            window->showNormal();
            window->raise();
            window->activateWindow();
        });
        menu->addSeparator();
        QObject::connect(menu->addAction("Exit"), &QAction::triggered, &app, &QApplication::quit);
        tray->setContextMenu(menu);

        QObject::connect(tray, &QSystemTrayIcon::activated, window, [window](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            {
                window->showNormal();
                window->raise();
                window->activateWindow();
            }
        });

        tray->show();
    }

    if (trayFlag)
        window->hide(); // tự khởi động cùng Windows: chỉ hiện icon khay, không bật cửa sổ ngay
    else
        window->show();

    return app.exec();
}
