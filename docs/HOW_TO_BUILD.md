# Hướng dẫn build JustInTime

Project build ra **2 file `.exe` độc lập** (xem `README.md` mục
"Project Structure" để biết vì sao tách): `JustInTime.exe` (agent) và
`JustInTimeDashboard.exe` (dashboard). Cả 2 cùng dùng `libs/common/`.

## Cấu trúc mã nguồn

Toàn bộ mã C++ native được gom vào hai thư mục cấp gốc: header trong
`includes/` và phần triển khai trong `src/`. Các thư mục con giữ ranh
giới giữa agent, dashboard và thư viện dùng chung:

```text
JustInTime/
├── includes/
│   ├── agent/
│   │   ├── agent/
│   │   ├── core/
│   │   ├── parent/
│   │   └── shared/
│   ├── common/
│   └── dashboard/
│       └── ui/
├── src/
│   ├── agent/
│   │   ├── agent/
│   │   ├── core/
│   │   ├── parent/
│   │   └── shared/
│   ├── common/
│   └── dashboard/
│       └── ui/
├── apps/
│   ├── agent/
│   │   └── CMakeLists.txt
│   ├── dashboard/
│   │   ├── CMakeLists.txt
│   │   ├── tests/
│   │   └── third_party/
│   └── dashboard-web/
│       ├── backend/       Go API + web UI nhúng
│       │   ├── cmd/
│       │   └── internal/
│       └── desktop/       React + Tauri
│           ├── src/
│           └── src-tauri/
├── libs/
│   └── common/            CMake definition của thư viện native dùng chung
├── migrations/            Supabase schema và migrations
├── resources/             Icon và tài nguyên Windows/Qt
├── docs/                  Hướng dẫn build và tài liệu dự án
├── installer/             Script build và cấu hình Inno Setup
├── CMakeLists.txt
└── out/                   Thư mục build cục bộ, không phải mã nguồn
```

Header và source của C++ nằm tập trung ở cấp gốc, phân tách theo component
trong `includes/agent`, `includes/dashboard`, `includes/common` và các
nhánh tương ứng trong `src/`. CMake của từng app vẫn ở `apps/`; dependencies,
tests và tài nguyên riêng vẫn nằm cùng app đó. Thư viện dùng chung được
khai báo tại `libs/common/`, còn mã nguồn thực tế nằm trong `includes/common/`
và `src/common/`.

---

## 1. Yêu cầu

- **Windows 10/11** (project chỉ build cho Windows - dùng WinAPI trực
  tiếp ở nhiều chỗ: WinHTTP, DPAPI, PDH, Toolhelp32...).
- **Visual Studio 2022** với workload **"Desktop development with C++"**
  (cần cho MSVC + Windows SDK). Bản Community (miễn phí) dùng được.
- **CMake 3.28 trở lên** - Visual Studio 2022 đã có sẵn CMake tích hợp,
  không cần cài riêng nếu bạn build qua VS (xem mục 4A). Nếu build bằng
  dòng lệnh thì cài CMake riêng: https://cmake.org/download/
- **Qt6** (khuyến nghị 6.5 trở lên), bản **MSVC 64-bit** - xem mục 2.

## 2. Cài Qt6

1. Tải Qt Online Installer: https://www.qt.io/download-qt-installer
   (cần tài khoản Qt, miễn phí).
2. Lúc chọn components, chỉ cần tick đúng **1 kit MSVC khớp phiên bản
   Visual Studio bạn cài** - ví dụ với VS2022 thì chọn
   **"MSVC 2022 64-bit"** dưới phiên bản Qt bạn muốn (vd Qt 6.7.x).
   **Không cần** kit MinGW, không cần Qt Creator (trừ khi muốn dùng nó
   làm IDE - xem mục 4B).
3. Ghi nhớ đường dẫn cài - mặc định dạng:
   `C:\Qt\<phiên bản>\msvc2022_64`
   (vd `C:\Qt\6.7.2\msvc2022_64`). Đường dẫn này cần dùng ở mục 4.

## 3. Lấy `sqlite3.c`

**Repo hiện CHƯA kèm sẵn** `third_party/sqlite/sqlite3.c` (chỉ có
`sqlite3.h`) - đây là file "amalgamation" (toàn bộ SQLite gộp thành 1
file `.c`), cần tự tải vì dung lượng lớn:

1. Vào https://sqlite.org/download.html
2. Tải mục **"sqlite-amalgamation-XXXXXXX.zip"** (phần "Source Code").
3. Giải nén, lấy file `sqlite3.c` bên trong, copy đè vào:
   `third_party/sqlite/sqlite3.c`
   (cạnh `sqlite3.h` đã có sẵn).

