/// @file GroupConfigManager.cpp
/// @brief 群组配置管理器 - 实现

#include <model/GroupConfigManager.hpp>

namespace LittleMeowBot {
    GroupConfig GroupConfigManager::getConfig(const uint64_t groupId) {
        return Database::instance().getGroupConfig(groupId);
    }

    bool GroupConfigManager::contains(const uint64_t groupId) {
        return Database::instance().hasGroupConfig(groupId);
    }

    void GroupConfigManager::addConfig(const uint64_t groupId, const GroupConfig &config) {
        Database::instance().saveGroupConfig(groupId, config);
    }

    void GroupConfigManager::incrementMessageCount(const uint64_t groupId, const size_t charCount) {
        Database::instance().incrementMessageCount(groupId, charCount);
    }
}
