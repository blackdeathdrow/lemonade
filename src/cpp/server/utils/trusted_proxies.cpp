#include "lemon/utils/trusted_proxies.h"

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>

namespace lemon::utils {

namespace {

bool parse_ipv4(const std::string& s, uint32_t& out) {
    uint32_t parts[4] = {0, 0, 0, 0};
    int idx = 0;
    std::string token;
    std::istringstream iss(s);
    while (std::getline(iss, token, '.')) {
        if (idx >= 4 || token.empty() || token.size() > 3) {
            return false;
        }
        for (char c : token) {
            if (c < '0' || c > '9') {
                return false;
            }
        }
        int v = std::stoi(token);
        if (v < 0 || v > 255) {
            return false;
        }
        parts[idx++] = static_cast<uint32_t>(v);
    }
    if (idx != 4) {
        return false;
    }
    out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return true;
}

bool match_ipv4_cidr(const std::string& remote, const std::string& entry) {
    uint32_t remote_ip = 0;
    if (!parse_ipv4(remote, remote_ip)) {
        return false;
    }
    const size_t slash = entry.find('/');
    if (slash == std::string::npos) {
        uint32_t entry_ip = 0;
        return parse_ipv4(entry, entry_ip) && entry_ip == remote_ip;
    }
    const std::string base_str = entry.substr(0, slash);
    const std::string bits_str = entry.substr(slash + 1);
    uint32_t base_ip = 0;
    if (!parse_ipv4(base_str, base_ip) || bits_str.empty() || bits_str.size() > 2) {
        return false;
    }
    for (char c : bits_str) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    int bits = std::stoi(bits_str);
    if (bits < 0 || bits > 32) {
        return false;
    }
    if (bits == 0) {
        return true;
    }
    const uint32_t mask = bits >= 32 ? 0xFFFFFFFFu : (~0u << (32 - bits));
    return (base_ip & mask) == (remote_ip & mask);
}

} // namespace

bool is_trusted_proxy(const std::string& remote_addr,
                      const std::vector<std::string>& trusted_proxies) {
    if (remote_addr.empty() || trusted_proxies.empty()) {
        return false;
    }
    for (const auto& entry : trusted_proxies) {
        if (entry.empty()) {
            continue;
        }
        // Any IPv4 entry (exact or CIDR) goes through the numeric matcher.
        if (entry.find(':') == std::string::npos) {
            if (match_ipv4_cidr(remote_addr, entry)) {
                return true;
            }
        } else if (entry == remote_addr) {
            // IPv6: exact match only.
            return true;
        }
    }
    return false;
}

} // namespace lemon::utils
