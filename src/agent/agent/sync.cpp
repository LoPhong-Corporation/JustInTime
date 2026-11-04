//
// sync.cpp
//
// Đồng bộ các activity chưa gửi lên cloud. Khác bản trước:
//   - Gửi theo LÔ (tối đa MAX_RECORDS record/request) thay vì từng record
//     một (mỗi record trước đây là 1 kết nối TLS mới + 2 lần gọi server).
//   - Mất mạng => dừng ngay ở request đầu tiên và KHÔNG tăng retry_count
//     của bất kỳ record nào. Trước đây offline 1 lúc thì mỗi record đều bị
//     phạt backoff lũy thừa (tới 30 phút), nên khi có mạng lại dữ liệu vẫn
//     phải chờ rất lâu mới lên được.
//   - Server từ chối 1 lô => chia đôi để cô lập đúng record hỏng (chỉ record
//     đó bị phạt backoff), các record tốt vẫn đi qua.
//   - Đánh dấu đã sync cả lô trong 1 transaction (1 lần fsync thay vì N).
//   - Backlog lớn (vd sau nhiều giờ offline) được xả nhiều lô liên tiếp
//     trong 1 lượt thay vì chỉ 100 record mỗi 30 giây.
//   - Mảng SyncRecord (~5KB/record) nằm trên heap, không còn 530KB trên
//     stack của worker thread.
//

#include "sync.h"
#include "batchsend.h"
#include "database.h"
#include "network.h"
#include "auth.h"
#include "error_codes.h"
#include "log.h"

#include <vector>

namespace {

constexpr int kMaxRoundsPerCycle = 20;  // <= 2000 record/lượt
constexpr int kMaxRequestsPerRound = 20; // đủ cho ~1 record hỏng/lô 100

} // namespace

extern "C" int sync_pending_records(void)
{
    if (!auth_is_logged_in())
    {
        JIT_LOG(
            L"[SYNC][%hs] Chua dang nhap, du lieu van luu local, "
            L"dang nhap qua tray de dong bo len cloud\n",
            ERR_SYNC_NOT_LOGGED_IN
        );
        return 0;
    }

    std::vector<SyncRecord> records(MAX_RECORDS);
    std::vector<int> ids;
    ids.reserve(MAX_RECORDS);

    for (int round = 0; round < kMaxRoundsPerCycle; round++)
    {
        const int count = db_get_unsynced_records(records.data(), MAX_RECORDS);

        if (count <= 0)
        {
            if (round == 0)
                JIT_LOG(L"[SYNC] No pending records\n");
            return 0;
        }

        JIT_LOG(L"[SYNC] Found %d record(s)\n", count);

        const jit::BatchReport report = jit::sendWithBisect(
            count, kMaxRequestsPerRound,
            [&](int lo, int hi) -> jit::SendOutcome
            {
                int transportError = 0;

                if (network_send_batch(records.data() + lo, hi - lo, &transportError))
                    return jit::SendOutcome::Accepted;

                return transportError ? jit::SendOutcome::TransportError
                                      : jit::SendOutcome::Rejected;
            }
        );

        if (!report.accepted.empty())
        {
            ids.clear();
            for (const int idx : report.accepted)
                ids.push_back(records[static_cast<size_t>(idx)].id);

            db_mark_synced_batch(ids.data(), static_cast<int>(ids.size()));
            JIT_LOG(L"[SYNC] Da dong bo %d record\n", static_cast<int>(ids.size()));
        }

        for (const int idx : report.rejected)
        {
            db_mark_sync_failed(records[static_cast<size_t>(idx)].id);
            JIT_LOG(L"[SYNC] Record %d bi server tu choi (se thu lai sau, backoff tang dan)\n",
                    records[static_cast<size_t>(idx)].id);
        }

        if (report.aborted)
            return -1;

        if (count < MAX_RECORDS)
            return 0; // đã xả hết hàng đợi
    }

    return 0;
}
