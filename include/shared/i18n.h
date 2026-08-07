//
// i18n.h
// Bảng dịch nhẹ cho tray + dialogs (Qt/C++). Ngôn ngữ lấy từ
// settings_get().language (APP_LANG_EN mặc định, APP_LANG_VI
// tuỳ chọn), đổi trong Settings và áp dụng ngay không cần
// khởi động lại app (xem TrayIcon::rebuildMenu()).
//
// Đây KHÔNG phải i18n đầy đủ cho toàn bộ ứng dụng - hiện chỉ
// phủ tray menu, About, Settings (phần vai trò/ngôn ngữ),
// ParentPage, ParentLinkPage, Control Panel (sidebar + Overview).
// Các trang cũ hơn (Account/Login, Supabase Setup, Remote View,
// phần còn lại của Settings) vẫn còn tiếng Anh cứng - có thể bổ
// sung dần bằng cách thêm entry vào bảng trong i18n.c và gọi
// i18n_t("key") thay cho literal.
//

#ifndef I18N_H
#define I18N_H

#ifdef __cplusplus
extern "C" {
#endif


/*
 * Trả về chuỗi UTF-8 đã dịch ứng với "key", theo ngôn ngữ hiện
 * tại. Nếu key không có trong bảng, trả về chính "key" đó (dễ
 * nhận ra ngay chỗ nào quên thêm bản dịch thay vì crash hay
 * hiện chuỗi rỗng).
 */
const char* i18n_t(const char* key);


#ifdef __cplusplus
}
#endif

#endif
