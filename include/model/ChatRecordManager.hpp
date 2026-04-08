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
    class ChatRecordManager{
    public:
        /// @brief 构造函数
        /// @param groupId 群号
        explicit ChatRecordManager(uint64_t groupId);

        /// @brief 设置群号
        /// @param groupId 群号
        void setGroupId(uint64_t groupId);

        /// @brief 获取群号
        /// @return 群号
        uint64_t getGroupId() const;

        /// @brief 添加用户消息记录
        /// @param content 消息内容
        void addUserRecord(const std::string& content) const;

        /// @brief 添加 AI 回复记录
        /// @param content 回复内容
        void addAssistantRecord(const std::string& content) const;

        /// @brief 获取聊天记录 JSON 数组
        /// @return JSON 数组，每条记录包含 role 和 content 字段
        [[nodiscard]] Json::Value getRecordsJson() const;

        /// @brief 获取聊天记录文本
        /// @return 格式化的聊天记录文本
        [[nodiscard]] std::string getRecordsText() const;

        /// @brief 获取聊天记录双端队列
        /// @return 聊天记录队列
        [[nodiscard]] std::deque<Json::Value> getRecords() const;

        /// @brief 获取聊天记录数量
        /// @return 记录数量
        [[nodiscard]] size_t getRecordCount() const;

        /// @brief 清理旧记录
        /// @details 保留最近 N 条记录，删除其余记录
        void clearOldRecords() const;

    private:
        uint64_t m_groupId; ///< 群号
    };
}
