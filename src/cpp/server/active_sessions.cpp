#include "lemon/active_sessions.h"

#include <algorithm>

namespace lemon {

namespace {

int64_t to_epoch_ms(const std::chrono::system_clock::time_point& tp) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}

} // namespace

ActiveSessionTracker::ActiveSessionTracker(std::chrono::milliseconds ttl, size_t max_sessions)
    : ttl_(ttl), max_sessions_(max_sessions) {}

std::string ActiveSessionTracker::record_request_presence(
    const std::string& client_session_id,
    const std::string& client_app,
    const std::string& client_name,
    const std::string& remote_addr,
    uint16_t remote_port,
    const std::string& user_agent,
    bool is_polling) {
    std::lock_guard<std::mutex> lock(mutex_);
    prune_locked();

    const std::string key = client_session_id.empty()
        ? remote_addr + ":" + std::to_string(remote_port)
        : client_session_id;

    auto& session = sessions_[key];
    if (session.key.empty()) {
        session.key = key;
        session.first_seen_at = std::chrono::system_clock::now();
    }
    session.client_session_id = client_session_id;
    session.client_app = client_app;
    session.client_name = client_name;
    session.remote_addr = remote_addr;
    session.remote_port = remote_port;
    session.user_agent = user_agent;
    ++session.request_count;
    if (is_polling) {
        ++session.polling_request_count;
    }
    session.last_active_at = std::chrono::system_clock::now();
    session.last_active_steady = std::chrono::steady_clock::now();

    if (sessions_.size() > max_sessions_) {
        // Evict the oldest idle session to keep the list bounded.
        std::string oldest_key;
        std::chrono::steady_clock::time_point oldest = std::chrono::steady_clock::time_point::max();
        for (const auto& [candidate_key, candidate] : sessions_) {
            if (candidate.active_requests.empty() && candidate.last_active_steady < oldest) {
                oldest = candidate.last_active_steady;
                oldest_key = candidate_key;
            }
        }
        if (!oldest_key.empty()) {
            sessions_.erase(oldest_key);
        }
    }

    return key;
}

void ActiveSessionTracker::set_authenticated(const std::string& key, bool authenticated) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(key);
    if (it != sessions_.end()) {
        it->second.authenticated = authenticated;
    }
}

ActiveSessionTracker::RequestHandle ActiveSessionTracker::begin_request(
    const std::string& key, const std::string& model,
    const std::string& kind, bool streaming) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (key.empty()) {
        return RequestHandle();
    }
    auto it = sessions_.find(key);
    if (it == sessions_.end()) {
        return RequestHandle();
    }

    const uint64_t request_id = next_request_id_++;
    ActiveRequest request;
    request.model = model;
    request.kind = kind;
    request.streaming = streaming;
    request.started_at = std::chrono::system_clock::now();
    it->second.active_requests.emplace(request_id, std::move(request));
    it->second.last_active_at = std::chrono::system_clock::now();
    it->second.last_active_steady = std::chrono::steady_clock::now();
    return RequestHandle(this, key, request_id);
}

void ActiveSessionTracker::end_request(const std::string& key, uint64_t request_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(key);
    if (it == sessions_.end()) {
        return;
    }
    it->second.active_requests.erase(request_id);
    it->second.last_active_at = std::chrono::system_clock::now();
    it->second.last_active_steady = std::chrono::steady_clock::now();
}

void ActiveSessionTracker::prune_locked() {
    const auto now_steady = std::chrono::steady_clock::now();
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        const bool idle = it->second.active_requests.empty() &&
                          (now_steady - it->second.last_active_steady) > ttl_;
        if (idle) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

nlohmann::json ActiveSessionTracker::snapshot_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json sessions = nlohmann::json::array();
    for (const auto& [key, session] : sessions_) {
        nlohmann::json session_json;
        session_json["key"] = key;
        session_json["client_session_id"] = session.client_session_id;
        session_json["client_app"] = session.client_app;
        session_json["client_name"] = session.client_name;
        session_json["remote_addr"] = session.remote_addr;
        session_json["remote_port"] = session.remote_port;
        session_json["user_agent"] = session.user_agent;
        session_json["authenticated"] = session.authenticated;
        session_json["is_polling"] = session.polling_request_count >= session.request_count;
        session_json["request_count"] = session.request_count;
        session_json["first_seen_ms"] = to_epoch_ms(session.first_seen_at);
        session_json["last_active_ms"] = to_epoch_ms(session.last_active_at);

        nlohmann::json active_requests = nlohmann::json::array();
        for (const auto& [request_id, request] : session.active_requests) {
            active_requests.push_back({
                {"model", request.model},
                {"kind", request.kind},
                {"streaming", request.streaming},
                {"started_ms", to_epoch_ms(request.started_at)},
            });
        }
        session_json["active_requests"] = std::move(active_requests);
        sessions.push_back(std::move(session_json));
    }
    return sessions;
}

size_t ActiveSessionTracker::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

ActiveSessionTracker::RequestHandle::RequestHandle(
    ActiveSessionTracker* tracker, std::string key, uint64_t request_id)
    : tracker_(tracker), key_(std::move(key)), request_id_(request_id) {}

ActiveSessionTracker::RequestHandle::~RequestHandle() {
    end();
}

ActiveSessionTracker::RequestHandle::RequestHandle(RequestHandle&& other) noexcept
    : tracker_(other.tracker_), key_(std::move(other.key_)), request_id_(other.request_id_) {
    other.tracker_ = nullptr;
}

ActiveSessionTracker::RequestHandle& ActiveSessionTracker::RequestHandle::operator=(
    RequestHandle&& other) noexcept {
    if (this != &other) {
        end();
        tracker_ = other.tracker_;
        key_ = std::move(other.key_);
        request_id_ = other.request_id_;
        other.tracker_ = nullptr;
    }
    return *this;
}

void ActiveSessionTracker::RequestHandle::end() {
    if (!tracker_) {
        return;
    }
    tracker_->end_request(key_, request_id_);
    tracker_ = nullptr;
}

} // namespace lemon
