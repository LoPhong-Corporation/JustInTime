//
// dashsession.h
// Cổng từ internal/dashsession: phiên đăng nhập RIÊNG của dashboard (khác
// session.dat của agent C++) - lưu %APPDATA%\JustInTime\dashboard_session.dat,
// mã hoá DPAPI theo user Windows hiện tại, để dashboard chạy nền trong tray
// không bắt đăng nhập lại sau mỗi lần khởi động máy.
//
#ifndef DASHBOARD_SESSION_H
#define DASHBOARD_SESSION_H

#include <optional>
#include <string>

namespace jit::dash {

struct DashSession {
    std::string accessToken;
    std::string refreshToken;
    std::string userId;
    std::string email;
};

bool saveDashSession(const DashSession& s);
std::optional<DashSession> loadDashSession();
void clearDashSession();

} // namespace jit::dash

#endif
