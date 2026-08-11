//
// device.cpp
//
// CHUYỂN TỪ C SANG C++ (bản EXPERIMENTAL) - giữ nguyên interface extern "C" trong
// device.h). Về hành vi, KHÔNG đổi gì so với bản C trước (đọc kỹ
// phần mô tả gốc bên dưới nếu cần) - chỉ đổi cách viết:
//   - std::mutex (qua 1 static local "magic static", an toàn luồng
//     theo chuẩn C++11 trở lên) thay cho CRITICAL_SECTION +
//     "ensure_device_lock()" tự viết tay - bản C cũ có 1 race
//     condition LÝ THUYẾT: nếu 2 luồng cùng gọi ensure_device_lock()
//     đồng thời trước khi InitializeCriticalSection() chạy xong, đó
//     là hành vi không xác định. Magic static của C++ được chuẩn
//     đảm bảo khởi tạo đúng 1 lần, an toàn luồng, không cần code tự
//     viết.
//   - std::string thay cho buffer char[] cấp phát tay ở phần lõi -
//     vẫn chuyển sang buffer C-style ở biên ngoài (get_device_id() -
//     interface public không đổi).
//
// MÔ TẢ GỐC (không đổi):
// TRƯỚC ĐÂY: device_id = GetComputerNameA() thẳng, tức là tên máy
// Windows thật của người dùng (ví dụ "PHONG-LAPTOP", hay tệ hơn là
// tên đầy đủ nếu ai đó đặt tên máy kiểu "NGUYEN-VAN-A-PC"). Giá trị
// này được ghi vào MỌI activity_logs, đồng bộ lên Supabase, và hiển
// thị thẳng trên dashboard (kể cả cho tài khoản phụ huynh xem máy
// con) - rò rỉ thông tin cá nhân không cần thiết.
//
// BÂY GIỜ: device_id là 1 chuỗi ngẫu nhiên dạng "PC-XXXXXXXX" (8 ký
// tự hex), không liên quan gì tới tên máy Windows thật. Sinh 1 LẦN
// DUY NHẤT ở lần chạy đầu tiên, lưu vào
//   %APPDATA%\JustInTime\device.id
// rồi dùng lại y nguyên ở các lần sau. Người dùng có thể tự đặt
// thêm 1 "nhãn hiển thị" (device_get_label/device_set_label), nhưng
// đây là lựa chọn CHỦ ĐỘNG - không bao giờ tự động lấy tên máy
// Windows làm giá trị mặc định.
//

#include "device.h"
#include "settings.h"

#include <windows.h>
#include <wincrypt.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <fstream>
#include <sstream>

namespace {

std::mutex& deviceMutex()
{
    // "Magic static" - C++11 trở lên đảm bảo khởi tạo đúng 1 lần,
    // an toàn khi nhiều luồng cùng gọi lần đầu (khác CRITICAL_SECTION
    // tự quản ở bản C cũ).
    static std::mutex m;
    return m;
}

std::string g_cachedId;
std::string g_cachedLabel;
bool        g_cacheLoaded = false;

std::string deviceFilePath()
{
    char dir[MAX_PATH] = {0};

    if (!settings_get_config_dir(dir, sizeof(dir)))
        return "";

    return std::string(dir) + "\\device.id";
}

/*
 * Sinh 4 byte ngẫu nhiên bằng CSPRNG của Windows (không dùng rand()
 * - không đủ ngẫu nhiên cho việc này) rồi in ra dạng hex 8 ký tự.
 */
std::string generateRandomId()
{
    BYTE rnd[4] = {0};
    HCRYPTPROV prov = 0;

    if (CryptAcquireContextW(
            &prov, NULL, NULL,
            PROV_RSA_FULL,
            CRYPT_VERIFYCONTEXT
        ) &&
        CryptGenRandom(prov, sizeof(rnd), rnd))
    {
        CryptReleaseContext(prov, 0);
    }
    else
    {
        /* Cực hiếm khi xảy ra, nhưng vẫn phải trả về 1 giá trị nào
         * đó thay vì để buffer rỗng - fallback dùng thời gian +
         * GetCurrentProcessId() làm nguồn ngẫu nhiên yếu hơn. */
        DWORD t = GetTickCount();
        DWORD pid = GetCurrentProcessId();
        rnd[0] = static_cast<BYTE>(t & 0xFF);
        rnd[1] = static_cast<BYTE>((t >> 8) & 0xFF);
        rnd[2] = static_cast<BYTE>(pid & 0xFF);
        rnd[3] = static_cast<BYTE>((pid >> 8) & 0xFF);
    }

    char buf[16] = {0};
    snprintf(buf, sizeof(buf), "PC-%02X%02X%02X%02X", rnd[0], rnd[1], rnd[2], rnd[3]);
    return buf;
}

std::string trimNewline(std::string s)
{
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
    return s;
}

bool writeDeviceFile(const std::string& path, const std::string& id, const std::string& label)
{
    if (path.empty())
        return false;

    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f)
        return false;

