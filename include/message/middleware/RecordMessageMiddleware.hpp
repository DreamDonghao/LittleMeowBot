/// @file RecordMessageMiddleware.hpp
/// @brief 聊天记录持久化中间件

#pragma once

#include <message/MessageMiddleware.hpp>

namespace insoulforge {
    /// @brief 写入用户或助手消息记录，并推送管理后台事件
    class RecordMessageMiddleware final : public MessageMiddleware {
    public:
        /// @copydoc MessageMiddleware::id
        [[nodiscard]] std::string_view id() const noexcept override;

        /// @brief 持久化格式化后的消息，并同步管理后台
        /// @param context 已格式化的消息上下文
        /// @return 始终返回 Continue
        drogon::Task<MessageFlow> handle(MessageContext &context) const override;
    };
} // namespace insoulforge
