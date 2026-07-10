#include <cstdio>
#include <string>

#include "omnisms/inbox.h"

using namespace omnisms;

static int failures = 0;
#define CHECK(value) do { if (!(value)) { \
    ++failures; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #value); \
} } while (0)

int main()
{
    MessageStore store;
    MessageAddResult first = store.add_received("10086", "hello", "2026-07-10", 100);
    CHECK(first.id == 1 && first.evictedId == 0);
    CHECK(store.received_count() == 1);
    CHECK(store.set_forwarded(first.id, true));
    InboxEntry inbox;
    CHECK(store.get_received_by_id(first.id, inbox) && inbox.forwarded);
    CHECK(store.set_forwarded(first.id, false));

    const std::string long_text(321, 'a');
    MessageAddResult second = store.add_received("10010", long_text, "ts", 101);
    CHECK(store.get_received_by_id(second.id, inbox));
    CHECK(inbox.text.size() == 323 && inbox.text.substr(320) == "...");

    for (size_t i = 2; i < INBOX_CAPACITY; ++i) {
        store.add_received(std::to_string(i), "body", "ts", static_cast<uint32_t>(100 + i));
    }
    CHECK(store.received_count() == INBOX_CAPACITY);
    MessageAddResult wrapped = store.add_received("new", "body", "ts", 999);
    CHECK(wrapped.id == INBOX_CAPACITY + 1 && wrapped.evictedId == 1);
    CHECK(!store.get_received_by_id(1, inbox));
    CHECK(store.get_received_newest(0, inbox) && inbox.id == wrapped.id);

    CHECK(store.delete_received(10));
    CHECK(store.received_count() == INBOX_CAPACITY - 1);
    CHECK(!store.delete_received(10));
    CHECK(store.json(false, 1).find("\"id\":51") != std::string::npos);

    for (size_t i = 0; i < SENT_CAPACITY; ++i) {
        store.add_sent("10086", "sent" + std::to_string(i), i % 2 == 0, 200 + i);
    }
    CHECK(store.sent_count() == SENT_CAPACITY);
    MessageAddResult sent_wrapped = store.add_sent("10010", "latest", true, 999);
    CHECK(sent_wrapped.id == SENT_CAPACITY + 1 && sent_wrapped.evictedId == 1);
    SentEntry sent;
    CHECK(store.get_sent_newest(0, sent) && sent.text == "latest");
    CHECK(store.json(true, 1).find("\"sent\":999") != std::string::npos);

    MessageStore restored;
    InboxEntry restored_entry{42, 123, "sender", "ts", "text", true};
    CHECK(restored.restore_received(restored_entry).id == 42);
    CHECK(restored.add_received("next", "body", "ts", 124).id == 43);
    SentEntry restored_sent{7, 200, "target", "body", true};
    CHECK(restored.restore_sent(restored_sent).id == 7);
    CHECK(restored.add_sent("target", "next", true, 201).id == 8);

    MessageStore exact;
    for (size_t i = 0; i < INBOX_CAPACITY + 4; ++i) {
        exact.add_received("发件人\t" + std::to_string(i), "正文\n" + std::to_string(i),
                           "2026-07-10", static_cast<uint32_t>(1000 + i));
    }
    CHECK(exact.delete_received(8));
    CHECK(exact.set_forwarded(12, true));
    for (size_t i = 0; i < SENT_CAPACITY + 3; ++i) {
        exact.add_sent("目标" + std::to_string(i), "发送\n" + std::to_string(i),
                       i % 2 == 0, static_cast<uint32_t>(2000 + i));
    }
    const std::string snapshot = exact.snapshot();
    MessageStore exact_restored;
    std::string restore_error;
    CHECK(exact_restored.restore_snapshot(snapshot, &restore_error));
    CHECK(restore_error.empty());
    CHECK(exact_restored.snapshot() == snapshot);
    CHECK(exact_restored.json(false) == exact.json(false));
    CHECK(exact_restored.json(true) == exact.json(true));
    CHECK(exact_restored.add_received("next", "body", "ts", 3000).id == INBOX_CAPACITY + 5);
    CHECK(exact_restored.add_sent("next", "body", true, 3000).id == SENT_CAPACITY + 4);

    const std::string before_bad_restore = exact_restored.snapshot();
    std::string corrupt = snapshot;
    const size_t marker = corrupt.find("R\t0\t");
    CHECK(marker != std::string::npos);
    if (marker != std::string::npos) corrupt.replace(marker, 4, "R\t9\t");
    CHECK(!exact_restored.restore_snapshot(corrupt, &restore_error));
    CHECK(!restore_error.empty());
    CHECK(exact_restored.snapshot() == before_bad_restore);
    CHECK(!exact_restored.restore_snapshot("OMNISMS_MESSAGE_STORE_V2\n", &restore_error));

    if (failures == 0) {
        printf("all inbox tests passed\n");
        return 0;
    }
    return 1;
}
