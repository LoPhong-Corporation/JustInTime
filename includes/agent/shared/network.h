//
// Created by LoPhongCorporation on 7/2/2026.
//

#ifndef NETWORK_H
#define NETWORK_H

#ifdef __cplusplus
extern "C" {
#endif


#include "sync.h"

/*
 * Gửi một activity lên server.
 * Trả về:
 *  1 = thành công
 *  0 = thất bại
 */
int network_send_record(
    const SyncRecord* record
);

/*
 * Gửi cả 1 LÔ activity trong đúng 1 request HTTPS (Edge Function
 * "sync-activity" nhận cả object đơn lẫn mảng object - xem
 * supabase/functions/sync-activity/index.ts). Nhanh hơn ~N lần so với gửi
 * từng cái: 1 lần xác thực + 1 lần upsert cho cả lô thay vì N lần.
 *
 * Trả về 1 nếu server nhận CẢ lô, 0 nếu không.
 * *transport_error (nếu != NULL) = 1 nghĩa là lỗi là do giao vận/tạm thời
 * (mất mạng, timeout, 408/429/502/503/504, chưa đăng nhập) - caller nên
 * dừng và thử lại sau, KHÔNG nên tính là record bị lỗi. = 0 nghĩa là
 * server đã trả lời và từ chối dữ liệu.
 */
int network_send_batch(
    const SyncRecord* records,
    int count,
    int* transport_error
);


#ifdef __cplusplus
}
#endif

#endif
