//
// settings.h
// Quản lý cấu hình người dùng, lưu tại
// %APPDATA%\JustInTime\settings.ini
//

#ifndef SETTINGS_H
#define SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif


#include <wchar.h>

#define MAX_EXCLUDED_LEN   1024
#define MAX_URL_LEN         256
#define MAX_KEY_LEN         512

typedef struct
{
    int  sync_interval_sec;
    int  backup_interval_sec;
    int  summary_interval_sec;
    int  min_duration_sec;
    int  autostart_enabled;

    /*
     * Retry Queue: backoff cơ bản (giây) và giới hạn tối đa
     * (giây) khi 1 record gửi lên cloud thất bại liên tiếp.
     * next_retry = base * 2^so_lan_that_bai, giới hạn bởi max.
     */
    int  retry_backoff_base_sec;
    int  retry_backoff_max_sec;

    /*
     * Remote View: cho phép xem số liệu real-time trực tiếp
     * từ agent qua HTTP cục bộ, KHÔNG qua cloud, CHỈ ĐỌC
     * (không có endpoint nào thay đổi được gì).
     * Tắt mặc định - phải chủ động bật.
     * Bảo vệ bằng token ngẫu nhiên (giống mã PIN dùng chung).
     * LƯU Ý: đây là HTTP thường, không mã hóa - chỉ nên dùng
     * trong mạng LAN/VPN tin cậy, không nên mở thẳng ra
     * Internet công cộng.
     */
    int  remote_view_enabled;
    int  remote_view_port;
    char remote_view_token[64];

    /*
     * Danh sách tên process bị loại trừ khỏi theo dõi,
     * cách nhau bởi dấu phẩy, không phân biệt hoa thường.
     * Ví dụ: "steam.exe,discord.exe"
     */
    char excluded_processes[MAX_EXCLUDED_LEN];

    /*
     * Cấu hình Supabase có thể đổi từ menu "Setup Supabase"
     * mà không cần build lại app. Nếu để trống sẽ dùng
     * giá trị mặc định trong config.h.
     */
    char supabase_url[MAX_URL_LEN];
    char supabase_key[MAX_KEY_LEN];

} AppSettings;

/*
 * Nạp settings từ file. Nếu file chưa tồn tại, tạo file
 * mới với giá trị mặc định và trả về giá trị mặc định đó.
 */
void settings_load(AppSettings* out);

/*
 * Ghi settings xuống file.
 * Trả về 1 nếu thành công, 0 nếu thất bại.
 */
int settings_save(const AppSettings* s);

/*
 * Lấy con trỏ tới bản settings đang cache trong bộ nhớ
 * (thread-safe, dùng critical section nội bộ để đọc).
 * out phải là bản sao (copy), không phải con trỏ sống,
 * để tránh tranh chấp giữa UI thread và worker thread.
 */
void settings_get(AppSettings* out);

/*
 * Cập nhật settings đang chạy (dùng khi lưu từ dialog Cài đặt)
 * và ghi xuống file luôn.
 */
int settings_update(const AppSettings* s);

/*
 * Kiểm tra 1 process có nằm trong danh sách loại trừ không.
 * process_name dạng wide-char (lấy từ ActiveWindow).
 */
int settings_is_process_excluded(const wchar_t* process_name);

/*
 * Đường dẫn thư mục cấu hình (%APPDATA%\JustInTime).
 * Trả về 1 nếu lấy được, 0 nếu thất bại.
 */
int settings_get_config_dir(char* out, int out_size);

/*
 * Lấy URL + key Supabase hiệu lực: ưu tiên giá trị đã lưu
 * trong settings.ini (từ menu "Setup Supabase"), nếu chưa
 * cấu hình thì dùng giá trị mặc định trong config.h.
 */
void settings_get_supabase_config(
    char* url_out, int url_out_size,
    char* key_out, int key_out_size
);

/*
 * Bật/tắt tự khởi động cùng Windows bằng cách ghi/xóa
 * registry key HKCU\...\Run. Gọi mỗi khi autostart_enabled
 * thay đổi trong settings_update().
 */
int settings_apply_autostart(int enabled);


#ifdef __cplusplus
}
#endif

#endif
