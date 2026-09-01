/// @file SessionConfigManager.cpp
/// @brief 群组配置管理器 - 实现

#include <service/SessionConfigManager.hpp>
#include <storage/SessionStore.hpp>

namespace insoulforge {
    SessionConfig SessionConfigManager::getConfig(const uint64_t sessionId) {
        return SessionStore::getSessionConfig(sessionId);
    }

    bool SessionConfigManager::contains(const uint64_t sessionId) { return SessionStore::hasSessionConfig(sessionId); }

    void SessionConfigManager::addConfig(const uint64_t sessionId, const SessionConfig &config) {
        SessionStore::saveSessionConfig(sessionId, config);
    }

    void SessionConfigManager::incrementMessageCount(const uint64_t sessionId, const size_t charCount) {
        SessionStore::incrementMessageCount(sessionId, charCount);
    }
} // namespace insoulforge
