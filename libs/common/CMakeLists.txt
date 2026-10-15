# libs/common - tiện ích THỰC SỰ dùng chung giữa apps/agent và
# apps/dashboard: ghi log có cổng bật/tắt (log.h), HTTP client WinHTTP có
# keep-alive/timeout (httpclient.h), escape JSON + UTF-8/16 (strutil.h,
# header-only), đường dẫn %APPDATA%\JustInTime (configdir.h, paths.h), và
# sqlite3 (biên dịch 1 LẦN ở đây, cả 2 app link vào).
#
# KHÔNG đặt ở đây: bất cứ thứ gì chỉ 1 trong 2 app cần (vd AppSettings -
# cấu hình riêng của agent - nằm ở apps/agent/src/shared/settings.cpp;
# DashSettings - cấu hình riêng của dashboard - nằm ở
# apps/dashboard/src/dashsettings.cpp). Để code "trôi" vào đây mà chỉ 1
# app dùng là dấu hiệu đặt sai chỗ.
add_library(JustInTimeCommon STATIC
    include/log.h
    src/log.cpp
    include/httpclient.h
    src/httpclient.cpp
    include/configdir.h
    src/configdir.cpp
    include/strutil.h   # header-only
    include/paths.h      # header-only
    ${CMAKE_SOURCE_DIR}/third_party/sqlite/sqlite3.c
    ${CMAKE_SOURCE_DIR}/third_party/sqlite/sqlite3.h
)

target_include_directories(JustInTimeCommon PUBLIC
    include
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
