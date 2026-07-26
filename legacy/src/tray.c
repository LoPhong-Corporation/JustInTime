//
// tray.c
//

#include "../include/tray.h"
#include "../include/ui_dialog.h"
#include "../include/auth.h"
#include "../include/settings.h"
#include "../include/database.h"
#include "../include/error_codes.h"

#include <shellapi.h>
#include <string.h>
#include <stdio.h>

#define WM_TRAYICON (WM_APP + 1)

#define ID_TRAY_ACCOUNT_INFO    3000
#define ID_TRAY_LOGIN           3001
#define ID_TRAY_LOGOUT          3002
#define ID_TRAY_PAUSE_TOGGLE    3003
#define ID_TRAY_REPORT          3004
#define ID_TRAY_SETTINGS        3005
#define ID_TRAY_SUPABASE_SETUP  3006
#define ID_TRAY_TOGGLE_DEBUG    3007
#define ID_TRAY_EXIT            3008

static HINSTANCE     g_hInstance;
static HWND          g_hwnd;
static NOTIFYICONDATAW g_nid;

static volatile int g_paused        = 0;
static int           g_debug_visible = 0;

/*
 * Đổi wide-string sang UTF-8 (dùng khi lưu từ dialog vào
 * settings, vốn lưu dạng char*).
 */
static void wide_to_utf8(
    const wchar_t* src,
    char* dst,
    int dst_size)
{
    WideCharToMultiByte(
        CP_UTF8, 0,
        src, -1,
        dst, dst_size,
        NULL, NULL
    );
}

