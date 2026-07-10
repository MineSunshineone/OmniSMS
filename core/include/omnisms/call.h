// 来电 URC 跟踪：复用 sms_forwarding 的 RING/+CLIP 去重语义。
// 不读系统时间、不创建线程；端口负责传入单调时钟并周期调用 expire()。
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "omnisms/sms.h"

namespace omnisms {

std::string parse_clip_number(const std::string& line);

class IncomingCallTracker {
public:
    using Handler = std::function<void(const std::string& caller)>;

    static constexpr int64_t CALL_GAP_US = 30LL * 1000 * 1000;
    static constexpr int64_t UNKNOWN_DELAY_US = 3LL * 1000 * 1000;

    explicit IncomingCallTracker(Handler handler);
    bool feed(const std::string& line, int64_t now_us);
    void expire(int64_t now_us);
    void reset();

private:
    Handler handler_;
    int64_t window_us_ = 0;
    bool has_window_ = false;
    bool notified_ = false;
    bool saw_ring_ = false;
    std::string number_;
};

}  // namespace omnisms
