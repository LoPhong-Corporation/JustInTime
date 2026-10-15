//
// Created by LoPhongCorporation on 6/24/2026.
//

#ifndef SYNC_H
#define SYNC_H

#ifdef __cplusplus
extern "C" {
#endif


#include <wchar.h>

#define MAX_RECORDS 100

typedef struct
{
    int id;

    char device_id[128];

    wchar_t process_name[512];

    wchar_t window_title[2048];

    long duration_seconds;

    long long start_time;
    long long end_time;

} SyncRecord;

/*
 * Đồng bộ các record chưa sync lên cloud (gửi theo lô, tự cô lập record
 * lỗi - xem batchsend.h). Chặn (blocking) theo I/O mạng: KHÔNG được gọi
 * từ luồng theo dõi hoạt động/GUI - main.cpp chạy nó ở luồng "cloud"
 * riêng.
 *
 * Trả về 0 nếu bình thường (kể cả không có gì để gửi / chưa đăng nhập),
 * -1 nếu lượt sync bị dừng sớm vì mất mạng/server lỗi - caller nên giãn
 * nhịp thử lại (backoff) thay vì cứ 30 giây gõ cửa 1 lần.
 */
int sync_pending_records(void);


#ifdef __cplusplus
}
#endif

#endif
