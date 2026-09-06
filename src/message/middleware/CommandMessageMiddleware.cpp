/// @file CommandMessageMiddleware.cpp
/// @brief 管理命令处理中间件实现

#include <controllers/CommandHandler.hpp>
#include <message/MessageContext.hpp>
#include <message/middleware/CommandMessageMiddleware.hpp>
#include <util/Logger.hpp>

namespace insoulforge {
    std::string_view CommandMessageMiddleware::id() const noexcept { return "command"; }

    drogon::Task<MessageFlow> CommandMessageMiddleware::handle(MessageContext &context) const {
        if (!isCommand(context.message())) {
            co_return MessageFlow::Continue;
        }
        Logger::session(context.sessionId()).info("收到命令消息: {}", context.message().getRawMessage());
        co_await context.sendReply(co_await handleCommand(context.message()));
        co_return MessageFlow::Stop;
    }
} // namespace insoulforge
