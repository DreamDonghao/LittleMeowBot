/// @file EventNormalizationMiddleware.hpp
/// @brief OneBot 事件归一化中间件

#pragma once

#include <message/MessageMiddleware.hpp>

namespace insoulforge {
    /// @brief 过滤非消息事件，并将拍一拍事件归一化为消息或聊天记录
    class EventNormalizationMiddleware final : public MessageMiddleware {
    public:
        /// @copydoc MessageMiddleware::id
        [[nodiscard]] std::string_view id() const noexcept override;

        /// @brief 将可处理事件转换为普通消息事件
        /// @param context 仅包含原始 OneBot 事件的消息上下文
        /// @return 普通消息或合成拍一拍消息返回 Continue；其他事件或已处理拍一拍返回 Stop
        drogon::Task<MessageFlow> handle(MessageContext &context) const override;
    };
} // namespace insoulforge