static void update_tooltip(void)
{
    AuthSession session;
    auth_get_session(&session);

    if (session.logged_in)
    {
        wchar_t email_w[256] = {0};

        MultiByteToWideChar(
            CP_UTF8, 0,
            session.email, -1,
            email_w, 256
        );

        swprintf(
            g_nid.szTip,
            sizeof(g_nid.szTip) / sizeof(wchar_t),
            L"JustInTime - %ls%ls",
            email_w,
            g_paused ? L" (tạm dừng)" : L""
        );
    }
    else
    {
        swprintf(
            g_nid.szTip,
            sizeof(g_nid.szTip) / sizeof(wchar_t),
            L"JustInTime - chưa đăng nhập%ls",
            g_paused ? L" (tạm dừng)" : L""
        );
    }

    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

/*
 * Xử lý menu "Đăng nhập / Đăng ký"
 */
static void handle_login(void)
{
    int wants_register = MessageBoxW(
        NULL,
        L"Bạn đã có tài khoản chưa?\n\n"
        L"Có = Đăng nhập\n"
        L"Không = Đăng ký tài khoản mới",
        L"JustInTime",
        MB_YESNO | MB_ICONQUESTION | MB_TOPMOST
    ) == IDNO;

    DialogField fields[2];
    memset(fields, 0, sizeof(fields));

    fields[0].label = L"Email:";
    fields[1].label = L"Mật khẩu:";
    fields[1].is_password = 1;

    if (
        !show_input_dialog(
            g_hInstance,
            wants_register ? L"Đăng ký tài khoản" : L"Đăng nhập",
            fields,
            2
        )
    )
        return;

    char email[256]    = {0};
    char password[256] = {0};

    wide_to_utf8(fields[0].value, email, sizeof(email));
    wide_to_utf8(fields[1].value, password, sizeof(password));

    if (email[0] == '\0' || password[0] == '\0')
    {
        show_error_message(L"Vui lòng nhập đầy đủ email và mật khẩu.");
        return;
    }

    char err[512] = {0};
    int ok;

    if (wants_register)
        ok = auth_register(email, password, err, sizeof(err));
    else
        ok = auth_login(email, password, err, sizeof(err));

    if (ok)
    {
        wchar_t err_w[512] = {0};

        MultiByteToWideChar(
            CP_UTF8, 0,
            err, -1,
            err_w, 512
        );

        if (auth_is_logged_in())
        {
            show_info_message(L"Đăng nhập thành công! Dữ liệu sẽ được đồng bộ lên cloud.");
        }
        else if (err[0] != '\0')
        {
            /* Đăng ký thành công nhưng cần xác nhận email */
            show_info_message(err_w);
        }

        update_tooltip();
    }
    else
    {
        wchar_t err_w[512] = {0};

        MultiByteToWideChar(
            CP_UTF8, 0,
            err, -1,
            err_w, 512
        );

        show_error_message(err_w);
    }
}

static void handle_logout(void)
{
    int confirm = MessageBoxW(
        NULL,
        L"Đăng xuất khỏi tài khoản hiện tại?",
        L"JustInTime",
        MB_YESNO | MB_ICONQUESTION | MB_TOPMOST
    );

    if (confirm != IDYES)
        return;

    auth_logout();
    update_tooltip();

    show_info_message(L"Đã đăng xuất. Dữ liệu vẫn lưu local, sẽ dừng đồng bộ cloud.");
}

static void handle_report(void)
{
    wchar_t buffer[4096] = {0};

    db_build_daily_summary_text(buffer, 4096);

    wchar_t full[4200] = {0};

    swprintf(
        full,
        4200,
        L"Tổng kết hôm nay:\n\n%ls",
        buffer
    );

    show_info_message(full);
}

static void handle_settings(void)
{
    AppSettings s;
    settings_get(&s);

    DialogField fields[6];
    memset(fields, 0, sizeof(fields));

    fields[0].label = L"Chu kỳ đồng bộ (giây):";
    swprintf(fields[0].value, MAX_FIELD_VALUE_LEN, L"%d", s.sync_interval_sec);

    fields[1].label = L"Chu kỳ backup (giây):";
    swprintf(fields[1].value, MAX_FIELD_VALUE_LEN, L"%d", s.backup_interval_sec);

    fields[2].label = L"Chu kỳ báo cáo (giây):";
    swprintf(fields[2].value, MAX_FIELD_VALUE_LEN, L"%d", s.summary_interval_sec);

    fields[3].label = L"Thời lượng tối thiểu ghi (giây):";
    swprintf(fields[3].value, MAX_FIELD_VALUE_LEN, L"%d", s.min_duration_sec);

    fields[4].label = L"App loại trừ (vd: steam.exe,discord.exe):";
    MultiByteToWideChar(
        CP_UTF8, 0,
        s.excluded_processes, -1,
        fields[4].value, MAX_FIELD_VALUE_LEN
    );

    fields[5].label = L"Tự khởi động cùng Windows:";
    fields[5].is_checkbox = 1;
    wcscpy_s(fields[5].value, MAX_FIELD_VALUE_LEN, s.autostart_enabled ? L"1" : L"0");

    if (
        !show_input_dialog(
            g_hInstance,
            L"Cài đặt",
            fields,
            6
        )
    )
        return;

    AppSettings updated;
    memset(&updated, 0, sizeof(updated));

    updated.sync_interval_sec    = _wtoi(fields[0].value);
    updated.backup_interval_sec  = _wtoi(fields[1].value);
    updated.summary_interval_sec = _wtoi(fields[2].value);
    updated.min_duration_sec     = _wtoi(fields[3].value);
    updated.autostart_enabled    = wcscmp(fields[5].value, L"1") == 0;

    if (updated.sync_interval_sec < 5)
        updated.sync_interval_sec = 5;

    if (updated.backup_interval_sec < 60)
        updated.backup_interval_sec = 60;

    if (updated.summary_interval_sec < 30)
        updated.summary_interval_sec = 30;

    if (updated.min_duration_sec < 0)
        updated.min_duration_sec = 0;

    wide_to_utf8(
        fields[4].value,
        updated.excluded_processes,
        MAX_EXCLUDED_LEN
    );

    /* Giữ nguyên cấu hình Supabase hiện có */
    strcpy_s(updated.supabase_url, MAX_URL_LEN, s.supabase_url);
    strcpy_s(updated.supabase_key, MAX_KEY_LEN, s.supabase_key);

    if (settings_update(&updated))
        show_info_message(L"Đã lưu cài đặt.");
    else
    {
        wchar_t msg[128];
        swprintf(msg, 128, L"[%hs] Lưu cài đặt thất bại.", ERR_SETTINGS_SAVE_FAIL);
        show_error_message(msg);
    }
}

static void handle_supabase_setup(void)
{
    AppSettings s;
    settings_get(&s);

    DialogField fields[2];
    memset(fields, 0, sizeof(fields));

    fields[0].label = L"Supabase URL:";
    MultiByteToWideChar(CP_UTF8, 0, s.supabase_url, -1, fields[0].value, MAX_FIELD_VALUE_LEN);

    fields[1].label = L"Supabase Publishable Key:";
    MultiByteToWideChar(CP_UTF8, 0, s.supabase_key, -1, fields[1].value, MAX_FIELD_VALUE_LEN);

    if (
        !show_input_dialog(
            g_hInstance,
            L"Thiết lập Supabase",
            fields,
            2
        )
    )
        return;

    wide_to_utf8(fields[0].value, s.supabase_url, MAX_URL_LEN);
    wide_to_utf8(fields[1].value, s.supabase_key, MAX_KEY_LEN);

    if (settings_update(&s))
        show_info_message(L"Đã lưu cấu hình Supabase.");
    else
    {
        wchar_t msg[128];
        swprintf(msg, 128, L"[%hs] Lưu cấu hình thất bại.", ERR_SETTINGS_SAVE_FAIL);
        show_error_message(msg);
    }
}

static void handle_toggle_debug(void)
{
    HWND console = GetConsoleWindow();

    if (!console)
        return;

    g_debug_visible = !g_debug_visible;

    ShowWindow(
        console,
        g_debug_visible ? SW_SHOW : SW_HIDE
    );
}

static void show_context_menu(HWND hwnd)
{
    HMENU menu = CreatePopupMenu();

    AuthSession session;
    auth_get_session(&session);

    if (session.logged_in)
    {
        wchar_t email_w[256] = {0};

        MultiByteToWideChar(
            CP_UTF8, 0,
            session.email, -1,
            email_w, 256
        );

        wchar_t label[300];
        swprintf(label, 300, L"Đã đăng nhập: %ls", email_w);

        AppendMenuW(menu, MF_STRING | MF_GRAYED, ID_TRAY_ACCOUNT_INFO, label);
        AppendMenuW(menu, MF_STRING, ID_TRAY_LOGOUT, L"Đăng xuất");
    }
    else
    {
        AppendMenuW(menu, MF_STRING, ID_TRAY_LOGIN, L"Đăng nhập / Đăng ký...");
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);

    AppendMenuW(
        menu,
        MF_STRING,
        ID_TRAY_PAUSE_TOGGLE,
        g_paused ? L"Tiếp tục theo dõi" : L"Tạm dừng theo dõi"
    );

    AppendMenuW(menu, MF_STRING, ID_TRAY_REPORT, L"Xem báo cáo hôm nay");

    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);

    AppendMenuW(menu, MF_STRING, ID_TRAY_SETTINGS, L"Cài đặt...");
    AppendMenuW(menu, MF_STRING, ID_TRAY_SUPABASE_SETUP, L"Thiết lập Supabase...");
    AppendMenuW(
        menu,
        MF_STRING | (g_debug_visible ? MF_CHECKED : 0),
        ID_TRAY_TOGGLE_DEBUG,
        L"Hiện cửa sổ debug"
    );

    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Thoát");

    POINT pt;
    GetCursorPos(&pt);

    SetForegroundWindow(hwnd);

    TrackPopupMenu(
        menu,
        TPM_RIGHTBUTTON,
        pt.x, pt.y,
        0,
        hwnd,
        NULL
    );

    PostMessage(hwnd, WM_NULL, 0, 0);

    DestroyMenu(menu);
}

