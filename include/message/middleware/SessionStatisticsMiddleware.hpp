/// @file SessionStatisticsMiddleware.hpp
/// @brief 会话统计更新中间件

#pragma once

#include <message/MessageMiddleware.hpp>

namespace insoulforge {
    /// @brief 更新消息条数与字符数统计
    class SessionStatisticsMiddleware final : public MessageMiddleware {
    public:
        /// @copydoc MessageMiddleware::id
        [[nodiscard]] std::string_view id() const noexcept override;

        /// @brief 累加已处理消息的数量与格式化内容长度
        /// @param context 已格式化且已记录的消息上下文
        /// @return 始终返回 Continue
        drogon::Task<MessageFlow> handle(MessageContext &context) const override;
    };
} // namespace insoulforge
