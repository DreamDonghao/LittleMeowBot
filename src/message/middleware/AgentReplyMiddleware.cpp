/// @file AgentReplyMiddleware.cpp
/// @brief Agent 回复处理中间件实现

#include <agent/runtime/AgentSystem.hpp>
#include <exception>
#include <message/MessageContext.hpp>
#include <message/middleware/AgentReplyMiddleware.hpp>
#include <util/Logger.hpp>

namespace insoulforge {
    std::string_view AgentReplyMiddleware::id() const noexcept { return "agent_reply"; }

    drogon::Task<MessageFlow> AgentReplyMiddleware::handle(MessageContext &context) const {
        auto &agentSystem = AgentSystem::instance();
        const auto log = Logger::session(context.sessionId());
        try {
            if (auto result = co_await agentSystem.process(context.chatRecords(), context.memory(), context.message());
              result && !result->empty() && agentSystem.isRunning()) {
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
