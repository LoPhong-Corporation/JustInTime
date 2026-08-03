-- ============================================================
-- 005_device_heartbeats.sql
--
-- Mỗi máy chạy dashboard-go (đã đăng nhập) tự đẩy lên 1 dòng
-- "heartbeat" mỗi ~30 giây: CPU/RAM/Disk hiện tại + last_seen.
-- Đây là nền tảng cho:
--   - Overview cards Online/Offline/Warning/Critical
--   - Danh sách "Machines" (mọi thiết bị cùng tài khoản)
--   - "Kết nối giữa 2 máy": 2 máy cùng đăng nhập 1 tài khoản sẽ
--     tự thấy nhau trong danh sách Machines, không cần cấu hình
--     gì thêm (giống Devices/Family, mọi thứ đi qua Supabase,
--     không có kết nối trực tiếp máy-tới-máy).
--   - Phụ huynh (đã approved) xem được heartbeat của con, để
--     biết máy con đang online hay không.
--
-- Chạy trong Supabase Dashboard > SQL Editor.
-- ============================================================

create table if not exists public.device_heartbeats (
    device_id     text not null,
    user_id       uuid not null default auth.uid() references auth.users(id) on delete cascade,
    hostname      text,
    cpu_percent   numeric,
    ram_percent   numeric,
    disk_percent  numeric,
    last_seen     timestamptz not null default now(),

    primary key (user_id, device_id)
);

create index if not exists device_heartbeats_user_idx on public.device_heartbeats (user_id, last_seen desc);

alter table public.device_heartbeats enable row level security;

drop policy if exists "user can read own heartbeats"          on public.device_heartbeats;
drop policy if exists "user can upsert own heartbeats"         on public.device_heartbeats;
drop policy if exists "user can update own heartbeats"         on public.device_heartbeats;
drop policy if exists "approved parent can read child heartbeats" on public.device_heartbeats;

create policy "user can read own heartbeats"
    on public.device_heartbeats for select
    to authenticated
    using (auth.uid() = user_id);

create policy "user can upsert own heartbeats"
    on public.device_heartbeats for insert
    to authenticated
    with check (auth.uid() = user_id);

create policy "user can update own heartbeats"
    on public.device_heartbeats for update
    to authenticated
    using (auth.uid() = user_id)
    with check (auth.uid() = user_id);

-- Cùng mô hình đồng thuận như activity_logs (migration 004):
-- phụ huynh chỉ xem được nếu con đã approve.
create policy "approved parent can read child heartbeats"
    on public.device_heartbeats for select
    to authenticated
    using (
        exists (
            select 1 from public.parent_links pl
            where pl.parent_user_id = auth.uid()
              and pl.child_user_id  = device_heartbeats.user_id
              and pl.status = 'approved'
        )
    );

-- (Tuỳ chọn) dọn heartbeat quá cũ (máy đã offline lâu, tránh
-- danh sách Machines phình to vô hạn với các thiết bị không
-- còn dùng nữa). Chạy định kỳ nếu muốn:
-- delete from public.device_heartbeats where last_seen < now() - interval '30 days';
