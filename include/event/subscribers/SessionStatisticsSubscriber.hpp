/// @file SessionStatisticsSubscriber.hpp
/// @brief 会话统计订阅者

#pragma once

#include <event/EventSubscriber.hpp>

namespace insoulforge {
    /// @brief 在消息主处理完成后更新会话统计数据
    class SessionStatisticsSubscriber final : public EventSubscriber {
    public:
        /// @copydoc EventSubscriber::id
        [[nodiscard]] std::string_view id() const noexcept override;

        /// @copydoc EventSubscriber::registerHandlers
        void registerHandlers(EventBus &eventBus) const override;
    };
} // namespace insoulforge
