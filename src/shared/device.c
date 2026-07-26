//
// Created by LoPhongCorporation on 6/24/2026.
// Fixed: chữ ký hàm phải khớp với device.h (char*, không phải wchar_t*)
//

#include "device.h"

#include <windows.h>
#include <string.h>

/*
 * Lấy tên máy làm device_id.
 * Nếu thất bại, dùng giá trị mặc định thay vì để
 * buffer chứa dữ liệu rác / rỗng.
 */
void get_device_id(
    char* buffer,
    int size)
{
    if (!buffer || size <= 0)
        return;

    DWORD len = (DWORD)size;

    if (!GetComputerNameA(
            buffer,
            &len
        ))
    {
        strncpy_s(
            buffer,
            size,
            "UNKNOWN-DEVICE",
            _TRUNCATE
        );
    }
}