static LRESULT CALLBACK tray_wndproc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (msg)
    {
        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP)
                show_context_menu(hwnd);
            return 0;

        case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
                case ID_TRAY_LOGIN:
                    handle_login();
                    break;

                case ID_TRAY_LOGOUT:
                    handle_logout();
                    break;

                case ID_TRAY_PAUSE_TOGGLE:
                    g_paused = !g_paused;
                    update_tooltip();
                    break;

                case ID_TRAY_REPORT:
                    handle_report();
                    break;

                case ID_TRAY_SETTINGS:
                    handle_settings();
                    break;

                case ID_TRAY_SUPABASE_SETUP:
                    handle_supabase_setup();
                    break;

                case ID_TRAY_TOGGLE_DEBUG:
                    handle_toggle_debug();
                    break;

                case ID_TRAY_EXIT:
                    DestroyWindow(hwnd);
                    break;
            }

            return 0;
        }

        case WM_DESTROY:
            tray_cleanup();
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND tray_init(HINSTANCE hInstance)
{
    g_hInstance = hInstance;

    const wchar_t* class_name = L"JustInTimeTrayClass";

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));

    wc.lpfnWndProc   = tray_wndproc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = class_name;

    RegisterClassW(&wc);

    g_hwnd = CreateWindowW(
        class_name,
        L"JustInTime",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        NULL,
        hInstance,
        NULL
    );

    if (!g_hwnd)
        return NULL;

    memset(&g_nid, 0, sizeof(g_nid));

    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = g_hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon            = LoadIcon(NULL, IDI_APPLICATION);

    wcscpy_s(
        g_nid.szTip,
        sizeof(g_nid.szTip) / sizeof(wchar_t),
        L"JustInTime"
    );

    Shell_NotifyIconW(NIM_ADD, &g_nid);

    update_tooltip();

    return g_hwnd;
}

int tray_is_paused(void)
{
    return g_paused;
}

void tray_cleanup(void)
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}
