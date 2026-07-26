//
// parentlink.h
//
// API cho mô hình liên kết phụ huynh-con CÓ ĐỒNG THUẬN:
//   - Phụ huynh gửi lời mời bằng email của con (trạng thái
//     "pending").
//   - Con phải CHỦ ĐỘNG bấm đồng ý (approve) thì phụ huynh mới
//     xem được dữ liệu của con - không có đường nào bỏ qua
//     bước này (thực thi bằng RLS phía Supabase, xem
//     migrations/004_parent_links.sql).
//   - Con có thể thu hồi (revoke) bất kỳ lúc nào, kể cả sau khi
//     đã approve từ trước.
//
// Dùng chung cho cả 2 vai trò: agent (con) gọi list_as_child/
// approve/revoke; parent gọi invite_child/list_as_parent.
//

#ifndef PARENTLINK_H
#define PARENTLINK_H

#ifdef __cplusplus
extern "C" {
#endif


#define MAX_LINK_UUID   40
#define MAX_LINK_EMAIL  256
#define MAX_LINK_STATUS 16
#define MAX_LINKS       32

typedef struct
{
    long id;

    /*
     * Nếu list_as_parent(): other_user_id/other_email là của
     * NGƯỜI CON. Nếu list_as_child(): là của NGƯỜI PHỤ HUYNH.
     */
    char other_user_id[MAX_LINK_UUID];
    char other_email[MAX_LINK_EMAIL];

    char status[MAX_LINK_STATUS]; /* "pending" | "approved" | "revoked" */
} ParentLink;

/*
 * (Vai trò phụ huynh) Gửi lời mời giám sát tới tài khoản có
 * email này. Tạo 1 dòng parent_links trạng thái "pending" -
 * CHƯA xem được gì cho tới khi con approve.
 *
 * Trả về 1 nếu tạo lời mời thành công, 0 nếu thất bại (email
 * không tồn tại, đã có lời mời/liên kết trước đó, hoặc lỗi
 * mạng) - err_out được điền thông báo lý do, đủ để hiển thị
 * thẳng cho người dùng.
 */
int parentlink_invite_child(
    const char* child_email,
    char* err_out, int err_out_size
);

/*
 * (Vai trò phụ huynh) Liệt kê tất cả các con đã/đang liên kết
 * (mọi trạng thái: pending/approved/revoked).
 * Trả về số dòng lấy được (0 nếu lỗi hoặc chưa có gì).
 */
int parentlink_list_as_parent(
    ParentLink* out, int max_out
);

/*
 * (Vai trò con/agent) Liệt kê tất cả phụ huynh đang xin/đã
 * được xem mình (mọi trạng thái) - dùng cho mục minh bạch
 * "Được giám sát bởi..." trong Settings, LUÔN hiển thị đầy đủ
 * kể cả các lời mời đang chờ, không giấu bất cứ điều gì.
 */
int parentlink_list_as_child(
    ParentLink* out, int max_out
);

/*
 * (Vai trò con/agent) Đồng ý cho 1 phụ huynh xem hoạt động
 * của mình.
 */
int parentlink_approve(
    long link_id,
    char* err_out, int err_out_size
);

/*
 * (Vai trò con/agent) Thu hồi quyền xem - dùng được cả khi
 * đang "pending" (từ chối lời mời) lẫn khi đã "approved" từ
 * trước (ngừng chia sẻ dữ liệu với phụ huynh đó).
 */
int parentlink_revoke(
    long link_id,
    char* err_out, int err_out_size
);


#ifdef __cplusplus
}
#endif

#endif
