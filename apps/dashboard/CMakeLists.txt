# apps/dashboard - dashboard.exe: cửa sổ Qt Widgets xem số liệu hệ thống/
# hoạt động/gia đình. KHÔNG có HTTP server (bản trước, dashboard-go, chạy
# 1 web server cục bộ; bản này mở thẳng cửa sổ native). Tách khỏi
# JustInTime.exe thành .exe riêng để build/chạy/debug độc lập không cần
# đụng tới agent - xem README.md mục "Vì sao tách".
add_executable(JustInTimeDashboard WIN32
        src/main.cpp

        include/dashconfig.h
        src/dashconfig.cpp
        include/cloud.h
        src/cloud.cpp
        include/localdb.h
        src/localdb.cpp
        include/sysstats.h
        src/sysstats.cpp
        include/dashsettings.h
        src/dashsettings.cpp
        include/dashsession.h
        src/dashsession.cpp
        include/dashi18n.h
        src/dashi18n.cpp
        include/autostart.h
        src/autostart.cpp

        include/ui/asyncutil.h
        include/ui/theme.h
        include/ui/leftrail.h
        src/ui/leftrail.cpp
        include/ui/machinessidebar.h
        src/ui/machinessidebar.cpp
        include/ui/rightpanel.h
        src/ui/rightpanel.cpp
        include/ui/chartwidget.h
        src/ui/chartwidget.cpp
        include/ui/gaugewidget.h
        src/ui/gaugewidget.cpp
        include/ui/timelinewidget.h
        src/ui/timelinewidget.cpp
        include/ui/logindialog.h
        src/ui/logindialog.cpp
        include/ui/settingsdialog.h
        src/ui/settingsdialog.cpp
        include/ui/mainwindow.h
        src/ui/mainwindow.cpp
        include/ui/pages/overviewpage.h
        src/ui/pages/overviewpage.cpp
        include/ui/pages/metricpages.h
        src/ui/pages/metricpages.cpp
        include/ui/pages/localpage.h
        src/ui/pages/localpage.cpp
        include/ui/pages/cloudpage.h
        src/ui/pages/cloudpage.cpp
        include/ui/pages/devicespage.h
        src/ui/pages/devicespage.cpp
        include/ui/pages/familypage.h
        src/ui/pages/familypage.cpp
)

target_include_directories(JustInTimeDashboard PRIVATE
        include
        third_party
        third_party/nlohmann
)

target_link_libraries(JustInTimeDashboard PRIVATE
        JustInTimeCommon
        Qt6::Widgets
        crypt32
        pdh
        psapi
        iphlpapi
        ntdll
        ws2_32
        advapi32
        ole32
)
