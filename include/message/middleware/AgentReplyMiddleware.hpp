/// @file AgentReplyMiddleware.hpp
/// @brief Agent 回复处理中间件

#pragma once

#include <message/MessageMiddleware.hpp>

namespace insoulforge {
    /// @brief 调用 Agent 处理消息，并按决策发送文本回复
    class AgentReplyMiddleware final : public MessageMiddleware {
    public:
        /// @copydoc MessageMiddleware::id
        [[nodiscard]] std::string_view id() const noexcept override;

        /// @brief 执行 Router 与 Executor，并发送其文本回复
        /// @param context 已格式化且已写入聊天记录的消息上下文
        /// @return 始终返回 Continue；Agent 异常被记录后不影响统计和记忆维护
        drogon::Task<MessageFlow> handle(MessageContext &context) const override;
    };
} // namespace insoulforge
