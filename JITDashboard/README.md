# Thời Gian Máy

Giao diện Tauri/React đọc dữ liệu hoạt động cục bộ do JustInTime agent ghi tại
`%APPDATA%\JustInTime\justintime.db`. Lệnh Rust mở cơ sở dữ liệu SQLite ở chế
độ chỉ đọc; nếu agent chưa tạo dữ liệu, ứng dụng hiển thị trạng thái chờ thay
vì sinh dữ liệu mẫu. Dashboard làm mới dữ liệu định kỳ mỗi 30 giây.

Overview, danh sách ứng dụng và lịch sử hoạt động dùng dữ liệu SQLite trên máy.
Các trang Đồng bộ, thiết bị, gia đình, giới hạn và AI dùng chung HTTP API của
Dashboard Go tại `http://127.0.0.1:5000`; cần khởi chạy Go dashboard và đăng
nhập tài khoản trong Cài đặt của giao diện này. UI hỗ trợ đồng bộ theo 7/30
ngày, xuất CSV, AI Insights/chat theo yêu cầu, xem và gửi tin nhắn thiết bị,
xóa heartbeat thiết bị cũ, mời/chấp thuận/thu hồi liên kết gia đình, phân quyền
chỉ xem/toàn quyền và quản lý giới hạn ứng dụng theo quyền. Gemini API key tiếp
tục được cấu hình và bảo vệ bởi phần Settings của Go dashboard. Bản in lịch sử
cũng dùng giao diện hiện tại. CORS chỉ cho phép origin loopback của Tauri và
Vite trên các cổng phát triển đã định.

Trang local SQLite cần chạy trong ứng dụng Tauri. Trang Vite chạy trong browser
vẫn có thể xem cloud, AI, thiết bị và gia đình khi Go backend sẵn sàng.

Để trỏ tới backend Go dùng cổng khác, đặt `VITE_GO_API_BASE_URL` trước khi build
(ví dụ `http://127.0.0.1:5001`).

## Phát triển

```powershell
npm run build
cd src-tauri
cargo check
```
