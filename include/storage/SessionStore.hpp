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
    class SessionStore {
    public:
        static SessionStore &instance();

        SessionConfig getSessionConfig(uint64_t sessionId) const;

        void saveSessionConfig(uint64_t sessionId, const SessionConfig &config) const;

        void incrementMessageCount(uint64_t sessionId, size_t charCount) const;

        bool hasSessionConfig(uint64_t sessionId) const;

        bool isSessionEnabled(uint64_t sessionId) const;

        void enableSession(uint64_t sessionId) const;

        void disableSession(uint64_t sessionId) const;

        std::vector<uint64_t> getEnabledGroups() const;

        /// @brief 获取所有有聊天记录的群（用于聊天记录页面）
        std::vector<std::tuple<uint64_t, std::string, int> > getSessionsWithChatRecords() const;

        /// @brief 获取所有群（包括已禁用的）
        std::vector<std::tuple<uint64_t, std::string, bool, int> > getAllSessionsWithStatus() const;

        /// @brief 切换群启用状态
        void toggleSessionStatus(uint64_t sessionId) const;

        void updateSessionName(uint64_t sessionId, const std::string &name) const;

        std::string getSessionName(uint64_t sessionId) const;

    private:
        SessionStore() = default;
    };
} // namespace insoulforge