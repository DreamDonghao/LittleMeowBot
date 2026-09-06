/// @file MessageContext.cpp
/// @brief 入站消息处理链路上下文实现

#include <message/MessageContext.hpp>
#include <message/runtime/MessageRuntime.hpp>

namespace insoulforge {
    MessageContext::MessageContext(json event, const MessageRuntime &runtime) :
        m_event(std::move(event)), m_runtime(runtime) {}

    json &MessageContext::event() { return m_event; }

    const MessageRuntime &MessageContext::runtime() const noexcept { return m_runtime; }

    void MessageContext::createMessage() {
        // 事件归一化完成后不再需要原始 JSON，转移其所有权以避免复制消息段。
        m_message.emplace(std::move(m_event));
    }

    QQMessage &MessageContext::message() { return *m_message; }

    const QQMessage &MessageContext::message() const { return *m_message; }

    uint64_t MessageContext::sessionId() const { return message().getSessionId(); }

    uint64_t MessageContext::logSessionId() const {
        if (m_message) {
            return m_message->getSessionId();
        }

        const uint64_t groupId = getUInt(m_event, "group_id", 0);
        if (groupId != 0) {
            return groupId;
        }
        const uint64_t userId = getUInt(m_event, "user_id", getUInt(atOrNull(m_event, "sender"), "user_id", 0));
        return userId == 0 ? 0 : userId | QQMessage::kPrivateSessionFlag;
    }

    uint64_t MessageContext::logMessageId() const {
        return m_message ? m_message->getMessageId() : getUInt(m_event, "message_id", 0);
    }

    ChatRecordManager &MessageContext::chatRecords() {
        if (!m_chatRecords) {
            // 命令、格式化失败或被短路的消息无需构造记录管理器。
            m_chatRecords.emplace(sessionId());
        }
        return *m_chatRecords;
    }

    MemoryManager &MessageContext::memory() {
        if (!m_memory) {
            // 只有 Agent 节点会访问短期记忆，延迟到实际使用时再构造。
            m_memory.emplace(sessionId());
        }
        return *m_memory;
    }

    drogon::Task<> MessageContext::sendReply(std::string content) {
        co_await m_runtime.sendReply(message(), chatRecords(), std::move(content));
    }
} // namespace insoulforge
