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
# TUỲ CHỌN DÀNH CHO DEVELOPMENT - 2 CHẾ ĐỘ CORE (chọn lúc cấu hình,
# KHÔNG đổi được lúc chạy - đây là 2 bản build khác nhau của cùng 1
# app). Người dùng thông thường KHÔNG cần quan tâm mục này - mặc định
# STABLE đã là lựa chọn đúng, an toàn cho production.
#
#   STABLE       - lõi C thuần (src/shared/stable/*.c) - bản gốc, đã
#                  chạy ổn định lâu dài. DÙNG BẢN NÀY nếu không chắc
#                  nên chọn gì.
#   EXPERIMENTAL - lõi C++ (src/shared/experimental/*.cpp) - bản viết
#                  lại dùng RAII/std::string/std::vector/std::mutex...
#                  cho các module đã chuyển (xem ghi chú "CHUYỂN TỪ C
#                  SANG C++" trong từng file .cpp). CHƯA chạy đủ lâu
#                  ngoài thực tế để coi là ổn định như bản STABLE -
#                  chỉ dùng để phát triển/thử nghiệm, so sánh hành vi
#                  với bản gốc.
#
# Cả 2 bản cùng thoả mãn NGUYÊN VẸN 1 bộ header extern "C" trong
# include/{shared,core,agent}/ - phần còn lại của app (Qt UI...) gọi
# vào hệt nhau, không biết (và không cần biết) đang chạy chế độ nào -
# chỉ khác ở cách hiện thực bên trong 15 module:
#   src/shared/{stable,experimental}/     - restclient, jsonutil,
#     device, machines, backup, applimits, parentlink, settings,
#     network, auth, i18n, database
#   src/agent/{stable,experimental}/      - remoteview, sync
#   src/core/{stable,experimental}/       - activity
# Đây là TOÀN BỘ phần "core" (dữ liệu/mạng/đồng bộ) của app - phần
# còn lại (giao diện Qt) không có bản C, chỉ có C++.
#
# Chọn qua dòng lệnh CMake, ví dụ:
#   cmake -DJUSTINTIME_CORE=STABLE ..
#   cmake -DJUSTINTIME_CORE=EXPERIMENTAL ..
# Mặc định STABLE - build production/phát hành LUÔN nên dùng STABLE
# trừ khi đang chủ động thử nghiệm bản EXPERIMENTAL.
# ------------------------------------------------------------------

set(JUSTINTIME_CORE "STABLE" CACHE STRING "[Development] Core implementation: STABLE (C) or EXPERIMENTAL (C++)")
set_property(CACHE JUSTINTIME_CORE PROPERTY STRINGS STABLE EXPERIMENTAL)

if(NOT JUSTINTIME_CORE STREQUAL "STABLE" AND NOT JUSTINTIME_CORE STREQUAL "EXPERIMENTAL")
    message(FATAL_ERROR "JUSTINTIME_CORE phải là STABLE hoặc EXPERIMENTAL, đang là '${JUSTINTIME_CORE}'")
endif()

if(JUSTINTIME_CORE STREQUAL "STABLE")
    set(CORE_DIR "src/shared/stable")
    set(AGENT_CORE_DIR "src/agent/stable")
    set(CORE_CORE_DIR "src/core/stable")
    set(CORE_EXT "c")
else()
    set(CORE_DIR "src/shared/experimental")
    set(AGENT_CORE_DIR "src/agent/experimental")
    set(CORE_CORE_DIR "src/core/experimental")
    set(CORE_EXT "cpp")
    message(WARNING "JustInTime: đang build với JUSTINTIME_CORE=EXPERIMENTAL (lõi C++ thử nghiệm) - dùng STABLE cho bản phát hành/production.")
endif()

message(STATUS "JustInTime: [Development] chế độ core = ${JUSTINTIME_CORE} (${CORE_DIR}/*.${CORE_EXT})")

set(CORE_DUAL_SOURCES
        "${CORE_DIR}/restclient.${CORE_EXT}"
        "${CORE_DIR}/jsonutil.${CORE_EXT}"
        "${CORE_DIR}/device.${CORE_EXT}"
        "${CORE_DIR}/machines.${CORE_EXT}"
        "${CORE_DIR}/backup.${CORE_EXT}"
        "${CORE_DIR}/applimits.${CORE_EXT}"
        "${CORE_DIR}/parentlink.${CORE_EXT}"
        "${CORE_DIR}/settings.${CORE_EXT}"
        "${CORE_DIR}/network.${CORE_EXT}"
        "${CORE_DIR}/auth.${CORE_EXT}"
        "${CORE_DIR}/i18n.${CORE_EXT}"
        "${CORE_DIR}/database.${CORE_EXT}"
        "${AGENT_CORE_DIR}/remoteview.${CORE_EXT}"
        "${AGENT_CORE_DIR}/sync.${CORE_EXT}"
        "${CORE_CORE_DIR}/activity.${CORE_EXT}"
)

add_executable(JustInTime WIN32
        # ---- shared/ : dùng chung bởi cả vai trò agent (con) và parent ----
        include/shared/auth.h
        include/shared/network.h
        include/shared/jsonutil.h
        include/shared/device.h
        include/shared/settings.h
        include/shared/backup.h
        include/shared/database.h
        include/shared/config.h
        include/shared/error_codes.h
        include/shared/parentlink.h
        include/shared/applimits.h
        include/shared/restclient.h
        include/shared/machines.h
        include/shared/i18n.h
        third_party/sqlite/sqlite3.c
        third_party/sqlite/sqlite3.h

        ${CORE_DUAL_SOURCES}

        # ---- core/ : engine theo dõi hoạt động (activity.{c,cpp} nằm
        # trong CORE_DUAL_SOURCES ở trên) ----
        include/core/activity.h

        # ---- agent/ : vai trò "con" (tray UI, Control Panel, entrypoint;
        # remoteview.{c,cpp} và sync.{c,cpp} nằm trong CORE_DUAL_SOURCES
        # ở trên) ----
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
        include/agent/sync.h
        "include/agent/parentlinkpage.h"
        src/agent/parentlinkpage.cpp

        # ---- parent/ : vai trò "phụ huynh" (xem dữ liệu con + đặt giới hạn) ----
        "include/parent/parentpage.h"
        src/parent/parentpage.cpp
)

target_compile_definitions(JustInTime PRIVATE
        JUSTINTIME_CORE_MODE="${JUSTINTIME_CORE}"
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
