//
// machines.cpp
//
// Đã CHUYỂN TỪ C SANG C++ (giữ nguyên interface extern "C" trong
// machines.h - ControlPanelWindow.cpp và main.cpp gọi vào không cần
// đổi gì). Hành vi giữ nguyên 100% so với bản C trước - xem
// machines.h để biết lý do file này tồn tại (agent tự đẩy heartbeat,
// không phụ thuộc dashboard web có mở hay không).
//
// Đổi cách viết: dùng std::string cho phần dựng JSON body/escape
// thay vì buffer char[] cấp phát tay ở NHIỀU bước trung gian (bản C
// cũ có tới 4-5 buffer riêng: device_id, label, *_esc, body...) -
// gộp lại còn ít bước hơn, rõ ràng hơn.
//

#include "machines.h"
#include "restclient.h"
#include "jsonutil.h"
#include "auth.h"
#include "device.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

std::string jsonEscape(const std::string& s)
{
    // json_escape() có thể mở rộng mỗi ký tự tối đa 6 lần (dạng
    // "\u00XX") - cấp đủ chỗ để không bao giờ bị cắt bớt kết quả
    // (json_escape() tự kiểm tra biên an toàn nếu buffer vẫn thiếu,
    // nhưng ở đây cấp dư để không cần lo trường hợp đó xảy ra).
    std::string out(s.size() * 6 + 16, '\0');
    json_escape(s.c_str(), out.data(), out.size());
    out.resize(strlen(out.c_str()));
    return out;
}

} // namespace

int machines_push_heartbeat(void)
{
    if (!auth_is_logged_in())
        return 0;

    char deviceIdBuf[32] = {0};
    char labelBuf[128] = {0};

    get_device_id(deviceIdBuf, sizeof(deviceIdBuf));
    device_get_label(labelBuf, sizeof(labelBuf));

    const std::string deviceIdEsc = jsonEscape(deviceIdBuf);
    const std::string labelEsc = jsonEscape(labelBuf);

    /*
     * Không gửi cpu_percent/ram_percent/disk_percent thật (agent C
     * chưa lấy số liệu này) - gửi 0.0, dashboard Go khi mở lên sẽ tự
     * ghi đè bằng số liệu thật của chính nó. Mục tiêu duy nhất ở đây
     * là last_seen luôn mới.
     */
    char body[512];
    snprintf(
        body, sizeof(body),
        "{\"device_id\":\"%s\",\"hostname\":\"%s\",\"cpu_percent\":0,"
        "\"ram_percent\":0,\"disk_percent\":0,\"last_seen\":\"now()\"}",
        deviceIdEsc.c_str(), labelEsc.c_str()
    );

    char response[512] = {0};
    DWORD status = 0;

    if (
        !restclient_call(
            "POST", L"/rest/v1/device_heartbeats?on_conflict=user_id,device_id",
            body, L"Prefer: resolution=merge-duplicates,return=minimal\r\n",
            response, sizeof(response), &status
        )
    )
        return 0;

    return (status >= 200 && status < 300) ? 1 : 0;
}

int machines_list(
    MachineHeartbeat* out,
    int max_entries)
{
    if (!out || max_entries <= 0)
        return 0;

    if (!auth_is_logged_in())
        return 0;

    char response[8192] = {0};
    DWORD status = 0;

    if (
        !restclient_call(
            "GET", L"/rest/v1/device_heartbeats?select=*&order=last_seen.desc",
            NULL, NULL,
            response, sizeof(response), &status
        )
    )
        return 0;

    if (status < 200 || status >= 300)
        return 0;

    const char* cursor = response;
    char obj[1024];
    int count = 0;

    while (count < max_entries && json_array_next(&cursor, obj, sizeof(obj)))
    {
        MachineHeartbeat* m = &out[count];
        memset(m, 0, sizeof(*m));

        json_extract_string(obj, "device_id", m->device_id, sizeof(m->device_id));
        json_extract_string(obj, "hostname", m->hostname, sizeof(m->hostname));
        json_extract_string(obj, "last_seen", m->last_seen, sizeof(m->last_seen));

        long cpu_l = 0, ram_l = 0, disk_l = 0;
        /* cpu/ram/disk là số thực (double) trong DB - json_extract_long
         * chỉ đọc số nguyên, nhưng đủ dùng để hiển thị gần đúng (làm
         * tròn) trên Overview; không cần độ chính xác thập phân ở
         * đây, khác với dashboard Go (nơi hiển thị số liệu chi tiết
         * hơn nhiều). */
        json_extract_long(obj, "cpu_percent", &cpu_l, NULL);
        json_extract_long(obj, "ram_percent", &ram_l, NULL);
        json_extract_long(obj, "disk_percent", &disk_l, NULL);
        m->cpu_percent = static_cast<double>(cpu_l);
        m->ram_percent = static_cast<double>(ram_l);
        m->disk_percent = static_cast<double>(disk_l);

        count++;
    }

    return count;
}
