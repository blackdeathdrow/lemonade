#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include "lemon/active_sessions.h"

using lemon::ActiveSessionTracker;

static int g_failures = 0;

static void check_bool(const char* name, bool ok) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++g_failures;
}

static void check_json_field(const char* name, const nlohmann::json& obj, const std::string& field, const std::string& expected) {
    bool ok = obj.contains(field) && obj[field].get<std::string>() == expected;
    std::printf("[%s] %s (field: %s)\n", ok ? "PASS" : "FAIL", name, field.c_str());
    if (!ok) {
        if (!obj.contains(field)) {
            std::printf("      Field '%s' missing from: %s\n", field.c_str(), obj.dump().c_str());
        } else {
            std::printf("      Expected: %s\n", expected.c_str());
            std::printf("      Actual:   %s\n", obj[field].dump().c_str());
        }
        ++g_failures;
    }
}

static const nlohmann::json& find_session(const nlohmann::json& sessions, const std::string& key) {
    for (const auto& s : sessions) {
        if (s["key"].get<std::string>() == key) {
            return s;
        }
    }
    static const nlohmann::json kEmpty = nlohmann::json::object();
    return kEmpty;
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== RUNNING ACTIVE SESSIONS C++ TESTS ===\n");

    // --- Presence recording ---
    {
        ActiveSessionTracker tracker(std::chrono::minutes(10), 200);
        const std::string key = tracker.record_request_presence(
            "session-1", "lemonade-web", "Chrome", "127.0.0.1", 50001, "Mozilla/5.0", false);
        check_bool("presence: key equals client_session_id", key == "session-1");
        check_bool("presence: one session tracked", tracker.size() == 1);

        const auto sessions = tracker.snapshot_json();
        check_bool("snapshot: one entry", sessions.size() == 1);
        const auto& s = sessions[0];
        check_json_field("presence: client_app", s, "client_app", "lemonade-web");
        check_json_field("presence: client_name", s, "client_name", "Chrome");
        check_json_field("presence: remote_addr", s, "remote_addr", "127.0.0.1");
        check_bool("presence: remote_port", s["remote_port"].get<int>() == 50001);
        check_json_field("presence: user_agent", s, "user_agent", "Mozilla/5.0");
        check_bool("presence: request_count == 1", s["request_count"].get<int>() == 1);
        check_bool("presence: is_polling false", s["is_polling"].get<bool>() == false);
        check_bool("presence: authenticated false by default", s["authenticated"].get<bool>() == false);
    }

    // --- ip:port fallback key when no client_session_id ---
    {
        ActiveSessionTracker tracker;
        const std::string key = tracker.record_request_presence("", "", "", "10.0.0.7", 51000, "curl/8.0", false);
        check_bool("fallback: key is addr:port", key == "10.0.0.7:51000");
    }

    // --- set_authenticated ---
    {
        ActiveSessionTracker tracker;
        const std::string key = tracker.record_request_presence("a", "", "", "1.2.3.4", 1, "", false);
        tracker.set_authenticated(key, true);
        const auto sessions = tracker.snapshot_json();
        check_bool("auth: flag set to true", sessions[0]["authenticated"].get<bool>() == true);
    }

    // --- In-flight request attribution ---
    {
        ActiveSessionTracker tracker;
        const std::string key = tracker.record_request_presence("b", "", "", "1.2.3.5", 2, "", false);
        {
            auto handle = tracker.begin_request(key, "qwen-3b", "chat", false);
            const auto snapshot = tracker.snapshot_json();
            check_bool("in-flight: one active request", snapshot[0]["active_requests"].size() == 1);
            const auto& req = snapshot[0]["active_requests"][0];
            check_json_field("in-flight: model", req, "model", "qwen-3b");
            check_json_field("in-flight: kind", req, "kind", "chat");
            check_bool("in-flight: streaming false", req["streaming"].get<bool>() == false);
        }
        check_bool("in-flight: cleared after handle destructed", tracker.snapshot_json()[0]["active_requests"].size() == 0);
    }

    // --- Streaming flag ---
    {
        ActiveSessionTracker tracker;
        const std::string key = tracker.record_request_presence("c", "", "", "1.2.3.6", 3, "", false);
        auto handle = tracker.begin_request(key, "whisper-1", "transcription", true);
        const auto snapshot = tracker.snapshot_json();
        check_bool("streaming: streaming true", snapshot[0]["active_requests"][0]["streaming"].get<bool>() == true);
    }

    // --- Concurrent requests for the same client session ---
    {
        ActiveSessionTracker tracker;
        const std::string key = tracker.record_request_presence("d", "", "", "1.2.3.7", 4, "", false);
        auto h1 = tracker.begin_request(key, "model-a", "chat", false);
        auto h2 = tracker.begin_request(key, "model-b", "completion", false);
        check_bool("concurrent: two active requests", tracker.snapshot_json()[0]["active_requests"].size() == 2);
        h1.end();
        check_bool("concurrent: one remains after first ends", tracker.snapshot_json()[0]["active_requests"].size() == 1);
        const auto snapshot = tracker.snapshot_json();
        check_json_field("concurrent: correct request ended", snapshot[0]["active_requests"][0], "model", "model-b");
    }

    // --- begin_request with unknown/empty key is a no-op ---
    {
        ActiveSessionTracker tracker;
        auto h = tracker.begin_request("", "model", "chat", false);
        check_bool("noop: empty key returns empty handle", h.is_empty());
        check_bool("noop: no sessions created", tracker.size() == 0);

        ActiveSessionTracker tracker2;
        auto h2 = tracker2.begin_request("missing-key", "model", "chat", false);
        check_bool("noop: unknown key returns empty handle", h2.is_empty());
    }

    // --- TTL pruning ---
    {
        ActiveSessionTracker tracker(std::chrono::milliseconds(30), 200);
        tracker.record_request_presence("e", "", "", "1.2.3.8", 5, "", false);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        tracker.record_request_presence("f", "", "", "1.2.3.9", 6, "", false);
        check_bool("ttl: stale session pruned on next mutation", tracker.size() == 1);
        const auto snapshot = tracker.snapshot_json();
        check_json_field("ttl: live session kept", snapshot[0], "client_session_id", "f");
    }

    // --- Cap enforcement (bounded list) ---
    {
        ActiveSessionTracker tracker(std::chrono::minutes(10), 3);
        tracker.record_request_presence("1", "", "", "10.0.0.1", 1, "", false);
        tracker.record_request_presence("2", "", "", "10.0.0.2", 2, "", false);
        tracker.record_request_presence("3", "", "", "10.0.0.3", 3, "", false);
        tracker.record_request_presence("4", "", "", "10.0.0.4", 4, "", false);
        check_bool("cap: oldest idle session evicted", tracker.size() == 3);
        const auto sessions = tracker.snapshot_json();
        check_json_field("cap: newest kept", find_session(sessions, "4"), "client_session_id", "4");
    }

    std::printf("===========================================\n");
    if (g_failures > 0) {
        std::printf("Tests finished: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf("All active sessions tests PASSED.\n");
    return 0;
}
