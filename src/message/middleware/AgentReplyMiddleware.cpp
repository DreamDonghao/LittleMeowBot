/// @file AgentReplyMiddleware.cpp
/// @brief Agent 回复处理中间件实现

#include <exception>
#include <message/MessageContext.hpp>
#include <message/middleware/AgentReplyMiddleware.hpp>
#include <message/runtime/MessageRuntime.hpp>
#include <util/Logger.hpp>

namespace insoulforge {
    std::string_view AgentReplyMiddleware::id() const noexcept { return "agent_reply"; }

    drogon::Task<MessageFlow> AgentReplyMiddleware::handle(MessageContext &context) const {
        const auto log = Logger::session(context.sessionId());
        try {
            if (auto result =
                  co_await context.runtime().processAgent(context.chatRecords(), context.memory(), context.message());
              result && !result->empty() && context.runtime().isAgentRunning()) {
                log.info("多层代理决定回复");
                co_await context.sendReply(std::move(*result));
            } else {
                log.info("多层代理决定不回复");
            }
        } catch (const std::exception &error) {
            log.error("消息处理异常: {}", error.what());
        }
        co_return MessageFlow::Continue;
    }
} // namespace insoulforge
