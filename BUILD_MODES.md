# [Development] Chế độ build: Stable (C) vs Experimental (C++)

> Mục này dành cho **development/thử nghiệm**. Người dùng thông
> thường không cần quan tâm - mặc định **Stable** đã là lựa chọn
> đúng, an toàn cho production.

JustInTime (tray agent, C++/Qt6) có thể build theo 2 chế độ khác
nhau, chọn lúc **cấu hình CMake** (không đổi được lúc chạy - đây là
2 bản `.exe` khác nhau của cùng 1 app):

| Chế độ | Trạng thái | Lõi dùng | Thư mục nguồn |
|---|---|---|---|
| **Stable** (mặc định) | Đã chạy ổn định lâu dài | C thuần (C11) | `src/shared/stable/*.c` |
| **Experimental** | Thử nghiệm, chưa đủ thời gian chạy thực tế | C++ (C++17, RAII/`std::string`/`std::vector`/`std::mutex`...) | `src/shared/experimental/*.cpp` |

Cả 2 bản cùng thoả mãn **nguyên vẹn 1 bộ header** `extern "C"` trong
`include/{shared,core,agent}/` - phần còn lại của app (giao diện Qt)
gọi vào **hệt nhau**, không biết (và không cần biết) mình đang chạy ở
chế độ nào. Khác biệt duy nhất nằm ở cách hiện thực bên trong **15
module** - toàn bộ phần "core" (dữ liệu/mạng/đồng bộ) của app:

**`src/shared/{stable,experimental}/`:**
- `restclient` - HTTP client gọi Supabase
- `jsonutil` - trích xuất giá trị từ JSON tối giản
- `device` - sinh/lưu device ID ẩn danh
- `machines` - đẩy/đọc heartbeat danh sách máy
- `backup` - sao lưu dữ liệu cục bộ ra JSON
- `applimits` - giới hạn/chặn app do phụ huynh đặt
- `parentlink` - mời/duyệt/thu hồi liên kết phụ huynh-con
- `settings` - đọc/ghi file cấu hình `.ini`
- `network` - gửi activity lên Supabase Edge Function
- `auth` - đăng nhập/đăng ký/refresh token, session mã hoá DPAPI
- `i18n` - bảng dịch Anh/Việt
- `database` - toàn bộ tầng SQLite (activity_logs, retry queue, export)

**`src/agent/{stable,experimental}/`:**
- `remoteview` - HTTP server cục bộ chỉ đọc (xem real-time)
- `sync` - vòng lặp đồng bộ record chưa gửi lên cloud

**`src/core/{stable,experimental}/`:**
- `activity` - engine theo dõi cửa sổ đang active, chặn app theo giới hạn

Đây là **toàn bộ** module "core" của app - phần giao diện Qt
(`ControlPanelWindow` và các trang con) không có bản C song song, chỉ
có 1 bản C++ duy nhất (bản thân giao diện vốn đã luôn là C++/Qt6 từ
đầu).

## Cách chọn

```bash
# Chế độ Stable (mặc định, không cần chỉ định)
cmake -B build .

# Chế độ Stable (chỉ định rõ ràng)
cmake -B build -DJUSTINTIME_CORE=STABLE .

# Chế độ Experimental
cmake -B build -DJUSTINTIME_CORE=EXPERIMENTAL .

cmake --build build
```

Build với `EXPERIMENTAL` sẽ in cảnh báo lúc cấu hình CMake, để không
ai vô tình đóng gói bản thử nghiệm thành bản phát hành.

## Xem trong ứng dụng

Trang **Development** riêng trong Control Panel (mục cuối sidebar,
tách biệt khỏi **About**) hiển thị:
- Badge màu xanh lá "STABLE (C)" hoặc màu cam "EXPERIMENTAL (C++)"
- Giải thích ngắn gọn ý nghĩa của chế độ đang chạy
- Lệnh CMake để đổi chế độ

Nếu đang chạy bản **Experimental**, tiêu đề cửa sổ Control Panel
cũng gắn thêm `[EXPERIMENTAL]` để nhận ra ngay mà không cần mở tới
trang Development (bản Stable không có gắn thêm gì, vì đó là mặc
định).

## Vì sao tách 2 chế độ thay vì chỉ thay thế C bằng C++?

Để có thể **so sánh trực tiếp** 2 cách hiện thực (hành vi phải giống
hệt nhau ở mọi tình huống, chỉ khác an toàn bộ nhớ/luồng và độ rõ
ràng của code), và để việc chuyển đổi dần dần không đánh mất bản gốc
đã chạy ổn định - nếu bản Experimental có bug mới phát sinh, luôn có
thể quay lại Stable ngay bằng 1 cờ CMake mà không cần revert code.
