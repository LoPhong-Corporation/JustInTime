//
// ui_dialog.h
// Hộp thoại nhập liệu đa năng (không cần file .rc),
// dùng chung cho Login/Register, Cài đặt, Setup Supabase.
//

#ifndef UI_DIALOG_H
#define UI_DIALOG_H

#include <windows.h>

#define MAX_DIALOG_FIELDS   8
#define MAX_FIELD_VALUE_LEN 512

typedef struct
{
    const wchar_t* label;
    wchar_t        value[MAX_FIELD_VALUE_LEN];
    int            is_password;
    int            is_checkbox;  /* 1 = checkbox thay vì edit box */
} DialogField;

/*
 * Hiện hộp thoại modal với danh sách field, chờ người
 * dùng bấm OK/Cancel.
 *
 * fields[i].value: giá trị ban đầu (input), và sẽ được
 * ghi đè bằng giá trị người dùng nhập (output) nếu bấm OK.
 *
 * Trả về 1 nếu bấm OK, 0 nếu Cancel/đóng cửa sổ.
 */
int show_input_dialog(
    HINSTANCE hInstance,
    const wchar_t* title,
    DialogField* fields,
    int field_count
);

/*
 * Hiện thông báo đơn giản (thay cho MessageBoxW, gói lại
 * cho gọn lời gọi + tiêu đề mặc định "JustInTime").
 */
void show_info_message(const wchar_t* text);
void show_error_message(const wchar_t* text);

#endif
