/// @file PostProcessMiddleware.cpp
/// @brief 消息处理完成事件发布中间件实现

#include <event/DomainEvent.hpp>
#include <message/MessageContext.hpp>
#include <message/middleware/PostProcessMiddleware.hpp>
#include <message/runtime/MessageRuntime.hpp>

namespace insoulforge {
    std::string_view PostProcessMiddleware::id() const noexcept { return "post_process"; }

    drogon::Task<MessageFlow> PostProcessMiddleware::handle(MessageContext &context) const {
        const auto &message = context.message();
        co_await context.runtime().publish(MessageProcessingCompletedEvent{
          .sessionId = context.sessionId(),
          .messageId = message.getMessageId(),
          .contentSize = message.getFormatMessage().size(),
        });
        co_return MessageFlow::Continue;
    }
} // namespace insoulforge
