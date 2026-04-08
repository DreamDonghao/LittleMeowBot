/// @file MemoryManager.hpp
/// @brief 短期记忆管理器 - 群聊记忆存储与检索
/// @author donghao
/// @date 2026-04-02
/// @details 管理单个群组的短期记忆，支持存储、更新和检索操作。
///          短期记忆以纯文本形式存储，每行一条记忆条目。

#pragma once
#include <storage/Database.hpp>
#include <string>

namespace LittleMeowBot {
    /// @brief 短期记忆管理类
    /// @details 管理单个群组的短期记忆，每行一条记忆条目。
    ///          用于 LLM 上下文中提供记忆信息。
    class MemoryManager{
    public:
        /// @brief 构造函数
        /// @param groupId 群号
        explicit MemoryManager(uint64_t groupId);

        /// @brief 设置群号
        /// @param groupId 群号
        void setGroupId(uint64_t groupId);

        /// @brief 获取群号
        /// @return 群号
        uint64_t getGroupId() const;

        /// @brief 更新短期记忆
        /// @param content 记忆内容（每行一条）
        void updateMemory(const std::string& content) const;

        /// @brief 获取短期记忆
        /// @return 记忆内容（每行一条）
        [[nodiscard]] std::string getMemory() const;

    private:
        uint64_t m_groupId;  ///< 群号
    };
}