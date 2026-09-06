/// @file RecordMessageMiddleware.cpp
/// @brief 聊天记录持久化中间件实现

#include <message/MessageContext.hpp>
#include <message/middleware/RecordMessageMiddleware.hpp>
#include <service/WebSocketManager.hpp>

namespace insoulforge {
    std::string_view RecordMessageMiddleware::id() const noexcept { return "record_message"; }

    drogon::Task<MessageFlow> RecordMessageMiddleware::handle(MessageContext &context) const {
        const std::string formattedMessage = context.message().getFormatMessage();
        if (context.message().getSelfQQNumber() == context.message().getSenderQQNumber()) {
            context.chatRecords().addAssistantRecord(formattedMessage);
            WebSocketManager::instance().pushMessage(context.sessionId(), "assistant", formattedMessage);
        } else {
            context.chatRecords().addUserRecord(formattedMessage);
            WebSocketManager::instance().pushMessage(context.sessionId(), "user", formattedMessage);
        }
        co_return MessageFlow::Continue;
    }
} // namespace insoulforge
