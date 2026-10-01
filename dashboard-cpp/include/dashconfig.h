//
// config.h
// Cổng 1:1 từ internal/config/config.go. Đường dẫn cấu hình dùng CHUNG
// %APPDATA%\JustInTime với agent C++ (jit::configFile trong
// shared/paths.h) và CHUNG file device.id (định dạng 2 dòng: device_id +
// nhãn tuỳ chọn) mà src/shared/device.cpp của agent đọc/ghi - máy nào
// chạy trước (agent hay dashboard) tạo file, máy kia chỉ đọc lại, nên
// device_id luôn khớp nhau giữa 2 tiến trình (cần cho nhắn tin liên thiết
// bị, vốn định danh theo device_id).
//
#ifndef DASHBOARD_CONFIG_H
#define DASHBOARD_CONFIG_H

#include <string>

namespace jit::dash {

constexpr int kDefaultPort = 5000;
constexpr const char* kSupabaseURL = "https://crdvfasjtrfrasqehwkc.supabase.co";
constexpr const char* kSupabaseAnonKey = "sb_publishable_2BDazw0ggLN0GC9Zyu2hOQ_XrcqaR7v";

struct Config {
    int port = kDefaultPort;
    std::wstring configDir;   // %APPDATA%\JustInTime
    std::string localDbPathUtf8;
    std::string deviceId;
    std::string deviceLabel;  // nhãn đã có LÚC KHỞI ĐỘNG - xem DisplayLabel()
    std::string supabaseURL = kSupabaseURL;
    std::string supabaseKey = kSupabaseAnonKey;

    // FIX (staleness, xem config.go bản gốc): KHÔNG dùng deviceLabel ở
    // trên cho hiển thị định kỳ (heartbeat) - hàm này đọc lại thẳng từ
    // device.id mỗi lần gọi, để đổi tên máy từ tray có hiệu lực ngay mà
    // không cần khởi động lại dashboard.
    std::string displayLabel() const;
};

// Nạp cấu hình cho lần chạy này (tạo config dir + đọc/tạo device.id nếu cần).
Config load();

// Ghi nhãn máy do người dùng đặt vào device.id (giữ nguyên device_id).
bool setDeviceLabel(const std::string& id, const std::string& label);

} // namespace jit::dash

#endif
