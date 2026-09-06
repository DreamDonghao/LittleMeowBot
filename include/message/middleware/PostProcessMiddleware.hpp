/// @file PostProcessMiddleware.hpp
/// @brief 消息处理完成事件发布中间件

#pragma once

#include <message/MessageMiddleware.hpp>

namespace insoulforge {
    /// @brief 发布消息主处理完成事件，触发统计和记忆等后处理订阅者
    class PostProcessMiddleware final : public MessageMiddleware {
    public:
        /// @copydoc MessageMiddleware::id
        [[nodiscard]] std::string_view id() const noexcept override;

        /// @brief 发布当前消息的处理完成事件
        /// @param context 已完成 Agent 处理的消息上下文
        /// @return 始终返回 Continue
        drogon::Task<MessageFlow> handle(MessageContext &context) const override;
    };
} // namespace insoulforge
