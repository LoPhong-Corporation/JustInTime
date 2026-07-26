//
// Created by LoPhongCorporation on 6/24/2026.
//

#ifndef DATABASE_H
#define DATABASE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sync.h"
#include "activity.h"

int db_init(void);

int db_insert_activity(
    const ActivityRecord* record
);

void db_print_unsynced(void);

/*
 * In báo cáo tổng thời gian sử dụng mỗi app trong ngày
 * hôm nay ra console (gộp các record dù bị chia nhỏ theo
 * từng lần đổi window title).
 */
void db_print_daily_summary(void);

/*
 * Build nội dung báo cáo tổng hợp theo app hôm nay vào
 * buffer, dùng để hiển thị trong MessageBox của tray.
 * Trả về 1 nếu có dữ liệu, 0 nếu chưa có.
 */
int db_build_daily_summary_text(
    wchar_t* out,
    int out_size
);

/*
 * Tổng số giây đã dùng HÔM NAY cho 1 process cụ thể (tính từ
 * 00:00 giờ địa phương) - dùng để đối chiếu với app_limits khi
 * kiểm tra giới hạn thời gian do phụ huynh đặt (xem
 * activity_check_limits() trong core/activity.c).
 */
long db_get_today_seconds(
    const wchar_t* process_name
);

void db_close(void);

int db_get_unsynced_records(
    SyncRecord* records,
    int max_records
);

int db_mark_synced(int id);

/*
 * Đánh dấu 1 record gửi thất bại: tăng retry_count và
 * tính lại thời điểm thử lại tiếp theo (exponential backoff).
 */
int db_mark_sync_failed(int id);

int db_count_unsynced(void);

int db_delete_old_records(int days);

int db_mark_record_synced(
    int id
);

/*
 * Xuất toàn bộ dữ liệu ra file JSON (backup cục bộ).
 * Trả về 1 nếu thành công, 0 nếu thất bại.
 */
int db_export_json(
    const char* filepath
);


#ifdef __cplusplus
}
#endif

#endif