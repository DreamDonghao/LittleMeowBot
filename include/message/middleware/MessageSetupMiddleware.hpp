/// @file MessageSetupMiddleware.hpp
/// @brief 消息模型与会话配置初始化中间件

#pragma once

#include <message/MessageMiddleware.hpp>

namespace insoulforge {
    /// @brief 创建 QQMessage，并确保对应会话配置存在
    class MessageSetupMiddleware final : public MessageMiddleware {
    public:
        /// @copydoc MessageMiddleware::id
        [[nodiscard]] std::string_view id() const noexcept override;

        /// @brief 从归一化事件创建消息模型并初始化会话配置
        /// @param context 尚未创建 QQMessage 的消息上下文
        /// @return 始终返回 Continue
        drogon::Task<MessageFlow> handle(MessageContext &context) const override;
    };
} // namespace insoulforge
