/// @file FormatMessageMiddleware.hpp
/// @brief QQ 消息格式化中间件

#pragma once

#include <message/MessageMiddleware.hpp>

namespace insoulforge {
    /// @brief 执行消息格式化，包括图片识别等高成本处理
    class FormatMessageMiddleware final : public MessageMiddleware {
    public:
        /// @copydoc MessageMiddleware::id
        [[nodiscard]] std::string_view id() const noexcept override;

        /// @brief 格式化消息内容
        /// @param context 已通过会话启用检查的消息上下文
        /// @return 始终返回 Continue
        drogon::Task<MessageFlow> handle(MessageContext &context) const override;
    };
} // namespace insoulforge
