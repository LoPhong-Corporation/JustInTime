# apps/dashboard - standalone Qt Widgets dashboard executable.
add_executable(JustInTimeDashboard WIN32
        ../../resources/app_icon.qrc
        ../../resources/app_icon.rc

        ../../src/dashboard/main.cpp

        ../../includes/dashboard/dashconfig.h
        ../../src/dashboard/dashconfig.cpp
        ../../includes/dashboard/cloud.h
        ../../src/dashboard/cloud.cpp
        ../../includes/dashboard/localdb.h
        ../../src/dashboard/localdb.cpp
        ../../includes/dashboard/sysstats.h
        ../../src/dashboard/sysstats.cpp
        ../../includes/dashboard/dashsettings.h
        ../../src/dashboard/dashsettings.cpp
        ../../includes/dashboard/dashsession.h
        ../../src/dashboard/dashsession.cpp
        ../../includes/dashboard/dashi18n.h
        ../../src/dashboard/dashi18n.cpp
        ../../includes/dashboard/autostart.h
        ../../src/dashboard/autostart.cpp

        ../../includes/dashboard/ui/asyncutil.h
        ../../includes/dashboard/ui/theme.h
        ../../includes/dashboard/ui/leftrail.h
        ../../src/dashboard/ui/leftrail.cpp
        ../../includes/dashboard/ui/machinessidebar.h
        ../../src/dashboard/ui/machinessidebar.cpp
        ../../includes/dashboard/ui/rightpanel.h
        ../../src/dashboard/ui/rightpanel.cpp
        ../../includes/dashboard/ui/chartwidget.h
        ../../src/dashboard/ui/chartwidget.cpp
        ../../includes/dashboard/ui/gaugewidget.h
        ../../src/dashboard/ui/gaugewidget.cpp
        ../../includes/dashboard/ui/timelinewidget.h
        ../../src/dashboard/ui/timelinewidget.cpp
        ../../includes/dashboard/ui/logindialog.h
        ../../src/dashboard/ui/logindialog.cpp
        ../../includes/dashboard/ui/settingsdialog.h
        ../../src/dashboard/ui/settingsdialog.cpp
        ../../includes/dashboard/ui/mainwindow.h
        ../../src/dashboard/ui/mainwindow.cpp
        ../../includes/dashboard/ui/pages/overviewpage.h
        ../../src/dashboard/ui/pages/overviewpage.cpp
        ../../includes/dashboard/ui/pages/metricpages.h
        ../../src/dashboard/ui/pages/metricpages.cpp
        ../../includes/dashboard/ui/pages/localpage.h
        ../../src/dashboard/ui/pages/localpage.cpp
        ../../includes/dashboard/ui/pages/cloudpage.h
        ../../src/dashboard/ui/pages/cloudpage.cpp
        ../../includes/dashboard/ui/pages/devicespage.h
        ../../src/dashboard/ui/pages/devicespage.cpp
        ../../includes/dashboard/ui/pages/familypage.h
        ../../src/dashboard/ui/pages/familypage.cpp
)

target_include_directories(JustInTimeDashboard PRIVATE
        ../../includes/dashboard
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
