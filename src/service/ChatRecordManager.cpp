/// @file ChatRecordManager.cpp
/// @brief 聊天记录管理器 - 实现

#include <config/Config.hpp>
#include <service/ChatRecordManager.hpp>
#include <storage/ChatRecordStore.hpp>
#include <storage/MemoryStore.hpp>

namespace insoulforge {
    ChatRecordManager::ChatRecordManager(uint64_t sessionId) : m_sessionId(sessionId) {}

    uint64_t ChatRecordManager::getSessionId() const { return m_sessionId; }

    void ChatRecordManager::addUserRecord(const std::string &content) const {
        ChatRecordStore::instance().addChatRecord(m_sessionId, "user", content);
    }

    void ChatRecordManager::addAssistantRecord(const std::string &content) const {
        ChatRecordStore::instance().addChatRecord(m_sessionId, "assistant", content);
    }

    std::deque<Json::Value> ChatRecordManager::getRecords() const {
        const uint64_t watermark = MemoryStore::instance().getMemoryWatermark(m_sessionId);
        const auto records = ChatRecordStore::instance().getChatRecordsSince(
          m_sessionId, watermark, Config::instance().windowTriggerCount);
        return {records.begin(), records.end()};
    }
} // namespace insoulforge
