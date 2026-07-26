//
// backup.h
// Tính năng backup dữ liệu cục bộ ra file JSON,
// độc lập với việc đồng bộ (sync) lên Supabase.
//

#ifndef BACKUP_H
#define BACKUP_H

#ifdef __cplusplus
extern "C" {
#endif


/*
 * Tạo một bản backup mới (file JSON có timestamp)
 * trong thư mục BACKUP_DIR, đồng thời tự động dọn
 * dẹp các bản backup cũ vượt quá BACKUP_KEEP_COUNT.
 *
 * Trả về 1 nếu thành công, 0 nếu thất bại.
 */
int backup_create_snapshot(void);


#ifdef __cplusplus
}
#endif

#endif
