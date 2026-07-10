#include "omnisms/call.h"

#include <utility>

namespace omnisms {

std::string parse_clip_number(const std::string& line)
{
    const size_t q1 = line.find('"');
    if (q1 == std::string::npos) return {};
    const size_t q2 = line.find('"', q1 + 1);
    if (q2 == std::string::npos) return {};
    return line.substr(q1 + 1, q2 - q1 - 1);
}

IncomingCallTracker::IncomingCallTracker(Handler handler) : handler_(std::move(handler)) {}

bool IncomingCallTracker::feed(const std::string& line, int64_t now_us)
{
    const bool is_ring = line == "RING";
    const bool is_clip = line.rfind("+CLIP:", 0) == 0;
    if (!is_ring && !is_clip) return false;

    if (!has_window_ || now_us - window_us_ > CALL_GAP_US) reset();
    has_window_ = true;
    window_us_ = now_us;

    if (is_clip) {
        std::string number = parse_clip_number(line);
        if (!number.empty()) number_ = std::move(number);
        if (!notified_) {
            notified_ = true;
            if (handler_) handler_(number_);
        }
    } else {
        saw_ring_ = true;
    }
    return true;
}

void IncomingCallTracker::expire(int64_t now_us)
{
    if (has_window_ && saw_ring_ && !notified_ && now_us - window_us_ > UNKNOWN_DELAY_US) {
        notified_ = true;
        if (handler_) handler_(std::string());
    }
}

void IncomingCallTracker::reset()
{
    window_us_ = 0;
    has_window_ = false;
    notified_ = false;
    saw_ring_ = false;
    number_.clear();
}

}  // namespace omnisms
