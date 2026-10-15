# JustInTime - monorepo gốc.
#
# Cấu trúc (xem README.md cho sơ đồ đầy đủ + lý do tách):
#   libs/common/     - tiện ích CHUNG (log, HTTP client, đường dẫn
#                       %APPDATA%, sqlite3) - thư viện tĩnh, không tự nó
#                       build ra .exe nào.
#   apps/agent/      - JustInTime.exe: theo dõi hoạt động, tray, Control
#                       Panel, đồng bộ cloud.
#   apps/dashboard/  - dashboard.exe: cửa sổ xem số liệu/quản lý gia đình
#                       (Qt Widgets, không HTTP server).
#
# 2 app build RA 2 FILE .exe RIÊNG (không gộp chung 1 target nữa) - xem
# README.md mục "Vì sao tách" để biết lý do và đánh đổi so với bản gộp
# trước đó. Cả 2 cùng dùng chung libs/common nên không có 2 bản sqlite3
# hay 2 bản log.cpp lặp lại trong repo.
cmake_minimum_required(VERSION 3.28)
project(JustInTime LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Cả JustInTime.exe và JustInTimeDashboard.exe LUÔN build ra CÙNG 1 thư
# mục (cho mọi config Debug/Release) - TrayIcon::onOpenDashboard() dựa
# thẳng vào việc này để tìm JustInTimeDashboard.exe (cùng thư mục với
# chính nó), không phải dò ngược cây thư mục.
foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${cfg} CFG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG_UPPER} "${CMAKE_BINARY_DIR}/bin/${cfg}")
endforeach()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Nếu CMake không tự tìm thấy Qt6, set CMAKE_PREFIX_PATH trỏ tới
# thư mục cài Qt, ví dụ:
#   -DCMAKE_PREFIX_PATH="C:/Qt/6.7.0/msvc2019_64"
set(Qt6_DIR
    "C:/Qt/6.11.2/msvc2022_64/lib/cmake/Qt6"
)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_subdirectory(libs/common)
add_subdirectory(apps/agent)
add_subdirectory(apps/dashboard)
