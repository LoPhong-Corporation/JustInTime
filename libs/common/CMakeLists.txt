# libs/common - tiện ích THỰC SỰ dùng chung giữa apps/agent và
# apps/dashboard: ghi log có cổng bật/tắt (log.h), HTTP client WinHTTP có
# keep-alive/timeout (httpclient.h), escape JSON + UTF-8/16 (strutil.h,
# header-only), đường dẫn %APPDATA%\JustInTime (configdir.h, paths.h), và
# sqlite3 (biên dịch 1 LẦN ở đây, cả 2 app link vào).
#
# KHÔNG đặt ở đây: bất cứ thứ gì chỉ 1 trong 2 app cần (vd AppSettings -
# cấu hình riêng của agent - nằm ở src/agent/shared/settings.cpp;
# DashSettings - cấu hình riêng của dashboard - nằm ở
# src/dashboard/dashsettings.cpp). Để code "trôi" vào đây mà chỉ 1
# app dùng là dấu hiệu đặt sai chỗ.
add_library(JustInTimeCommon STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../../includes/common/log.h
    ${CMAKE_CURRENT_LIST_DIR}/../../src/common/log.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../includes/common/httpclient.h
    ${CMAKE_CURRENT_LIST_DIR}/../../src/common/httpclient.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../includes/common/configdir.h
    ${CMAKE_CURRENT_LIST_DIR}/../../src/common/configdir.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../includes/common/strutil.h   # header-only
    ${CMAKE_CURRENT_LIST_DIR}/../../includes/common/paths.h      # header-only
    ${CMAKE_SOURCE_DIR}/third_party/sqlite/sqlite3.c
    ${CMAKE_SOURCE_DIR}/third_party/sqlite/sqlite3.h
)

target_include_directories(JustInTimeCommon PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../includes/common
    ${CMAKE_SOURCE_DIR}/third_party
)

target_compile_definitions(JustInTimeCommon PUBLIC
    _WIN32_WINNT=0x0A00
    MINGW_HAS_SECURE_API=1
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_DEFAULT_MEMSTATUS=0
)

target_link_libraries(JustInTimeCommon PUBLIC
    winhttp
)
