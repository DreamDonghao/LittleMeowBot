/// @file RecordMessageMiddleware.cpp
/// @brief 聊天记录持久化中间件实现

#include <event/DomainEvent.hpp>
#include <event/EventBus.hpp>
#include <message/MessageContext.hpp>
#include <message/middleware/RecordMessageMiddleware.hpp>

namespace insoulforge {
    std::string_view RecordMessageMiddleware::id() const noexcept { return "record_message"; }

    drogon::Task<MessageFlow> RecordMessageMiddleware::handle(MessageContext &context) const {
        const std::string formattedMessage = context.message().getFormatMessage();
        const MessageRole role = context.message().getSelfQQNumber() == context.message().getSenderQQNumber()
                                   ? MessageRole::Assistant
                                   : MessageRole::User;
        if (context.message().getSelfQQNumber() == context.message().getSenderQQNumber()) {
            context.chatRecords().addAssistantRecord(formattedMessage);
        } else {
            context.chatRecords().addUserRecord(formattedMessage);
        }
        co_await EventBus::instance().publish(MessageRecordedEvent{
          .sessionId = context.sessionId(),
          .messageId = context.message().getMessageId(),
          .role = role,
          .recordContent = formattedMessage,
          .displayContent = formattedMessage,
        });
        co_return MessageFlow::Continue;
    }
} // namespace insoulforge
