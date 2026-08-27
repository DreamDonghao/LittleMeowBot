/// @file ChatRecordManager.cpp
/// @brief 聊天记录管理器 - 实现

#include <model/ChatRecordManager.hpp>
#include <config/Config.hpp>

namespace LittleMeowBot {
    ChatRecordManager::ChatRecordManager(uint64_t sessionId) : m_sessionId(sessionId) {
    }

    uint64_t ChatRecordManager::getSessionId() const {
        return m_sessionId;
    }

    void ChatRecordManager::addUserRecord(const std::string &content) const {
        Database::instance().addChatRecord(m_sessionId, "user", content);
    }

    void ChatRecordManager::addAssistantRecord(const std::string &content) const {
        Database::instance().addChatRecord(m_sessionId, "assistant", content);
    }

    std::deque<Json::Value> ChatRecordManager::getRecords() const {
        const uint64_t watermark = Database::instance().getMemoryWatermark(m_sessionId);
        const auto records = Database::instance().getChatRecordsSince(
            m_sessionId, watermark, Config::instance().windowTriggerCount);
        return {records.begin(), records.end()};
    }
}
