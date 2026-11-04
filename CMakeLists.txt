# JustInTime - monorepo gốc.
#
# C++ headers and sources are grouped by component under root includes/
# and src/. Build definitions and app-specific resources remain under apps/.
# libs/common/ defines the shared static library; apps/agent/ and
# apps/dashboard/ define the two separate native executables.
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

# CMake thường KHÔNG tự tìm thấy Qt6 (nó không nằm trong PATH mặc định) -
# phải trỏ đường dẫn qua CMAKE_PREFIX_PATH lúc configure, ví dụ:
#   cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.0/msvc2019_64"
# (thay bằng đúng phiên bản/kit Qt bạn đã cài - xem
# docs/HOW_TO_BUILD.md để biết cách tìm đường dẫn này).
#
# Dòng dưới đây chỉ là "lưới an toàn": nếu bạn QUÊN truyền
# -DCMAKE_PREFIX_PATH, CMake sẽ thử luôn biến môi trường Qt6_DIR (nếu có)
# trước khi báo lỗi, thay vì lúc nào cũng phải gõ lại đường dẫn.
if(NOT DEFINED CMAKE_PREFIX_PATH AND DEFINED ENV{Qt6_DIR})
    set(CMAKE_PREFIX_PATH "$ENV{Qt6_DIR}")
endif()
set(Qt6_DIR
    "C:/Qt/6.11.2/msvc2022_64/lib/cmake/Qt6"
)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_subdirectory(libs/common)
add_subdirectory(apps/agent)
add_subdirectory(apps/dashboard)
