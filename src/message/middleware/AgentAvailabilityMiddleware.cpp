/// @file AgentAvailabilityMiddleware.cpp
/// @brief Agent 运行状态检查中间件实现

#include <message/MessageContext.hpp>
#include <message/middleware/AgentAvailabilityMiddleware.hpp>
#include <message/runtime/MessageRuntime.hpp>

namespace insoulforge {
    std::string_view AgentAvailabilityMiddleware::id() const noexcept { return "agent_availability"; }

    drogon::Task<MessageFlow> AgentAvailabilityMiddleware::handle(MessageContext &context) const {
        co_return context.runtime().isAgentRunning() ? MessageFlow::Continue : MessageFlow::Stop;
    }
} // namespace insoulforge
