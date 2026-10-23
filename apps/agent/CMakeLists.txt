# apps/agent - JustInTime.exe: theo dõi hoạt động, tray, Control Panel,
# đồng bộ cloud. Mở Dashboard (apps/dashboard) bằng cách spawn
# dashboard.exe như 1 tiến trình riêng (xem TrayIcon::onOpenDashboard()
# trong src/agent/trayicon.cpp) - 2 app KHÔNG build chung 1 target.
#
# ------------------------------------------------------------------
# Toàn bộ phần "core" (dữ liệu/mạng/đồng bộ) của app đã được chuyển
# hoàn toàn từ C sang C++ (RAII cho HANDLE/HINTERNET/sqlite3_stmt*,
# std::string/std::vector thay buffer cấp phát tay, std::mutex thay
# CRITICAL_SECTION tự quản...) - xem ghi chú "Đã CHUYỂN TỪ C SANG
# C++" ở đầu mỗi file .cpp trong src/shared/, src/agent/, src/core/
# để biết chi tiết từng thay đổi.
# ------------------------------------------------------------------

add_executable(JustInTime WIN32
        # ---- shared/ : dùng chung bởi cả vai trò agent (con) và parent,
        # RIÊNG của app này - không nhầm với libs/common (xem đầu file đó
        # để biết ranh giới: cái gì CHỈ agent cần thì ở đây, cái gì cả
        # dashboard cũng cần thì ở libs/common) ----
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
        include/agent/batchsend.h

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
)

target_include_directories(JustInTime PRIVATE
        include
        include/shared
        include/core
        include/agent
        include/parent
)

target_link_libraries(JustInTime PRIVATE
        JustInTimeCommon
        Qt6::Widgets
        Qt6::Network
        psapi
        crypt32
        advapi32
        ws2_32
        wtsapi32
)
