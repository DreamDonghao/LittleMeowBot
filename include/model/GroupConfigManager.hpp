/// @file GroupConfigManager.hpp
/// @brief 群组配置管理器 - 群组状态与统计管理
/// @author donghao
/// @date 2026-04-02
/// @details 管理群组配置信息，使用 SQLite 持久化存储。
///          支持群组配置的增删改查和消息计数统计。

#pragma once
#include <storage/Database.hpp>

namespace LittleMeowBot {
    /// @brief 群组配置管理
    /// @details 使用 SQLite 存储群组配置信息
    namespace GroupConfigManager {
        /// @brief 获取群组配置
        /// @param groupId 群号
        /// @return 群组配置结构体
        [[nodiscard]] GroupConfig getConfig(uint64_t groupId);

        /// @brief 检查群组是否存在配置
        /// @param groupId 群号
        /// @return 是否存在
        [[nodiscard]] bool contains(uint64_t groupId);

        /// @brief 添加群组配置
        /// @param groupId 群号
        /// @param config 群组配置
        void addConfig(uint64_t groupId, const GroupConfig &config = GroupConfig());

        /// @brief 增加消息计数
        /// @param groupId 群号
        /// @param charCount 字符数
        void incrementMessageCount(uint64_t groupId, size_t charCount);
    };
}
