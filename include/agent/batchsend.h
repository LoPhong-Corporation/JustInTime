//
// batchsend.h
// Thuật toán gửi theo lô có khả năng cô lập bản ghi lỗi ("poison record").
// Tách thành header thuần C++ (không phụ thuộc WinAPI/DB/mạng) để kiểm thử
// đơn vị được - sync.cpp chỉ việc cung cấp hàm gửi thật.
//
// Vấn đề cần giải quyết: gửi 100 record trong 1 request nhanh hơn ~100 lần
// gửi từng cái, NHƯNG nếu server từ chối cả lô chỉ vì 1 record hỏng thì
// (không có cơ chế cô lập) record hỏng đó sẽ chặn vĩnh viễn toàn bộ hàng
// đợi. Giải pháp: khi 1 lô bị từ chối, chia đôi và thử lại từng nửa (đệ
// quy) - tốn O(log n) request cho mỗi record hỏng, và các record tốt vẫn
// đi qua bình thường.
//

#ifndef BATCHSEND_H
#define BATCHSEND_H

#include <vector>

namespace jit {

enum class SendOutcome
{
    Accepted,       // server nhận cả đoạn
    Rejected,       // server từ chối (4xx/5xx do dữ liệu) - nên chia nhỏ để cô lập
    TransportError  // mất mạng/timeout/quá tải tạm thời - dừng hẳn, không phạt record
};

struct BatchReport
{
    std::vector<int> accepted;  // chỉ số các record đã gửi thành công
    std::vector<int> rejected;  // chỉ số các record bị từ chối khi gửi RIÊNG LẺ
    bool aborted = false;       // dừng sớm (lỗi giao vận hoặc hết ngân sách request)
    int  requests = 0;          // số request đã tốn
};

/*
 * send(lo, hi) -> SendOutcome: gửi các record [lo, hi).
 * maxRequests giới hạn tổng số request cho 1 lượt, để khi server hỏng
 * hoàn toàn (mọi request đều bị từ chối) ta không tốn 2n-1 request vô ích.
 * Các record chưa được xử lý khi dừng sớm KHÔNG nằm trong accepted/rejected
 * (giữ nguyên trạng thái chưa sync để lần sau thử lại).
 */
template <class SendFn>
BatchReport sendWithBisect(int count, int maxRequests, SendFn&& send)
{
    BatchReport report;

    struct Range { int lo; int hi; };
    std::vector<Range> pending;

    if (count > 0)
        pending.push_back({0, count});

    while (!pending.empty())
    {
        if (report.requests >= maxRequests)
        {
            report.aborted = true;
            break;
        }

        const Range r = pending.back();
        pending.pop_back();

        const SendOutcome outcome = send(r.lo, r.hi);
        report.requests++;

        if (outcome == SendOutcome::Accepted)
        {
            for (int i = r.lo; i < r.hi; i++)
                report.accepted.push_back(i);
        }
        else if (outcome == SendOutcome::TransportError)
        {
            report.aborted = true;
            break;
        }
        else if (r.hi - r.lo == 1)
        {
            report.rejected.push_back(r.lo);
        }
        else
        {
            const int mid = r.lo + (r.hi - r.lo) / 2;
            pending.push_back({mid, r.hi}); // nửa phải xử lý sau
            pending.push_back({r.lo, mid}); // nửa trái xử lý trước (LIFO)
        }
    }

    return report;
}

} // namespace jit

#endif
