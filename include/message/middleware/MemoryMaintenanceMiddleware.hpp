/// @file MemoryMaintenanceMiddleware.hpp
/// @brief 记忆维护中间件

#pragma once

#include <message/MessageMiddleware.hpp>

namespace insoulforge {
    /// @brief 消息处理结束后按窗口条件提取并合并记忆
    class MemoryMaintenanceMiddleware final : public MessageMiddleware {
    public:
        /// @copydoc MessageMiddleware::id
        [[nodiscard]] std::string_view id() const noexcept override;

        /// @brief 维护当前会话的短期与长期记忆
        /// @param context 已完成 Agent 与统计阶段的消息上下文
        /// @return 始终返回 Continue；记忆异常被记录后不向外传播
        drogon::Task<MessageFlow> handle(MessageContext &context) const override;
    };
} // namespace insoulforge
