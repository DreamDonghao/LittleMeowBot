/// @file LogWebSocketManager.hpp
/// @brief 运行日志 WebSocket 管理器

#pragma once

#include <drogon/WebSocketConnection.h>
#include <json/value.h>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace LittleMeowBot {
    struct LogSubscription {
        bool all = true;
        bool systemOnly = false;
        std::optional<uint64_t> groupId;
        std::optional<std::string> level;
        std::string keyword;
    };

    class LogWebSocketManager {
    public:
        static LogWebSocketManager &instance();

        void addConnection(const drogon::WebSocketConnectionPtr &conn);

        void removeConnection(const drogon::WebSocketConnectionPtr &conn);

        void updateSubscription(const drogon::WebSocketConnectionPtr &conn, LogSubscription subscription);

        void pushLog(const Json::Value &log);

        void broadcastStatus(const Json::Value &status);

    private:
        LogWebSocketManager() = default;

        [[nodiscard]] static bool matches(const LogSubscription &subscription, const Json::Value &log);

        std::mutex m_mutex;
        std::unordered_set<drogon::WebSocketConnectionPtr> m_connections;
        std::unordered_map<drogon::WebSocketConnectionPtr, LogSubscription> m_subscriptions;
    };
}
