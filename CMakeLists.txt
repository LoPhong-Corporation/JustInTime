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
        # ---- Lõi C (giữ nguyên logic gốc) ----
        src/activity.c
        include/activity.h
        src/database.c
        include/database.h
        third_party/sqlite/sqlite3.c
        third_party/sqlite/sqlite3.h
        src/device.c
        include/device.h
        src/sync.c
        include/sync.h
        include/config.h
        include/network.h
        src/network.c
        src/jsonutil.c
        include/jsonutil.h
        src/backup.c
        include/backup.h
        src/settings.c
        include/settings.h
        src/auth.c
        include/auth.h
        include/error_codes.h
        src/remoteview.c
        include/remoteview.h

        # ---- UI Qt (C++) ----
        src/main.cpp
        "include/trayicon.h"
        src/trayicon.cpp
        "include/logindialog.h"
        src/logindialog.cpp
        "include/settingsdialog.h"
        src/settingsdialog.cpp
        "include/supabasesetupdialog.h"
        src/supabasesetupdialog.cpp
        "include/remoteviewdialog.h"
        src/remoteviewdialog.cpp
        "include/updatechecker.h"
        src/updatechecker.cpp
)

include_directories(third_party)
include_directories(include)

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
