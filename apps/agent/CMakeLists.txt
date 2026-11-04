# apps/agent - JustInTime.exe: theo dõi hoạt động, tray, Control Panel,
# đồng bộ cloud. Mở Dashboard (apps/dashboard) bằng cách spawn
# dashboard.exe như 1 tiến trình riêng - 2 app KHÔNG build chung 1 target.

add_executable(JustInTime WIN32
        ../../resources/app_icon.qrc
        ../../resources/app_icon.rc

        # Agent-only shared modules.
        ../../src/agent/shared/restclient.cpp
        ../../includes/agent/shared/restclient.h
        ../../src/agent/shared/jsonutil.cpp
        ../../includes/agent/shared/jsonutil.h
        ../../src/agent/shared/device.cpp
        ../../includes/agent/shared/device.h
        ../../src/agent/shared/machines.cpp
        ../../includes/agent/shared/machines.h
        ../../src/agent/shared/backup.cpp
        ../../includes/agent/shared/backup.h
        ../../src/agent/shared/applimits.cpp
        ../../includes/agent/shared/applimits.h
        ../../src/agent/shared/parentlink.cpp
        ../../includes/agent/shared/parentlink.h
        ../../src/agent/shared/settings.cpp
        ../../includes/agent/shared/settings.h
        ../../src/agent/shared/network.cpp
        ../../includes/agent/shared/network.h
        ../../src/agent/shared/auth.cpp
        ../../includes/agent/shared/auth.h
        ../../src/agent/shared/i18n.cpp
        ../../includes/agent/shared/i18n.h
        ../../src/agent/shared/database.cpp
        ../../includes/agent/shared/database.h
        ../../includes/agent/shared/config.h
        ../../includes/agent/shared/error_codes.h
        ../../includes/agent/agent/batchsend.h

        # Activity-tracking engine.
        ../../src/agent/core/activity.cpp
        ../../includes/agent/core/activity.h

        # Agent UI, sync, and entrypoint.
        ../../src/agent/agent/main.cpp
        ../../includes/agent/agent/trayicon.h
        ../../src/agent/agent/trayicon.cpp
        ../../includes/agent/agent/controlpanelwindow.h
        ../../src/agent/agent/controlpanelwindow.cpp
        ../../includes/agent/agent/loginpage.h
        ../../src/agent/agent/loginpage.cpp
        ../../includes/agent/agent/settingspage.h
        ../../src/agent/agent/settingspage.cpp
        ../../includes/agent/agent/supabasesetuppage.h
        ../../src/agent/agent/supabasesetuppage.cpp
        ../../includes/agent/agent/remoteviewpage.h
        ../../src/agent/agent/remoteviewpage.cpp
        ../../includes/agent/agent/updatechecker.h
        ../../src/agent/agent/updatechecker.cpp
        ../../includes/agent/agent/remoteview.h
        ../../src/agent/agent/remoteview.cpp
        ../../includes/agent/agent/sync.h
        ../../src/agent/agent/sync.cpp
        ../../includes/agent/agent/parentlinkpage.h
        ../../src/agent/agent/parentlinkpage.cpp

        # Parent role UI.
        ../../includes/agent/parent/parentpage.h
        ../../src/agent/parent/parentpage.cpp
)

target_include_directories(JustInTime PRIVATE
        ../../includes/agent
        ../../includes/agent/shared
        ../../includes/agent/core
        ../../includes/agent/agent
        ../../includes/agent/parent
)

target_link_libraries(JustInTime PRIVATE
        JustInTimeCommon
        Qt6::Widgets
        Qt6::Network
        psapi
        crypt32
        advapi32
        ws2_32
        wtsapi32
)
