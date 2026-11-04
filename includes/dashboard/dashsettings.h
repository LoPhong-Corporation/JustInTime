//
// dashsettings.h
// Cổng từ internal/dashsettings/dashsettings.go: đọc/ghi CHÍNH XÁC file
// %APPDATA%\JustInTime\dashboard_settings.json mà dashboard Python/Go
// trước đây dùng - font/ngôn ngữ/ngưỡng cảnh báo tự mang sang khi
// chuyển bản build. Gemini API key được mã hoá DPAPI trên đĩa (trường
// "gemini_api_key_enc", base64), KHÔNG BAO GIỜ ở dạng plaintext.
//
#ifndef DASHBOARD_SETTINGS_H
#define DASHBOARD_SETTINGS_H

#include <string>

namespace jit::dash {

struct DashSettings {
    std::string font = "sans";
    std::string language = "en";
    int cpuThreshold = 85;
    int ramThreshold = 85;
    int diskThreshold = 90;
    std::string timeFormat = "24h";
    std::string defaultPeriod = "today";

    // Chỉ có trong bộ nhớ (giải mã lúc load) - KHÔNG BAO GIỜ marshal
    // thẳng ra JSON; save() tự mã hoá riêng vào gemini_api_key_enc.
    std::string geminiApiKey;

    bool hasGeminiApiKey() const { return !geminiApiKey.empty(); }
};

// name -> chuỗi font-family CSS - khớp fontStacks trong dashsettings.go.
std::string fontStack(const std::string& name);

DashSettings loadDashSettings();
bool saveDashSettings(const DashSettings& s);

} // namespace jit::dash

#endif
