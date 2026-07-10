#include <cstdio>
#include <string>
#include <vector>

#include "omnisms/call.h"

using namespace omnisms;

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        ++g_failures; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

int main()
{
    CHECK(parse_clip_number("+CLIP: \"+8613800138000\",145") == "+8613800138000");
    CHECK(parse_clip_number("+CLIP: missing") == "");

    std::vector<std::string> calls;
    IncomingCallTracker tracker([&](const std::string& caller) { calls.push_back(caller); });

    CHECK(!tracker.feed("+CMTI: \"SM\",1", 100));
    CHECK(tracker.feed("RING", 100));
    CHECK(calls.empty());
    CHECK(tracker.feed("+CLIP: \"10086\",129", 1000));
    CHECK(calls.size() == 1 && calls[0] == "10086");

    tracker.feed("RING", 2000);
    tracker.feed("+CLIP: \"10086\",129", 2500);
    CHECK(calls.size() == 1);

    tracker.feed("RING", IncomingCallTracker::CALL_GAP_US + 3000);
    tracker.expire(IncomingCallTracker::CALL_GAP_US + 3000 +
                   IncomingCallTracker::UNKNOWN_DELAY_US + 1);
    CHECK(calls.size() == 2 && calls[1].empty());

    tracker.feed("+CLIP: \"+44123\",145", 2 * IncomingCallTracker::CALL_GAP_US + 5000);
    CHECK(calls.size() == 3 && calls[2] == "+44123");

    if (g_failures == 0) {
        printf("all call tests passed\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
