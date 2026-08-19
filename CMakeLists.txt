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
    "C:/Qt/6.11.1/msvc2022_64/lib/cmake/Qt6"
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
)

include_directories(third_party)
include_directories(include)
include_directories(include/shared)
include_directories(include/core)
include_directories(include/agent)
include_directories(include/parent)

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
)

# ------------------------------------------------------------------
# Đưa cả dashboard (Go) ra cạnh JustInTime.exe sau khi
# build, để tray menu "Dashboards" tìm thấy chúng ngay mà không phải
# dò ngược cây thư mục (xem findResourceDir() trong trayicon.cpp).
# Không lỗi nếu dashboard-go/dashboard.exe chưa được build (go build
# ./cmd/dashboard) - chỉ là mục "Open Local Dashboard (Go)" sẽ báo
# không tìm thấy cho tới khi bạn build nó.
# ------------------------------------------------------------------

add_custom_command(TARGET JustInTime POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                -DSRC="${CMAKE_SOURCE_DIR}/dashboard-go/dashboard.exe"
                -DDEST_DIR="$<TARGET_FILE_DIR:JustInTime>/dashboard-go"
                -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake"
        COMMENT "Copying Go dashboard next to JustInTime.exe (skipped if not built yet)"
)
