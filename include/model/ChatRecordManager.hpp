/// @file ChatRecordManager.hpp
/// @brief 聊天记录管理器 - 群聊历史记录存储与检索
/// @author donghao
/// @date 2026-04-02
/// @details 管理单个群组的聊天记录，使用 SQLite 持久化存储。
///          支持用户消息和 AI 回复的记录与检索。

#pragma once
#include <storage/Database.hpp>
#include <json/value.h>
#include <deque>

namespace LittleMeowBot {
    /// @brief 聊天记录管理类
    /// @details 管理单个群组的聊天记录，使用 SQLite 存储。
    ///          每个群组对应一个 ChatRecordManager 实例。
    class ChatRecordManager {
    public:
        /// @brief 构造函数
        /// @param groupId 群号
        explicit ChatRecordManager(uint64_t groupId);

        /// @brief 获取群号
        /// @return 群号
        [[nodiscard]] uint64_t getGroupId() const;

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
        uint64_t m_groupId; ///< 群号
    };
}
