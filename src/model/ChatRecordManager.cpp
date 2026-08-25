/// @file ChatRecordManager.cpp
/// @brief 聊天记录管理器 - 实现

#include <model/ChatRecordManager.hpp>
#include <config/Config.hpp>

namespace LittleMeowBot {
    ChatRecordManager::ChatRecordManager(uint64_t groupId) : m_groupId(groupId) {
    }

    uint64_t ChatRecordManager::getGroupId() const {
        return m_groupId;
    }

    void ChatRecordManager::addUserRecord(const std::string &content) const {
        Database::instance().addChatRecord(m_groupId, "user", content);
    }

    void ChatRecordManager::addAssistantRecord(const std::string &content) const {
        Database::instance().addChatRecord(m_groupId, "assistant", content);
    }

    std::deque<Json::Value> ChatRecordManager::getRecords() const {
        const uint64_t watermark = Database::instance().getMemoryWatermark(m_groupId);
        const auto records = Database::instance().getChatRecordsSince(
            m_groupId, watermark, Config::instance().windowTriggerCount);
        return {records.begin(), records.end()};
    }
}
