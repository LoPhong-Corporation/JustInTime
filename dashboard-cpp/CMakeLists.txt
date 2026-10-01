# JustInTime Dashboard - Qt6 Widgets desktop app (KHÔNG còn HTTP
# server/trình duyệt - xem BAO_CAO_UI_CPP.md). Dùng chung 1 số module ở
# ../src/shared (log, settings, httpclient) với agent để không lặp lại
# code kết nối Supabase/ghi log/đọc %APPDATA% đã viết và kiểm thử ở đó.
cmake_minimum_required(VERSION 3.16)
project(JustInTimeDashboard CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC OFF) # các lớp UI ở đây KHÔNG dùng Q_OBJECT/signal-slot tự khai báo (xem asyncutil.h) - không cần moc

if (NOT WIN32)
    message(FATAL_ERROR "JustInTime Dashboard chỉ build cho Windows.")
endif()

set(AGENT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

add_executable(dashboard WIN32
    src/main.cpp
    src/cloud.cpp
    src/dashconfig.cpp
    src/localdb.cpp
    src/dashsettings.cpp
    src/dashsession.cpp
    src/sysstats.cpp
    src/i18n.cpp
    src/autostart.cpp
    src/ui/chartwidget.cpp
    src/ui/gaugewidget.cpp
    src/ui/timelinewidget.cpp
    src/ui/logindialog.cpp
    src/ui/settingsdialog.cpp
    src/ui/mainwindow.cpp
    src/ui/pages/overviewpage.cpp
    src/ui/pages/metricpages.cpp
    src/ui/pages/localpage.cpp
    src/ui/pages/cloudpage.cpp
    src/ui/pages/devicespage.cpp
    src/ui/pages/familypage.cpp
    # Dùng lại nguyên vẹn từ agent (xem chú thích đầu file mỗi module):
    ${AGENT_ROOT}/src/shared/log.cpp
    ${AGENT_ROOT}/src/shared/settings.cpp
    ${AGENT_ROOT}/src/shared/httpclient.cpp
    ${AGENT_ROOT}/third_party/sqlite/sqlite3.c
)

target_include_directories(dashboard PRIVATE
    include
    third_party
    third_party/nlohmann
    ${AGENT_ROOT}/include/shared
    ${AGENT_ROOT}/third_party
)

target_compile_definitions(dashboard PRIVATE
    _WIN32_WINNT=0x0A00
    MINGW_HAS_SECURE_API=1
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_DEFAULT_MEMSTATUS=0
)

target_link_libraries(dashboard PRIVATE
    Qt6::Widgets
    winhttp crypt32 pdh psapi iphlpapi ntdll ws2_32 shell32 advapi32 ole32
)
