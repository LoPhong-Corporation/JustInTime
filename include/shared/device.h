//
// Created by LoPhongCorporation on 6/24/2026.
// Rewritten: device_id không còn là tên máy Windows (GetComputerName)
// nữa để tránh rò rỉ thông tin cá nhân - xem device.c để biết lý do
// và chi tiết cách sinh/lưu id mới.
//

#ifndef DEVICE_H
#define DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif


/*
 * ID định danh máy, dạng "PC-XXXXXXXX" (8 ký tự hex ngẫu nhiên),
 * KHÔNG liên quan gì tới tên máy tính Windows thật của người dùng.
 * Sinh 1 lần duy nhất (lần chạy đầu tiên) rồi lưu lại ở
 * "%APPDATA%\JustInTime\device.id" - từ lần sau chỉ đọc lại, không
 * đổi, để device_id vẫn khớp giữa các lần chạy/đồng bộ (khớp cả với
 * dashboard-go, đọc cùng file này qua config.deviceID()).
 *
 * Giữ nguyên tên hàm get_device_id() để không phải sửa lại chỗ gọi
 * hiện có (database.c) - chỉ có HÀNH VI bên trong là thay đổi.
 */
void get_device_id(
    char* buffer,
    int size
);

/*
 * Tên hiển thị thân thiện của máy này, do NGƯỜI DÙNG tự đặt (ví dụ
 * "May lam viec", "Laptop hoc tap") - mặc định là chuỗi rỗng, nghĩa
 * là dashboard sẽ tự hiển thị 1 nhãn chung chung kiểu "Computer" +
 * vài ký tự cuối của device_id, KHÔNG BAO GIỜ tự động lấy tên máy
 * Windows thật làm mặc định.
 */
void device_get_label(
    char* buffer,
    int size
);

/*
 * Đặt tên hiển thị thân thiện cho máy này (xem device_get_label).
 * Trả về 1 nếu lưu thành công.
 */
int device_set_label(
    const char* label
);


#ifdef __cplusplus
}
#endif

#endif
