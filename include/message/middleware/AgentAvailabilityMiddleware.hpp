/// @file AgentAvailabilityMiddleware.hpp
/// @brief Agent 运行状态检查中间件

#pragma once

#include <message/MessageMiddleware.hpp>

namespace insoulforge {
    /// @brief Agent 未运行时终止消息处理链路
    class AgentAvailabilityMiddleware final : public MessageMiddleware {
    public:
        /// @copydoc MessageMiddleware::id
        [[nodiscard]] std::string_view id() const noexcept override;

        /// @brief 检查 Agent 系统是否处于可处理状态
        /// @param context 当前消息上下文；本节点不修改它
        /// @return Agent 运行中返回 Continue，否则返回 Stop
        drogon::Task<MessageFlow> handle(MessageContext &context) const override;
    };
} // namespace insoulforge
