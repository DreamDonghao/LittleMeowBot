/// @file SessionStore.hpp
/// @brief 会话（群）配置与启用状态存储
/// @author donghao
/// @date 2026-08-30
/// @details 表：group_config（消息统计）、enabled_groups（启用状态与群名称）

#pragma once
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

namespace insoulforge {
    /// @brief 会话配置结构
    struct SessionConfig {
        uint64_t allMesCount = 0;
        uint64_t allCharCount = 0;
    };

    /// @brief 会话（群）配置与启用状态存储
    namespace SessionStore {
        [[nodiscard]] SessionConfig getSessionConfig(uint64_t sessionId);

        void saveSessionConfig(uint64_t sessionId, const SessionConfig &config);

        void incrementMessageCount(uint64_t sessionId, size_t charCount);

        [[nodiscard]] bool hasSessionConfig(uint64_t sessionId);

        [[nodiscard]] bool isSessionEnabled(uint64_t sessionId);

        void enableSession(uint64_t sessionId);

        void disableSession(uint64_t sessionId);

        [[nodiscard]] std::vector<uint64_t> getEnabledGroups();

        /// @brief 获取所有有聊天记录的群（用于聊天记录页面）
        [[nodiscard]] std::vector<std::tuple<uint64_t, std::string, int>> getSessionsWithChatRecords();

        /// @brief 获取所有群（包括已禁用的）
        [[nodiscard]] std::vector<std::tuple<uint64_t, std::string, bool, int>> getAllSessionsWithStatus();

        /// @brief 切换群启用状态
        void toggleSessionStatus(uint64_t sessionId);

        void updateSessionName(uint64_t sessionId, const std::string &name);

        [[nodiscard]] std::string getSessionName(uint64_t sessionId);
    } // namespace SessionStore
} // namespace insoulforge
