// rules 单测：依赖 POSIX regex.h，仅在 UNIX 主机(含 CI)编译运行
#include <cassert>
#include <cstdio>
#include <string>

#include "omnisms/rules.h"

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
    CHECK(translate_perl_classes("\\d+") == "[0-9]+");
    CHECK(translate_perl_classes("[\\d]") == "[0-9]");
    CHECK(translate_perl_classes("\\D") == "[^0-9]");
    CHECK(translate_perl_classes("a\\.b") == "a\\.b");  // 其他转义原样保留

    // kw 命中 → drop
    ForwardDecision d = eval_forward_rules("kw\t贷款\tdrop", "+861", "低息贷款广告");
    CHECK(d.matched && d.drop);

    // from 正则 + 通道/邮件动作
    d = eval_forward_rules("from\t^\\+8610086\t1,email", "+8610086", "hi");
    CHECK(d.matched && !d.drop && d.email && d.chMask == 0x1);

    // 禁用行(第 4 列 0)跳过
    d = eval_forward_rules("kw\thi\tdrop\t0", "+861", "hi");
    CHECK(!d.matched);

    // re 正则命中正文
    d = eval_forward_rules("re\t验证码\\s*\\d{4,6}\t2", "+861", "您的验证码 123456");
    CHECK(d.matched && d.chMask == 0x2);

    // 无命中
    d = eval_forward_rules("kw\tabc\tdrop", "+861", "xyz");
    CHECK(!d.matched);

    // 校验：坏正则给出行号
    std::string msg;
    CHECK(!validate_forward_rules("re\t[unclosed\t1", &msg));
    CHECK(msg.find("1") != std::string::npos);
    CHECK(validate_forward_rules("kw\t[anything\tdrop\nre\t\\d+\t1", &msg));

    if (g_failures == 0) {
        printf("all rules tests passed\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
