/// @file MessageWebSocketSubscriber.hpp
/// @brief 消息 WebSocket 推送订阅者

#pragma once

#include <event/EventSubscriber.hpp>

namespace insoulforge {
    /// @brief 将已记录的消息同步给订阅该会话的管理后台连接
    class MessageWebSocketSubscriber final : public EventSubscriber {
    public:
        /// @copydoc EventSubscriber::id
        [[nodiscard]] std::string_view id() const noexcept override;

        /// @copydoc EventSubscriber::registerHandlers
        void registerHandlers(EventBus &eventBus) const override;
    };
} // namespace insoulforge
