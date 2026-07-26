//
// Created by LoPhongCorporation on 6/24/2026.
//

#ifndef CONFIG_H
#define CONFIG_H

/*
 * Thông tin phiên bản, hiển thị ở tray > About và dùng để so
 * sánh với bảng "app_releases" trên Supabase khi kiểm tra
 * cập nhật (xem updatechecker.cpp). Nhớ tăng số này mỗi khi
 * phát hành bản mới, khớp với cột "version" bạn thêm vào
 * bảng app_releases.
 */
#define APP_VERSION   "1.0.0"
#define APP_PUBLISHER "LoPhong Corporation"
#define APP_WEBSITE   "https://example.com"

#define SUPABASE_URL \
"https://crdvfasjtrfrasqehwkc.supabase.co"

/*
 * QUAN TRỌNG: từ cuối 2025, các project Supabase mới
 * không còn dùng anon key kiểu JWT (eyJ...) nữa.
 * Đây là Publishable key mới (sb_publishable_...),
 * dùng thay thế hoàn toàn cho anon key cũ.
 * Macro vẫn tên SUPABASE_ANON_KEY để không phải sửa
 * lại các chỗ khác trong code (network.c) đang dùng nó.
 */
#define SUPABASE_ANON_KEY \
"sb_publishable_2BDazw0ggLN0GC9Zyu2hOQ_XrcqaR7v"

/*
 * Tên bảng trên Supabase (chỉ để tham khảo/dùng trong SQL,
 * không còn dùng trực tiếp trong network.c vì client giờ gọi
 * qua Edge Function "sync-activity" thay vì gọi thẳng bảng).
 */
#define SUPABASE_TABLE "activity_logs"

/*
 * Thư mục lưu các bản backup cục bộ (JSON)
 */
#define BACKUP_DIR "backups"

/*
 * Số lượng file backup gần nhất được giữ lại,
 * các file cũ hơn sẽ tự động bị xóa.
 */
#define BACKUP_KEEP_COUNT 14

/*
 * Số ngày giữ lại bản ghi ĐÃ đồng bộ trong DB local
 * trước khi dọn dẹp (không bao giờ xóa bản ghi chưa synced).
 */
#define RETENTION_DAYS 30

#endif