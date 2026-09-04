/// @file ChatRecordManager.cpp
/// @brief 聊天记录管理器 - 实现

#include <config/Config.hpp>
#include <service/ChatRecordManager.hpp>
#include <service/MessageRecall.hpp>
#include <storage/ChatRecordStore.hpp>
#include <storage/MemoryStore.hpp>

namespace insoulforge {
    ChatRecordManager::ChatRecordManager(const uint64_t sessionId) : m_sessionId(sessionId) {}

    uint64_t ChatRecordManager::getSessionId() const { return m_sessionId; }

    void ChatRecordManager::addUserRecord(const std::string &content) const {
        ChatRecordStore::addChatRecord(m_sessionId, "user", content);
        MessageRecall::onRecordAdded(m_sessionId, content, false);
    }

    void ChatRecordManager::addAssistantRecord(const std::string &content) const {
        ChatRecordStore::addChatRecord(m_sessionId, "assistant", content);
        MessageRecall::onRecordAdded(m_sessionId, content, true);
    }

    std::deque<json> ChatRecordManager::getRecords() const {
        const uint64_t watermark = MemoryStore::getMemoryWatermark(m_sessionId);
        const auto records =
          ChatRecordStore::getChatRecordsSince(m_sessionId, watermark, Config::instance().windowTriggerCount);
        return {records.begin(), records.end()};
    }
} // namespace insoulforge
