/// @file AgentAvailabilityMiddleware.cpp
/// @brief Agent 运行状态检查中间件实现

#include <agent/runtime/AgentSystem.hpp>
#include <message/middleware/AgentAvailabilityMiddleware.hpp>

namespace insoulforge {
    std::string_view AgentAvailabilityMiddleware::id() const noexcept { return "agent_availability"; }

    drogon::Task<MessageFlow> AgentAvailabilityMiddleware::handle(MessageContext &) const {
        co_return AgentSystem::instance().isRunning() ? MessageFlow::Continue : MessageFlow::Stop;
    }
} // namespace insoulforge
