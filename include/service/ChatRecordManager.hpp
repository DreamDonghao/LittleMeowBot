/// @file ChatRecordManager.hpp
/// @brief 聊天记录管理器 - 会话历史记录存储与检索
/// @author donghao
/// @date 2026-04-02
/// @details 管理单个会话的聊天记录，使用 SQLite 持久化存储。
///          支持用户消息和 AI 回复的记录与检索。

#pragma once
#include <cstddef>
#include <deque>
#include <json/value.h>

namespace insoulforge {
    /// @brief 提示词中完整保留的最近记录条数（更早记录简化处理；召回缓存按此长度对齐淘汰）
    inline constexpr size_t kRecentRecordCount = 12;

    /// @brief 聊天记录管理类
    /// @details 管理单个会话的聊天记录，使用 SQLite 存储。
    ///          每个会话对应一个 ChatRecordManager 实例。
    class ChatRecordManager {
    public:
        /// @brief 构造函数
        /// @param sessionId 会话 ID（私聊会话带标志位）
        explicit ChatRecordManager(uint64_t sessionId);

        /// @brief 获取会话 ID
        /// @return 群号
        [[nodiscard]] uint64_t getSessionId() const;

        /// @brief 添加用户消息记录
        /// @param content 消息内容
        void addUserRecord(const std::string &content) const;

        /// @brief 添加 AI 回复记录
        /// @param content 回复内容
        void addAssistantRecord(const std::string &content) const;

        /// @brief 获取上下文窗口内的聊天记录（水位线之后的最新记录，旧→新）
        /// @return 聊天记录队列
        [[nodiscard]] std::deque<Json::Value> getRecords() const;

    private:
        uint64_t m_sessionId; ///< 群号
    };
} // namespace insoulforge
