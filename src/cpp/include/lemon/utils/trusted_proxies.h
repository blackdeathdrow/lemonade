#pragma once

#include <string>
#include <vector>

namespace lemon::utils {

// True when `remote_addr` matches any entry in `trusted_proxies`. Entries may
// be an exact IP (v4 or v6) or an IPv4 CIDR range (e.g. "10.0.0.0/8"). IPv6 is
// matched exactly only. Empty list or empty addr -> false.
bool is_trusted_proxy(const std::string& remote_addr,
                      const std::vector<std::string>& trusted_proxies);

} // namespace lemon::utils
