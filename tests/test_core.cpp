// core 单测：无外部框架依赖，assert 风格，任何平台可跑
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "omnisms/pdu.h"
#include "omnisms/phone.h"
#include "omnisms/text.h"

using namespace omnisms;

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        ++g_failures; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static void test_phone()
{
    CHECK(canonical_phone("+8613800138000") == "13800138000");
    CHECK(canonical_phone("8613800138000") == "13800138000");   // 13 位裸 86 前缀
    CHECK(canonical_phone("13800138000") == "13800138000");
    CHECK(canonical_phone("+44 7700 900123") == "447700900123"); // 非 86 不剥
    CHECK(canonical_phone("861234") == "861234");                // 短号不误剥
    CHECK(canonical_phone(" +86 138-0013-8000 ") == "13800138000");

    CHECK(number_blacklisted("10086\n10010", "+8610086"));
    CHECK(number_blacklisted("+8613800138000", "13800138000"));
    CHECK(!number_blacklisted("10086", "10010"));
    CHECK(!number_blacklisted("", "10086"));

    CHECK(is_valid_phone_number("+8613800138000"));
    CHECK(!is_valid_phone_number("12"));
    CHECK(!is_valid_phone_number("138-0013"));
}

static void test_text()
{
    CHECK(trim("  ab c \n") == "ab c");
    CHECK(json_escape("a\"b\\c\nd") == "a\\\"b\\\\c\\nd");
    CHECK(html_escape("<b>&\"") == "&lt;b&gt;&amp;&quot;");
    CHECK(url_encode("a b+c") == "a+b%2Bc");
    CHECK(url_decode_component("a+b%2Bc") == "a b+c");
    CHECK(utf8_truncate("你好世界", 7) == "你好...");  // 不切半个汉字(6 字节两字)

    // 占位符单次扫描：正文里的 {message} 字面量不被二次展开
    std::string out = apply_push_placeholders(
        "{sender}|{message}|{timestamp}|{receiver}|{local_number}|{unknown}",
        "S", "T{message}", "TS", "R");
    CHECK(out == "S|T{message}|TS|R|R|{unknown}");
}

static void test_pdu_decode()
{
    // 手工构造 SMS-DELIVER：SMSC 省略、发送方 +8613800138000、GSM7 "hello"
    // SCTS 2026-07-10 12:00:00 +08
    PduDecoder dec;
    DecodedSms sms;
    CHECK(dec.decode("00040D91683108108300F000006270012100002305E8329BFD06", sms));
    CHECK(sms.sender == "+8613800138000");
    CHECK(sms.text == "hello");
    CHECK(sms.concat[2] <= 1);  // 非长短信

    CHECK(!dec.decode("zz", sms));       // 非 hex
    CHECK(!dec.decode("00040", sms));    // 奇数长度
}

static void test_concat()
{
    std::vector<std::string> emitted;
    std::vector<ConcatAssembler::Notice> notices;
    ConcatAssembler asmres([&](const std::string& s, const std::string& t, const std::string&) {
        emitted.push_back(s + ":" + t);
    }, [&](const ConcatAssembler::Notice& notice) {
        notices.push_back(notice);
    });

    // 单条直接吐出
    DecodedSms single;
    single.sender = "+861";
    single.text = "one";
    asmres.feed(single, 0);
    CHECK(emitted.size() == 1 && emitted[0] == "+861:one");

    // 两段乱序合并
    DecodedSms p2;
    p2.sender = "+862"; p2.text = "B"; p2.concat[0] = 7; p2.concat[1] = 2; p2.concat[2] = 2;
    DecodedSms p1 = p2;
    p1.text = "A"; p1.concat[1] = 1;
    asmres.feed(p2, 1000);
    CHECK(emitted.size() == 1);          // 未凑齐不吐
    CHECK(notices.size() == 1 && notices[0].kind == ConcatAssembler::NoticeKind::PartReceived);
    CHECK(notices[0].reference == 7 && notices[0].part == 2 && notices[0].received == 1 && notices[0].total == 2);
    asmres.feed(p1, 2000);
    CHECK(emitted.size() == 2 && emitted[1] == "+862:AB");
    CHECK(notices.size() == 2 && notices[1].kind == ConcatAssembler::NoticeKind::PartReceived);

    // 超时强制合并：缺口有标记
    DecodedSms q1;
    q1.sender = "+863"; q1.text = "X"; q1.concat[0] = 9; q1.concat[1] = 1; q1.concat[2] = 2;
    asmres.feed(q1, 5000);
    asmres.expire(5000 + ConcatAssembler::TIMEOUT_US + 1);
    CHECK(emitted.size() == 3);
    CHECK(emitted[2].find("X[缺失分段2]") != std::string::npos);
    CHECK(notices.size() == 4 && notices[3].kind == ConcatAssembler::NoticeKind::TimedOut);
    CHECK(notices[3].received == 1 && notices[3].total == 2);

    DecodedSms invalid;
    invalid.sender = "+864"; invalid.text = "bad";
    invalid.concat[0] = 10; invalid.concat[1] = 11; invalid.concat[2] = 11;
    asmres.feed(invalid, 9000);
    CHECK(emitted.size() == 4 && emitted[3] == "+864:bad");
    CHECK(notices.size() == 5 && notices[4].kind == ConcatAssembler::NoticeKind::InvalidPart);
}

static void test_pdu_encode()
{
    PduEncoder encoder;
    std::vector<EncodedPdu> parts;
    std::string error;

    CHECK(encoder.encode_text("+8613800138000", "hello", 0, parts, &error));
    CHECK(parts.size() == 1 && parts[0].tpduLength > 0);
    CHECK(!parts[0].payload.empty() && parts[0].payload.back() == '\x1A');
    CHECK(parts[0].part == 1 && parts[0].total == 1);

    std::string long_cn;
    for (int i = 0; i < 80; ++i) long_cn += "中";
    CHECK(encoder.encode_text("10086", long_cn, 0x1234, parts, &error));
    CHECK(parts.size() == 2);
    CHECK(parts[0].part == 1 && parts[0].total == 2);
    CHECK(parts[1].part == 2 && parts[1].total == 2);

    CHECK(!encoder.encode_text("bad-number", "hello", 0, parts, &error));
    CHECK(!encoder.encode_text("10086", std::string(301, 'a'), 0, parts, &error));

    // MCU 收发共用工作缓冲的 codec 也必须与独立包装行为一致。
    PduCodec codec;
    DecodedSms decoded;
    CHECK(codec.decode("00040D91683108108300F000006270012100002305E8329BFD06", decoded));
    CHECK(decoded.sender == "+8613800138000" && decoded.text == "hello");
    CHECK(codec.encode_text("10086", "codec", 0x4321, parts, &error));
    CHECK(parts.size() == 1 && parts[0].tpduLength > 0);
}

int main()
{
    test_phone();
    test_text();
    test_pdu_decode();
    test_concat();
    test_pdu_encode();
    if (g_failures == 0) {
        printf("all core tests passed\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
