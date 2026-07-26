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
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_executable(JustInTime WIN32
        # ---- shared/ : dùng chung bởi cả vai trò agent (con) và parent ----
        src/shared/auth.c
        include/shared/auth.h
        src/shared/network.c
        include/shared/network.h
        src/shared/jsonutil.c
        include/shared/jsonutil.h
        src/shared/device.c
        include/shared/device.h
        src/shared/settings.c
        include/shared/settings.h
        src/shared/backup.c
        include/shared/backup.h
        src/shared/database.c
        include/shared/database.h
        include/shared/config.h
        include/shared/error_codes.h
        src/shared/parentlink.c
        include/shared/parentlink.h
        src/shared/applimits.c
        include/shared/applimits.h
        src/shared/restclient.c
        include/shared/restclient.h
        third_party/sqlite/sqlite3.c
        third_party/sqlite/sqlite3.h

        # ---- core/ : engine theo dõi hoạt động ----
        src/core/activity.c
        include/core/activity.h

        # ---- agent/ : vai trò "con" (tray UI, sync, remote view, dialogs, entrypoint) ----
        src/agent/main.cpp
        "include/agent/trayicon.h"
        src/agent/trayicon.cpp
        "include/agent/logindialog.h"
        src/agent/logindialog.cpp
        "include/agent/settingsdialog.h"
        src/agent/settingsdialog.cpp
        "include/agent/supabasesetupdialog.h"
        src/agent/supabasesetupdialog.cpp
        "include/agent/remoteviewdialog.h"
        src/agent/remoteviewdialog.cpp
        "include/agent/updatechecker.h"
        src/agent/updatechecker.cpp
        include/agent/remoteview.h
        src/agent/remoteview.c
        include/agent/sync.h
        src/agent/sync.c
        "include/agent/parentlinkdialog.h"
        src/agent/parentlinkdialog.cpp

        # ---- parent/ : vai trò "phụ huynh" (xem dữ liệu con + đặt giới hạn) ----
        "include/parent/parentdialog.h"
        src/parent/parentdialog.cpp
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
# Đưa cả 2 dashboard (Python + Go) ra cạnh JustInTime.exe sau khi
# build, để tray menu "Dashboards" tìm thấy chúng ngay mà không phải
# dò ngược cây thư mục (xem findResourceDir() trong trayicon.cpp).
# Không lỗi nếu dashboard-go/dashboard.exe chưa được build (go build
# ./cmd/dashboard) - chỉ là mục "Open Local Dashboard (Go)" sẽ báo
# không tìm thấy cho tới khi bạn build nó.
# ------------------------------------------------------------------

add_custom_command(TARGET JustInTime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/dashboard"
                "$<TARGET_FILE_DIR:JustInTime>/dashboard"
        COMMENT "Copying Python dashboard next to JustInTime.exe"
)

add_custom_command(TARGET JustInTime POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                -DSRC="${CMAKE_SOURCE_DIR}/dashboard-go/dashboard.exe"
                -DDEST_DIR="$<TARGET_FILE_DIR:JustInTime>/dashboard-go"
                -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake"
        COMMENT "Copying Go dashboard next to JustInTime.exe (skipped if not built yet)"
)
