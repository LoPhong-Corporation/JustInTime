//
// autostart.h
// Cổng từ internal/autostart/autostart_windows.go: đăng ký dashboard.exe
// (--tray) chạy cùng lúc đăng nhập Windows, qua registry Run key RIÊNG
// (giá trị "JustInTimeDashboard") - tách biệt hoàn toàn với autostart của
// agent (settings_apply_autostart trong agent), bật/tắt cái này không
// đụng tới cái kia.
//
#ifndef DASHBOARD_AUTOSTART_H
#define DASHBOARD_AUTOSTART_H

namespace jit::dash {

bool setAutostart(bool enabled);
bool isAutostartEnabled();

} // namespace jit::dash

#endif
