//
// ui_dialog.c
//

#include "../include/ui_dialog.h"

#include <string.h>

static int          g_dialog_result = -1;
static DialogField* g_dialog_fields = NULL;
static int          g_dialog_field_count = 0;
static HWND         g_dialog_controls[MAX_DIALOG_FIELDS];

static LRESULT CALLBACK dialog_wndproc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (msg)
    {
        case WM_COMMAND:
        {
            if (HIWORD(wParam) != BN_CLICKED)
                break;

            int id = LOWORD(wParam);

            if (id == 1) /* OK */
            {
                for (int i = 0; i < g_dialog_field_count; i++)
                {
                    if (g_dialog_fields[i].is_checkbox)
                    {
                        LRESULT checked = SendMessageW(
                            g_dialog_controls[i],
                            BM_GETCHECK,
                            0, 0
                        );

                        wcscpy_s(
                            g_dialog_fields[i].value,
                            MAX_FIELD_VALUE_LEN,
                            checked == BST_CHECKED ? L"1" : L"0"
                        );
                    }
                    else
                    {
                        GetWindowTextW(
                            g_dialog_controls[i],
                            g_dialog_fields[i].value,
                            MAX_FIELD_VALUE_LEN
                        );
                    }
                }

                g_dialog_result = 1;
                DestroyWindow(hwnd);
            }
            else if (id == 2) /* Cancel */
            {
                g_dialog_result = 0;
                DestroyWindow(hwnd);
            }

            return 0;
        }

        case WM_CLOSE:
            g_dialog_result = 0;
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int show_input_dialog(
    HINSTANCE hInstance,
    const wchar_t* title,
    DialogField* fields,
    int field_count)
{
    static int class_registered = 0;

    const wchar_t* class_name = L"JustInTimeDialogClass";

    if (!class_registered)
    {
        WNDCLASSW wc;
        memset(&wc, 0, sizeof(wc));

        wc.lpfnWndProc   = dialog_wndproc;
        wc.hInstance     = hInstance;
        wc.lpszClassName = class_name;
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);

        RegisterClassW(&wc);

        class_registered = 1;
    }

    if (field_count > MAX_DIALOG_FIELDS)
        field_count = MAX_DIALOG_FIELDS;

    const int width  = 460;
    const int row_h  = 50;
    const int height = 80 + field_count * row_h + 70;

    /*
     * Căn giữa màn hình cho dễ nhìn, thay vì để Windows
     * tự chọn vị trí (có thể ra góc trên trái, khó thấy
     * với app chạy nền không có cửa sổ chính).
     */
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);

    int pos_x = (screen_w - width) / 2;
    int pos_y = (screen_h - height) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        class_name,
        title,
        WS_CAPTION | WS_SYSMENU | WS_POPUP,
        pos_x, pos_y,
        width, height,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd)
        return 0;

    HICON icon = LoadIcon(NULL, IDI_APPLICATION);
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);

    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    g_dialog_fields      = fields;
    g_dialog_field_count = field_count;
    g_dialog_result      = -1;

    int y = 24;

    for (int i = 0; i < field_count; i++)
    {
        HWND label = CreateWindowW(
            L"STATIC", fields[i].label,
            WS_CHILD | WS_VISIBLE,
            24, y + 4, 200, 20,
            hwnd, NULL, hInstance, NULL
        );

        SendMessageW(label, WM_SETFONT, (WPARAM)font, TRUE);

        if (fields[i].is_checkbox)
        {
            g_dialog_controls[i] = CreateWindowW(
                L"BUTTON", L"",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                232, y + 2, 20, 20,
                hwnd, (HMENU)(INT_PTR)(2000 + i), hInstance, NULL
            );

            SendMessageW(
                g_dialog_controls[i],
                BM_SETCHECK,
                wcscmp(fields[i].value, L"1") == 0
                    ? BST_CHECKED
                    : BST_UNCHECKED,
                0
            );
        }
        else
        {
            DWORD style =
                WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP |
                (fields[i].is_password ? ES_PASSWORD : 0);

            g_dialog_controls[i] = CreateWindowW(
                L"EDIT", fields[i].value,
                style,
                232, y, width - 232 - 24, 26,
                hwnd, (HMENU)(INT_PTR)(2000 + i), hInstance, NULL
            );
        }

        SendMessageW(g_dialog_controls[i], WM_SETFONT, (WPARAM)font, TRUE);

        y += row_h;
    }

    const int btn_w = 100;
    const int btn_h = 32;
    const int btn_gap = 16;
    const int btn_total_w = btn_w * 2 + btn_gap;
    const int btn_x = (width - btn_total_w) / 2 - 8;

    HWND btn_ok = CreateWindowW(
        L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
        btn_x, y + 16, btn_w, btn_h,
        hwnd, (HMENU)(INT_PTR)1, hInstance, NULL
    );

    HWND btn_cancel = CreateWindowW(
        L"BUTTON", L"Hủy",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        btn_x + btn_w + btn_gap, y + 16, btn_w, btn_h,
        hwnd, (HMENU)(INT_PTR)2, hInstance, NULL
    );

    SendMessageW(btn_ok, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageW(btn_cancel, WM_SETFONT, (WPARAM)font, TRUE);

    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);

    if (field_count > 0)
        SetFocus(g_dialog_controls[0]);

    MSG msg;

    while (
        g_dialog_result == -1 &&
        GetMessage(&msg, NULL, 0, 0)
    )
    {
        if (!IsDialogMessageW(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return g_dialog_result == 1 ? 1 : 0;
}

void show_info_message(const wchar_t* text)
{
    MessageBoxW(
        NULL,
        text,
        L"JustInTime",
        MB_OK | MB_ICONINFORMATION | MB_TOPMOST
    );
}

void show_error_message(const wchar_t* text)
{
    MessageBoxW(
        NULL,
        text,
        L"JustInTime",
        MB_OK | MB_ICONWARNING | MB_TOPMOST
    );
}