Thiếu file này, CMake configure vẫn chạy được nhưng build sẽ báo lỗi
không tìm thấy `third_party/sqlite/sqlite3.c` ngay khi build target
`JustInTimeCommon`.

## 4. Build

### Cách A - Dòng lệnh (nhanh nhất, dùng được trong CI)

Mở **"x64 Native Tools Command Prompt for VS 2022"** (tìm trong Start
Menu - PHẢI dùng cửa sổ này, không phải CMD/PowerShell thường, để có sẵn
biến môi trường MSVC), rồi:

```bat
cd đường-dẫn-tới-project
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.7.2/msvc2022_64"
cmake --build build --config Release
```

(thay `6.7.2` bằng đúng phiên bản Qt bạn cài ở mục 2).

Build xong, 2 file nằm ở:
```
build/bin/Release/JustInTime.exe
build/bin/Release/JustInTimeDashboard.exe
```

Chỉ muốn build 1 trong 2:
```bat
cmake --build build --config Release --target JustInTime
cmake --build build --config Release --target JustInTimeDashboard
```

### Cách B - Visual Studio 2022 (có debugger, dễ sửa lỗi hơn)

1. Mở Visual Studio 2022 → **File → Open → Folder...** → chọn thư mục
   gốc project (chứa `CMakeLists.txt`).
2. VS tự nhận ra đây là project CMake và chạy configure lần đầu - lần
   này **sẽ lỗi** vì chưa biết Qt6 ở đâu. Vào
   **Project → CMake Settings for JustInTime** (hoặc sửa trực tiếp file
   `CMakeSettings.json` VS tự tạo), thêm vào mục "CMake command
   arguments":
   ```
   -DCMAKE_PREFIX_PATH="C:/Qt/6.7.2/msvc2022_64"
   ```
3. Lưu lại, VS tự configure lại. Chọn target (`JustInTime.exe` hoặc
   `JustInTimeDashboard.exe`) ở thanh công cụ trên cùng, bấm **Build**
   (Ctrl+Shift+B) hoặc **F5** để build + chạy kèm debugger.

## 5. Chạy thử

File `.exe` cần các **DLL của Qt6** nằm cùng thư mục (hoặc trong PATH)
mới chạy được - nếu double-click báo thiếu `Qt6Widgets.dll` hay tương tự:

```bat
cd build\bin\Release
"C:\Qt\6.7.2\msvc2022_64\bin\windeployqt.exe" JustInTime.exe
"C:\Qt\6.7.2\msvc2022_64\bin\windeployqt.exe" JustInTimeDashboard.exe
```

`windeployqt` tự copy đúng các DLL Qt cần thiết vào cạnh file `.exe`.
Chạy từ Visual Studio (F5) thì không cần bước này - VS tự thêm thư mục
`bin` của Qt vào PATH lúc debug.

`JustInTime.exe` mở tray icon (góc dưới phải màn hình) - bấm phải vào
icon, chọn **"Open Dashboard"** để tự mở `JustInTimeDashboard.exe` (phải
nằm CÙNG thư mục với `JustInTime.exe` - 2 target đã được cấu hình build
ra chung 1 thư mục, xem `CMakeLists.txt` gốc).

## 6. Lỗi thường gặp

| Lỗi | Nguyên nhân / cách sửa |
|---|---|
| `Could not find a package configuration file provided by "Qt6"` | Thiếu hoặc sai `-DCMAKE_PREFIX_PATH` - kiểm tra lại đường dẫn mục 2, phải trỏ tới thư mục `msvc20XX_64` (không phải thư mục Qt gốc). |
| Lỗi build `JustInTimeCommon`, không tìm thấy `sqlite3.c` | Chưa làm mục 3. |
| `windows.h: No such file or directory` | Đang build bằng compiler không phải MSVC (vd MinGW/g++ thường) - project dùng nhiều API Windows-only, **bắt buộc build bằng MSVC** (Cách A/B ở trên), không build được bằng g++/clang thường. |
| Thiếu DLL lúc chạy (`Qt6Widgets.dll not found`...) | Chạy `windeployqt` - xem mục 5. |
| Bấm "Open Dashboard" ở tray báo không tìm thấy | `JustInTimeDashboard.exe` chưa build, hoặc bị di chuyển sang thư mục khác `JustInTime.exe` - 2 file phải nằm CÙNG thư mục. |

## 7. Build lại sau khi sửa code

Không cần configure lại từ đầu mỗi lần - chỉ cần:
```bat
cmake --build build --config Release
```
(CMake tự phát hiện file nào đổi, chỉ build lại phần cần thiết). Chỉ cần
configure lại (`cmake -B build ...`) khi bạn **thêm/xoá file** trong
danh sách `add_executable(...)`/`add_library(...)` của 1 trong 3
`CMakeLists.txt` (gốc, `libs/common`, `apps/agent`, `apps/dashboard`).
