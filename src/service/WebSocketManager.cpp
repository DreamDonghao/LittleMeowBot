/// @file WebSocketManager.cpp
/// @brief WebSocket 连接管理器 - 实现

#include <ranges>
#include <service/WebSocketManager.hpp>
#include <util/tool.h>
#include <spdlog/spdlog.h>

namespace LittleMeowBot {
    WebSocketManager &WebSocketManager::instance() {
        static WebSocketManager mgr;
        return mgr;
    }

    void WebSocketManager::addConnection(const drogon::WebSocketConnectionPtr &conn) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_connections.insert(conn);
        spdlog::info("WebSocket连接已建立，当前连接数: {}", m_connections.size());
    }

    void WebSocketManager::removeConnection(const drogon::WebSocketConnectionPtr &conn) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_connections.erase(conn);
        // 清理订阅关系
        for (auto &subscribers: m_subscriptions | std::views::values) {
            subscribers.erase(conn);
        }
        spdlog::info("WebSocket连接已断开，当前连接数: {}", m_connections.size());
    }

    void WebSocketManager::subscribeGroup(const drogon::WebSocketConnectionPtr &conn, uint64_t groupId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_subscriptions[groupId].insert(conn);
        spdlog::info("WebSocket订阅群: {}", groupId);
    }

    void WebSocketManager::unsubscribeGroup(const drogon::WebSocketConnectionPtr &conn, const uint64_t groupId) {
        std::lock_guard lock(m_mutex);
        if (m_subscriptions.contains(groupId)) {
            m_subscriptions[groupId].erase(conn);
        }
    }

    void WebSocketManager::pushMessage(const uint64_t groupId, const std::string &role, const std::string &content) {
        std::lock_guard lock(m_mutex);

        Json::Value msg;
        msg["type"] = "new_message";
        msg["groupId"] = groupId;
        msg["data"]["role"] = role;
        msg["data"]["content"] = content;
        msg["data"]["timestamp"] = currentDateTime();

        const Json::StreamWriterBuilder builder;
        const std::string jsonStr = Json::writeString(builder, msg);

        // 发送给订阅该群的连接
        if (m_subscriptions.contains(groupId)) {
            for (const auto &conn: m_subscriptions[groupId]) {
                conn->send(jsonStr);
            }
        }

        // 也发送给未订阅特定群的连接（订阅所有群）
        for (const auto &conn: m_connections) {
            // 如果连接没有订阅任何群，发送所有消息
            bool hasSubscription = false;
            for (const auto &subscribers: m_subscriptions | std::views::values) {
                if (subscribers.contains(conn)) {
                    hasSubscription = true;
                    break;
                }
            }
            if (!hasSubscription) {
                conn->send(jsonStr);
            }
        }
    }

    void WebSocketManager::broadcastEvent(const std::string &type, const Json::Value &data) {
        std::lock_guard lock(m_mutex);

        Json::Value msg;
        msg["type"] = type;
        msg["data"] = data;

        Json::StreamWriterBuilder builder;
        const std::string jsonStr = Json::writeString(builder, msg);

        for (const auto &conn: m_connections) {
            conn->send(jsonStr);
        }
    }
}