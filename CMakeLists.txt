cmake_minimum_required(VERSION 3.28)
project(JustInTime LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Nếu CMake không tự tìm thấy Qt6, set CMAKE_PREFIX_PATH trỏ tới
# thư mục cài Qt, ví dụ:
#   -DCMAKE_PREFIX_PATH="C:/Qt/6.7.0/msvc2019_64" 
set(Qt6_DIR
    "C:/Qt/6.11.2/msvc2022_64/lib/cmake/Qt6"
)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

# ------------------------------------------------------------------
# Toàn bộ phần "core" (dữ liệu/mạng/đồng bộ) của app đã được chuyển
# hoàn toàn từ C sang C++ (RAII cho HANDLE/HINTERNET/sqlite3_stmt*,
# std::string/std::vector thay buffer cấp phát tay, std::mutex thay
# CRITICAL_SECTION tự quản...) - xem ghi chú "Đã CHUYỂN TỪ C SANG
# C++" ở đầu mỗi file .cpp trong src/shared/, src/agent/, src/core/
# để biết chi tiết từng thay đổi. Không còn bản C song song nữa;
# project trước đây có 2 chế độ build (STABLE/C và EXPERIMENTAL/C++)
# trong lúc chuyển đổi dần, giờ đã hợp nhất về 1 bản C++ duy nhất.
# ------------------------------------------------------------------

add_executable(JustInTime WIN32
        # ---- shared/ : dùng chung bởi cả vai trò agent (con) và parent ----
        src/shared/restclient.cpp
        include/shared/restclient.h
        src/shared/jsonutil.cpp
        include/shared/jsonutil.h
        src/shared/device.cpp
        include/shared/device.h
        src/shared/machines.cpp
        include/shared/machines.h
        src/shared/backup.cpp
        include/shared/backup.h
        src/shared/applimits.cpp
        include/shared/applimits.h
        src/shared/parentlink.cpp
        include/shared/parentlink.h
        src/shared/settings.cpp
        include/shared/settings.h
        src/shared/network.cpp
        include/shared/network.h
        src/shared/auth.cpp
        include/shared/auth.h
        src/shared/i18n.cpp
        include/shared/i18n.h
        src/shared/database.cpp
        include/shared/database.h
        include/shared/config.h
        include/shared/error_codes.h
        src/shared/log.cpp
        include/shared/log.h
        src/shared/httpclient.cpp
        include/shared/httpclient.h
        include/shared/strutil.h
        include/shared/paths.h
        include/agent/batchsend.h
        third_party/sqlite/sqlite3.c
        third_party/sqlite/sqlite3.h

        # ---- core/ : engine theo dõi hoạt động ----
        src/core/activity.cpp
        include/core/activity.h

        # ---- agent/ : vai trò "con" (tray UI, Control Panel, sync,
        # remote view, entrypoint) ----
        src/agent/main.cpp
        "include/agent/trayicon.h"
        src/agent/trayicon.cpp
        "include/agent/controlpanelwindow.h"
        src/agent/controlpanelwindow.cpp
        "include/agent/loginpage.h"
        src/agent/loginpage.cpp
        "include/agent/settingspage.h"
        src/agent/settingspage.cpp
        "include/agent/supabasesetuppage.h"
        src/agent/supabasesetuppage.cpp
        "include/agent/remoteviewpage.h"
        src/agent/remoteviewpage.cpp
        "include/agent/updatechecker.h"
        src/agent/updatechecker.cpp
        include/agent/remoteview.h
        src/agent/remoteview.cpp
        include/agent/sync.h
        src/agent/sync.cpp
        "include/agent/parentlinkpage.h"
        src/agent/parentlinkpage.cpp

        # ---- parent/ : vai trò "phụ huynh" (xem dữ liệu con + đặt giới hạn) ----
        "include/parent/parentpage.h"
        src/parent/parentpage.cpp

        # ---- dashboard/ : cửa sổ Dashboard (trước là dashboard-go, 1 tiến
        # trình HTTP server + trình duyệt riêng; giờ là Qt Widgets thuần,
        # SỐNG TRONG CÙNG tiến trình JustInTime.exe - mở từ tray, xem
        # TrayIcon::openDashboardWindow() trong agent/trayicon.cpp) ----
        include/dashboard/dashconfig.h
        src/dashboard/dashconfig.cpp
        include/dashboard/cloud.h
        src/dashboard/cloud.cpp
        include/dashboard/localdb.h
        src/dashboard/localdb.cpp
        include/dashboard/sysstats.h
        src/dashboard/sysstats.cpp
        include/dashboard/dashsettings.h
        src/dashboard/dashsettings.cpp
        include/dashboard/dashsession.h
        src/dashboard/dashsession.cpp
        include/dashboard/dashi18n.h
        src/dashboard/dashi18n.cpp
        include/dashboard/autostart.h
        src/dashboard/autostart.cpp

        include/dashboard/ui/asyncutil.h
        include/dashboard/ui/theme.h
        include/dashboard/ui/chartwidget.h
        src/dashboard/ui/chartwidget.cpp
        include/dashboard/ui/gaugewidget.h
        src/dashboard/ui/gaugewidget.cpp
        include/dashboard/ui/timelinewidget.h
        src/dashboard/ui/timelinewidget.cpp
        include/dashboard/ui/logindialog.h
        src/dashboard/ui/logindialog.cpp
        include/dashboard/ui/settingsdialog.h
        src/dashboard/ui/settingsdialog.cpp
        include/dashboard/ui/mainwindow.h
        src/dashboard/ui/mainwindow.cpp
        include/dashboard/ui/pages/overviewpage.h
        src/dashboard/ui/pages/overviewpage.cpp
        include/dashboard/ui/pages/metricpages.h
        src/dashboard/ui/pages/metricpages.cpp
        include/dashboard/ui/pages/localpage.h
        src/dashboard/ui/pages/localpage.cpp
        include/dashboard/ui/pages/cloudpage.h
        src/dashboard/ui/pages/cloudpage.cpp
        include/dashboard/ui/pages/devicespage.h
        src/dashboard/ui/pages/devicespage.cpp
        include/dashboard/ui/pages/familypage.h
        src/dashboard/ui/pages/familypage.cpp
)

include_directories(third_party)
include_directories(third_party/nlohmann)
include_directories(include)
include_directories(include/shared)
include_directories(include/core)
include_directories(include/agent)
include_directories(include/parent)
# Cho include BARE name ("cloud.h", "ui/chartwidget.h"...) dùng nội bộ
# giữa các file dashboard/ với nhau. Code NGOÀI dashboard/ (vd
# trayicon.cpp) include qua tiền tố "dashboard/..." (vd
# "dashboard/ui/mainwindow.h") để không bao giờ nhầm lẫn với header cùng
# tên bên agent (đã đổi tên 2 file có nguy cơ trùng: config.h ->
# dashconfig.h, i18n.h -> dashi18n.h - xem chú thích đầu 2 file đó).
include_directories(include/dashboard)

target_link_libraries(
        JustInTime
        Qt6::Widgets
        Qt6::Network
        psapi
        winhttp
        crypt32
        advapi32
        ws2_32
        wtsapi32
        pdh
        iphlpapi
        ntdll
)

# ------------------------------------------------------------------
# Dashboard (C++/Qt) giờ BUILD THẲNG VÀO JustInTime.exe (xem danh sách
# include/dashboard + src/dashboard ở trên) - không còn là tiến trình
# .exe riêng (dashboard-go) phải copy ra cạnh JustInTime.exe sau build
# nữa, nên bước copy trước đây ở đây đã được bỏ.
#
# dashboard-go/ (mã nguồn Go) vẫn còn trong repo làm tài liệu tham khảo
# lịch sử, nhưng không còn nằm trong đường build của JustInTime.exe.
# ------------------------------------------------------------------
