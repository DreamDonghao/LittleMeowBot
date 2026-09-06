/// @file CommandMessageMiddleware.hpp
/// @brief 管理命令处理中间件

#pragma once

#include <message/MessageMiddleware.hpp>

namespace insoulforge {
    /// @brief 识别并执行命令消息，命令完成后终止后续消息处理
    class CommandMessageMiddleware final : public MessageMiddleware {
    public:
        /// @copydoc MessageMiddleware::id
        [[nodiscard]] std::string_view id() const noexcept override;

        /// @brief 执行命令并发送命令响应
        /// @param context 已创建 QQMessage 的消息上下文
        /// @return 非命令消息返回 Continue；命令响应发送后返回 Stop
        drogon::Task<MessageFlow> handle(MessageContext &context) const override;
    };
} // namespace insoulforge
