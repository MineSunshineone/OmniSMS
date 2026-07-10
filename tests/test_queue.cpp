#include <cstdio>

#include "omnisms/queue.h"

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
    SmsEvent first{"10086", "first", "", ""};
    SmsEvent second{"10010", "second", "", ""};
    ForwardRetryQueue queue(2);
    CHECK(queue.enqueue(0, first, 200, 7));
    CHECK(queue.enqueue(1, second, 100, 14, 42, true));
    CHECK(!queue.enqueue(2, first, 0));
    CHECK(queue.size() == 2 && queue.has_initial_attempt());
    CHECK(queue.next_due_us() == 100);

    ForwardJob job;
    CHECK(!queue.take_ready(99, job));
    CHECK(queue.take_ready(100, job));
    CHECK(job.target == 1 && job.event.text == "second" && job.attempts == 0);
    CHECK(job.messageId == 42);
    CHECK(job.notify);

    CHECK(queue.retry(job, 1000));
    CHECK(job.attempts == 1);
    CHECK(job.nextAttemptUs == 1000 +
          static_cast<int64_t>(backoff_seconds(1, 15)) * 1000 * 1000);

    CHECK(queue.take_ready(200, job));
    CHECK(job.target == 0 && job.event.text == "first");
    CHECK(!queue.has_initial_attempt());

    // 总失败次数达到 PUSH_RETRY_MAX 后不再入队。
    ForwardRetryQueue retries;
    CHECK(retries.enqueue(0, first, 0, 0));
    CHECK(retries.take_ready(0, job));
    for (uint8_t attempt = 1; attempt <= PUSH_RETRY_MAX; ++attempt) {
        const bool requeued = retries.retry(job, job.nextAttemptUs);
        if (attempt < PUSH_RETRY_MAX) {
            CHECK(requeued);
            CHECK(retries.take_ready(job.nextAttemptUs, job));
        } else {
            CHECK(!requeued);
        }
    }
    CHECK(retries.empty() && job.attempts == PUSH_RETRY_MAX);

    FixedDueQueue<SmsEvent, 2> fixed;
    CHECK(fixed.enqueue(first, 200));
    CHECK(fixed.enqueue(second, 100));
    CHECK(!fixed.enqueue(first, 0));
    CHECK(fixed.size() == 2 && fixed.free() == 0);
    SmsEvent fixed_out;
    // 保持 ESP 原数组语义：槽 0 未到期时继续扫描，取出已到期的槽 1。
    CHECK(fixed.take_ready(100, fixed_out) && fixed_out.text == "second");
    CHECK(!fixed.take_ready(199, fixed_out));
    CHECK(fixed.take_ready(200, fixed_out) && fixed_out.text == "first");
    CHECK(fixed.empty() && fixed.free() == 2);
    fixed.enqueue(first, 0);
    fixed.clear();
    CHECK(fixed.empty());

    if (g_failures == 0) {
        printf("all queue tests passed\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
