#include "lemon/utils/trusted_proxies.h"

#include <cstdio>
#include <string>
#include <vector>

struct TestResult {
    int passed = 0;
    int failed = 0;

    void ok(const std::string& name) {
        printf("[PASS] %s\n", name.c_str());
        ++passed;
    }

    void fail(const std::string& name) {
        printf("[FAIL] %s\n", name.c_str());
        ++failed;
    }

    void check(const std::string& name, bool cond) {
        if (cond) {
            ok(name);
        } else {
            fail(name);
        }
    }
};

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    TestResult t;
    printf("=== RUNNING TRUSTED PROXIES C++ TESTS ===\n");

    using lemon::utils::is_trusted_proxy;

    // Empty list / empty addr -> never trusted.
    t.check("empty list not trusted", !is_trusted_proxy("127.0.0.1", {}));
    t.check("empty addr not trusted", !is_trusted_proxy("", {"127.0.0.1"}));

    // Exact IPv4 match.
    t.check("exact ipv4 match", is_trusted_proxy("127.0.0.1", {"127.0.0.1"}));
    t.check("exact ipv4 no match", !is_trusted_proxy("127.0.0.2", {"127.0.0.1"}));
    t.check("exact ipv4 one of many", is_trusted_proxy("10.0.0.5", {"127.0.0.1", "10.0.0.5"}));

    // IPv4 CIDR /8.
    t.check("cidr /8 inside", is_trusted_proxy("10.1.2.3", {"10.0.0.0/8"}));
    t.check("cidr /8 outside", !is_trusted_proxy("11.1.2.3", {"10.0.0.0/8"}));

    // IPv4 CIDR /24.
    t.check("cidr /24 inside", is_trusted_proxy("192.168.1.42", {"192.168.1.0/24"}));
    t.check("cidr /24 boundary low", is_trusted_proxy("192.168.1.0", {"192.168.1.0/24"}));
    t.check("cidr /24 boundary high", is_trusted_proxy("192.168.1.255", {"192.168.1.0/24"}));
    t.check("cidr /24 outside next", !is_trusted_proxy("192.168.2.0", {"192.168.1.0/24"}));

    // /32 exact.
    t.check("cidr /32 exact", is_trusted_proxy("8.8.8.8", {"8.8.8.8/32"}));
    t.check("cidr /32 no match", !is_trusted_proxy("8.8.8.9", {"8.8.8.8/32"}));

    // /0 matches everything.
    t.check("cidr /0 matches all", is_trusted_proxy("203.0.113.7", {"0.0.0.0/0"}));

    // Loopback as CIDR.
    t.check("loopback /8", is_trusted_proxy("127.255.255.255", {"127.0.0.0/8"}));

    // IPv6 exact match only.
    t.check("ipv6 exact match", is_trusted_proxy("::1", {"::1"}));
    t.check("ipv6 no match", !is_trusted_proxy("::2", {"::1"}));
    t.check("ipv6 full exact", is_trusted_proxy("2001:db8::1", {"2001:db8::1"}));

    // Malformed entries are skipped, not fatal.
    t.check("malformed entry skipped",
            is_trusted_proxy("127.0.0.1", {"not-an-ip", "127.0.0.1"}));
    t.check("malformed cidr skipped",
            !is_trusted_proxy("127.0.0.1", {"10.0.0.0/abc"}));

    // Mixed v4/v6 list.
    t.check("mixed list v4 hit",
            is_trusted_proxy("10.0.0.1", {"::1", "10.0.0.0/8"}));
    t.check("mixed list v6 hit",
            is_trusted_proxy("::1", {"10.0.0.0/8", "::1"}));

    // Leading zeros / whitespace not trimmed for exact entries.
    t.check("extra whitespace entry skipped",
            !is_trusted_proxy("127.0.0.1", {" 127.0.0.1 "}));

    printf("===========================================\n");
    if (t.failed == 0) {
        printf("All trusted proxies tests PASSED.\n");
        return 0;
    }
    printf("%d tests FAILED.\n", t.failed);
    return 1;
}
