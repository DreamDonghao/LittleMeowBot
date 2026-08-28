/// @file SessionConfigManager.cpp
/// @brief 群组配置管理器 - 实现

#include <model/SessionConfigManager.hpp>

namespace insoulforge {
    SessionConfig SessionConfigManager::getConfig(const uint64_t sessionId) {
        return Database::instance().getSessionConfig(sessionId);
    }

    bool SessionConfigManager::contains(const uint64_t sessionId) {
        return Database::instance().hasSessionConfig(sessionId);
    }

    void SessionConfigManager::addConfig(const uint64_t sessionId, const SessionConfig &config) {
        Database::instance().saveSessionConfig(sessionId, config);
    }

    void SessionConfigManager::incrementMessageCount(const uint64_t sessionId, const size_t charCount) {
        Database::instance().incrementMessageCount(sessionId, charCount);
    }
}
