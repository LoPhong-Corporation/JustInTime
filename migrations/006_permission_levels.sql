-- ============================================================
-- 005_permission_levels.sql
--
-- Phân quyền theo email cho phụ huynh: mỗi liên kết phụ huynh-con
-- (public.parent_links, xem 004_parent_links.sql - không có trong
-- repo này vì được áp trực tiếp qua Supabase SQL Editor) giờ có
-- thêm 1 mức quyền:
--
--   'full'      - xem hoạt động CON, đặt/xoá giới hạn app (mặc định,
--                  giữ nguyên hành vi cũ cho mọi liên kết đã có sẵn -
--                  không ai bị mất quyền đột ngột khi chạy migration
--                  này).
--   'view_only' - chỉ xem hoạt động, KHÔNG được đặt/xoá giới hạn app.
--                  Hữu ích khi 1 tài khoản người lớn khác (ông bà,
--                  người trông trẻ...) chỉ cần theo dõi chứ không nên
--                  tự ý chặn app của trẻ.
--
-- Chạy file này trong Supabase Dashboard > SQL Editor, SAU KHI đã
-- có bảng parent_links.
--
-- LƯU Ý: file này CHỈ thêm cột + ràng buộc, KHÔNG động tới các RPC
-- sẵn có (parent_links_for_parent/parent_links_for_child) vì SQL
-- gốc của chúng không có trong repo này - ứng dụng (dashboard-go)
-- đọc/ghi permission_level bằng cách SELECT/PATCH thẳng vào bảng
-- parent_links (không qua RPC), tái sử dụng policy UPDATE sẵn có
-- mà revoke/approve đã dùng. Nếu policy UPDATE của bạn giới hạn
-- theo cột cụ thể (column-level security) thay vì cho phép sửa cả
-- dòng, bạn cần tự mở rộng policy đó để cho phép cột này.
-- ============================================================

alter table public.parent_links
    add column if not exists permission_level text not null default 'full';

alter table public.parent_links
    drop constraint if exists parent_links_permission_level_check;

alter table public.parent_links
    add constraint parent_links_permission_level_check
    check (permission_level in ('full', 'view_only'));

-- Cho phép app_limits chặn ở tầng DB luôn (không chỉ tầng Go) -
-- nếu bạn muốn phòng thủ kép: 1 policy phụ trên app_limits chỉ
-- cho phép insert/update khi liên kết tương ứng đang 'full'.
-- Bỏ comment nếu bạn muốn bật (yêu cầu app_limits.child_user_id
-- và parent_links như trên đã tồn tại đúng tên cột).
--
-- drop policy if exists "parent with full permission can set limits" on public.app_limits;
-- create policy "parent with full permission can set limits"
--     on public.app_limits
--     for insert
--     with check (
--         exists (
--             select 1 from public.parent_links pl
--             where pl.parent_user_id = auth.uid()
--               and pl.child_user_id = app_limits.child_user_id
--               and pl.status = 'approved'
--               and pl.permission_level = 'full'
--         )
--     );
