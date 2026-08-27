/// @file LogWebSocketManager.cpp
/// @brief 运行日志 WebSocket 管理器 - 实现

#include <service/LogWebSocketManager.hpp>
#include <util/tool.h>
#include <json/writer.h>
#include <spdlog/spdlog.h>
#include <utility>

namespace LittleMeowBot {
    LogWebSocketManager &LogWebSocketManager::instance() {
        static LogWebSocketManager manager;
        return manager;
    }

    void LogWebSocketManager::addConnection(const drogon::WebSocketConnectionPtr &conn) {
        std::lock_guard lock(m_mutex);
        m_connections.insert(conn);
    }

    void LogWebSocketManager::removeConnection(const drogon::WebSocketConnectionPtr &conn) {
        std::lock_guard lock(m_mutex);
        m_connections.erase(conn);
        m_subscriptions.erase(conn);
    }

    void LogWebSocketManager::updateSubscription(const drogon::WebSocketConnectionPtr &conn,
                                                 LogSubscription subscription) {
        std::lock_guard lock(m_mutex);
        m_subscriptions[conn] = std::move(subscription);
    }

    void LogWebSocketManager::pushLog(const Json::Value &log) {
        std::lock_guard lock(m_mutex);
        for (const auto &conn: m_connections) {
            auto it = m_subscriptions.find(conn);
            if (it != m_subscriptions.end() && matches(it->second, log)) {
                Json::Value msg;
                msg["type"] = "log";
                msg["data"] = log;
                Json::StreamWriterBuilder builder;
                conn->send(Json::writeString(builder, msg));
            }
        }
    }

    void LogWebSocketManager::broadcastStatus(const Json::Value &status) {
        std::lock_guard lock(m_mutex);
        Json::Value msg;
        msg["type"] = "status";
        msg["data"] = status;
        const Json::StreamWriterBuilder builder;
        const auto json = Json::writeString(builder, msg);
        for (const auto &conn: m_connections) {
            conn->send(json);
        }
    }

    bool LogWebSocketManager::matches(const LogSubscription &subscription, const Json::Value &log) {
        // 日志条目的会话字段线上名为 "groupId"（字符串，可能带私聊标志位超出 int64）
        const bool hasSession = log.isMember("groupId") && !log["groupId"].isNull();
        if (subscription.systemOnly) {
            if (hasSession) {
                return false;
            }
        } else if (subscription.sessionId.has_value()) {
            if (!hasSession || parseUInt64(log["groupId"].asString()) != *subscription.sessionId) {
                return false;
            }
        }
        if (subscription.level.has_value() && log.get("level", "").asString() != *subscription.level) {
            return false;
        }
        if (!subscription.keyword.empty() && log.get("message", "").asString().find(subscription.keyword) ==
            std::string::npos) {
            return false;
        }
        return true;
    }
}
