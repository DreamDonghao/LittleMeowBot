/// @file SessionEnabledMiddleware.hpp
/// @brief 会话启用状态检查中间件

#pragma once

#include <message/MessageMiddleware.hpp>

namespace insoulforge {
    /// @brief 忽略未启用会话中的非命令消息
    class SessionEnabledMiddleware final : public MessageMiddleware {
    public:
        /// @copydoc MessageMiddleware::id
        [[nodiscard]] std::string_view id() const noexcept override;

        /// @brief 检查当前会话是否启用
        /// @param context 已创建 QQMessage 的消息上下文
        /// @return 启用时返回 Continue，未启用时返回 Stop
        drogon::Task<MessageFlow> handle(MessageContext &context) const override;
    };
} // namespace insoulforge
