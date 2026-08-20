#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace lemon {

// Tracks which clients are using the server and, while an inference is in
// flight, which model each one is using. Presence is recorded per HTTP request
// (see Server::authenticate_request); in-flight inferences are attributed via
// the begin_request/end_request RAII handles owned by Router. WebSocket
// connections live in a separate snapshot from WebSocketServer and are merged
// by the /connections handler.
class ActiveSessionTracker {
public:
    struct ActiveRequest {
        std::string model;
        std::string kind;
        bool streaming = false;
        std::chrono::system_clock::time_point started_at;
    };

    struct ClientSession {
        std::string key;
        std::string client_session_id;
        std::string client_app;
        std::string client_name;
        std::string remote_addr;
        uint16_t remote_port = 0;
        std::string user_agent;
        bool authenticated = false;
        uint64_t request_count = 0;
        uint64_t polling_request_count = 0;
        std::chrono::system_clock::time_point first_seen_at;
        std::chrono::system_clock::time_point last_active_at;
        std::chrono::steady_clock::time_point last_active_steady;
        std::unordered_map<uint64_t, ActiveRequest> active_requests;
    };

    explicit ActiveSessionTracker(
        std::chrono::milliseconds ttl = std::chrono::minutes(10),
        size_t max_sessions = 200);

    ActiveSessionTracker(const ActiveSessionTracker&) = delete;
    ActiveSessionTracker& operator=(const ActiveSessionTracker&) = delete;

    // Record that a client made an HTTP request. Returns the session key that
    // identifies this client for subsequent begin_request/set_authenticated.
    std::string record_request_presence(
        const std::string& client_session_id,
        const std::string& client_app,
        const std::string& client_name,
        const std::string& remote_addr,
        uint16_t remote_port,
        const std::string& user_agent,
        bool is_polling);

    void set_authenticated(const std::string& key, bool authenticated);

    // RAII handle for one in-flight inference. Ends the tracked request on
    // destruction (including unwinding), so it can never leak.
    class RequestHandle {
    public:
        RequestHandle() = default;
        ~RequestHandle();
        RequestHandle(RequestHandle&& other) noexcept;
        RequestHandle& operator=(RequestHandle&& other) noexcept;
        RequestHandle(const RequestHandle&) = delete;
        RequestHandle& operator=(const RequestHandle&) = delete;
        // End the tracked request now (idempotent; also runs on destruction).
        void end();
        bool is_empty() const { return tracker_ == nullptr; }
    private:
        friend class ActiveSessionTracker;
        RequestHandle(ActiveSessionTracker* tracker, std::string key, uint64_t request_id);
        ActiveSessionTracker* tracker_ = nullptr;
        std::string key_;
        uint64_t request_id_ = 0;
    };

    RequestHandle begin_request(const std::string& key, const std::string& model,
                                const std::string& kind, bool streaming);

    nlohmann::json snapshot_json() const;
    size_t size() const;

private:
    void end_request(const std::string& key, uint64_t request_id);
    void prune_locked();

    std::chrono::milliseconds ttl_;
    size_t max_sessions_;
    uint64_t next_request_id_ = 1;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ClientSession> sessions_;
};

} // namespace lemon