    f << id << "\n" << label << "\n";
    return true;
}

/*
 * Đọc file device.id (2 dòng: id, rồi label - label có thể vắng
 * mặt/rỗng). Nếu file không tồn tại hoặc dòng đầu không đúng định
 * dạng "PC-XXXXXXXX", coi như chưa có gì, sinh mới và ghi lại.
 * GỌI KHI ĐÃ GIỮ deviceMutex().
 */
void loadOrCreate()
{
    if (g_cacheLoaded)
        return;

    const std::string path = deviceFilePath();
    g_cachedId.clear();
    g_cachedLabel.clear();

    if (!path.empty())
    {
        std::ifstream f(path);
        if (f)
        {
            std::string line1, line2;
            std::getline(f, line1);
            std::getline(f, line2);

            line1 = trimNewline(line1);
            line2 = trimNewline(line2);

            if (line1.rfind("PC-", 0) == 0 && line1.size() == 11)
                g_cachedId = line1;

            g_cachedLabel = line2;
        }
    }

    if (g_cachedId.empty())
    {
        g_cachedId = generateRandomId();
        writeDeviceFile(path, g_cachedId, g_cachedLabel);
    }

    g_cacheLoaded = true;
}

void copyToBuffer(const std::string& s, char* buffer, int size)
{
    strncpy_s(buffer, static_cast<size_t>(size), s.c_str(), _TRUNCATE);
}

} // namespace

void get_device_id(
    char* buffer,
    int size)
{
    if (!buffer || size <= 0)
        return;

    std::lock_guard<std::mutex> lock(deviceMutex());
    loadOrCreate();
    copyToBuffer(g_cachedId, buffer, size);
}

void device_get_label(
    char* buffer,
    int size)
{
    if (!buffer || size <= 0)
        return;

    std::lock_guard<std::mutex> lock(deviceMutex());
    loadOrCreate();

    if (!g_cachedLabel.empty())
    {
        copyToBuffer(g_cachedLabel, buffer, size);
        return;
    }

    /*
     * KHÔNG dùng tên máy Windows làm mặc định - chỉ dùng 1 nhãn
     * chung chung kèm 4 ký tự cuối của device_id, đủ để phân biệt
     * nhiều máy trên cùng 1 tài khoản mà không tiết lộ gì về người
     * dùng.
     */
    std::string suffix = g_cachedId.size() >= 4
        ? g_cachedId.substr(g_cachedId.size() - 4)
        : g_cachedId;

    std::string label = "Computer " + suffix;
    copyToBuffer(label, buffer, size);
}

int device_set_label(
    const char* label)
{
    std::lock_guard<std::mutex> lock(deviceMutex());
    loadOrCreate();

    g_cachedLabel = label ? label : "";

    const std::string path = deviceFilePath();
    return writeDeviceFile(path, g_cachedId, g_cachedLabel) ? 1 : 0;
}
