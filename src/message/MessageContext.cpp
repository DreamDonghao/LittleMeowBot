/// @file MessageContext.cpp
/// @brief 入站消息处理链路上下文实现

#include <message/MessageContext.hpp>
#include <service/MessageService.hpp>

namespace insoulforge {
    MessageContext::MessageContext(json event) : m_event(std::move(event)) {}

    json &MessageContext::event() { return m_event; }

    void MessageContext::createMessage() {
        // 事件归一化完成后不再需要原始 JSON，转移其所有权以避免复制消息段。
        m_message.emplace(std::move(m_event));
    }

    QQMessage &MessageContext::message() { return *m_message; }

    const QQMessage &MessageContext::message() const { return *m_message; }

    uint64_t MessageContext::sessionId() const { return message().getSessionId(); }

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
        if (message().isPrivate()) {
            co_await MessageService::sendPrivateMsg(message().getUserId(), std::move(content), chatRecords());
        } else {
            co_await MessageService::sendGroupMsg(message().getGroupId(), std::move(content), chatRecords());
        }
    }
} // namespace insoulforge
