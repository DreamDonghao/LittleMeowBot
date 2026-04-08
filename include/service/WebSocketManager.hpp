/// @file WebSocketManager.hpp
/// @brief WebSocket 连接管理器
/// @author donghao
/// @date 2026-04-02
/// @details 管理 Web 管理后台的 WebSocket 连接：
///          - 连接管理：添加、移除连接
///          - 群订阅：支持按群订阅消息推送
///          - 消息推送：实时推送新消息和群列表更新

#pragma once
#include <drogon/WebSocketConnection.h>
#include <json/json.h>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <memory>
#include <vector>
#include <string>

namespace LittleMeowBot {
    /// @brief WebSocket连接管理器（单例模式）
    /// @details 管理所有WebSocket连接，支持按群订阅消息
    class WebSocketManager{
    public:
        /// @brief 获取单例实例
        /// @return WebSocketManager 实例引用
        static WebSocketManager& instance();

        /// @brief 添加连接
        /// @param conn WebSocket 连接指针
        void addConnection(const drogon::WebSocketConnectionPtr& conn);

        /// @brief 移除连接
        /// @param conn WebSocket 连接指针
        void removeConnection(const drogon::WebSocketConnectionPtr& conn);

        /// @brief 订阅特定群的消息
        /// @param conn WebSocket 连接指针
        /// @param groupId 群号
        void subscribeGroup(const drogon::WebSocketConnectionPtr& conn, uint64_t groupId);

        /// @brief 取消订阅群
        /// @param conn WebSocket 连接指针
        /// @param groupId 群号
        void unsubscribeGroup(const drogon::WebSocketConnectionPtr& conn, uint64_t groupId);

        /// @brief 推送新消息到订阅该群的连接
        /// @param groupId 群号
        /// @param role 角色（user/assistant）
        /// @param content 消息内容
        void pushMessage(uint64_t groupId, const std::string& role, const std::string& content);

        /// @brief 推送群列表更新
        /// @param groups 群号列表
        void pushGroupList(const std::vector<uint64_t>& groups);

        /// @brief 获取当前连接数
        /// @return 连接数量
        size_t getConnectionCount() const;

    private:
        WebSocketManager() = default;

        /// @brief 获取当前时间戳字符串
        /// @return 格式化的时间戳（YYYY-MM-DD HH:MM:SS）
        std::string getCurrentTimestamp() const;

        std::unordered_set<drogon::WebSocketConnectionPtr> m_connections;  ///< 所有连接
        std::unordered_map<uint64_t, std::unordered_set<drogon::WebSocketConnectionPtr>> m_subscriptions; ///< 群订阅映射
        mutable std::mutex m_mutex;  ///< 线程安全锁
    };
}