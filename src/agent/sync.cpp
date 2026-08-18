//
// sync.cpp
//
// Đã CHUYỂN TỪ C SANG C++ (giữ nguyên interface
// extern "C" trong sync.h), hành vi 100% giống bản C gốc trước khi chuyển đổi. File này
// vốn đã đơn giản (1 vòng lặp gọi các hàm khác) - không có gì để
// dùng RAII/std::string, chỉ đổi văn phong nhẹ (nullptr, kiểu dữ
// liệu rõ ràng hơn) để đồng bộ với các module .cpp khác.
//

#include "sync.h"
#include "database.h"
#include "network.h"
#include "auth.h"
#include "error_codes.h"

#include <cstdio>

void sync_pending_records(void)
{
    if (!auth_is_logged_in())
    {
        wprintf(
            L"[SYNC][%hs] Chua dang nhap, du lieu van luu local, "
            L"dang nhap qua tray de dong bo len cloud\n",
            ERR_SYNC_NOT_LOGGED_IN
        );
        return;
    }

    SyncRecord records[MAX_RECORDS];
    const int count = db_get_unsynced_records(records, MAX_RECORDS);

    if (count == 0)
    {
        wprintf(L"[SYNC] No pending records\n");
        return;
    }

    wprintf(L"[SYNC] Found %d record(s)\n", count);

    for (int i = 0; i < count; i++)
    {
        if (network_send_record(&records[i]))
        {
            if (db_mark_record_synced(records[i].id))
                wprintf(L"[SYNC] Record %d synced\n", records[i].id);
            else
                wprintf(L"[SYNC] Failed to update database for record %d\n", records[i].id);
        }
        else
        {
            db_mark_sync_failed(records[i].id);
            wprintf(L"[SYNC] Send failed: %d (se tu thu lai sau, backoff tang dan)\n", records[i].id);
        }
    }
}
