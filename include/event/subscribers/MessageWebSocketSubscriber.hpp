/// @file MessageWebSocketSubscriber.hpp
/// @brief 消息 WebSocket 推送订阅者

#pragma once

#include <functional>

#include <event/DomainEvent.hpp>
#include <event/EventSubscriber.hpp>

namespace insoulforge {
    /// @brief 将已记录的消息同步给订阅该会话的管理后台连接
    class MessageWebSocketSubscriber final : public EventSubscriber {
    public:
        /// @brief 推送一条已记录消息的副作用
        using MessagePusher = std::function<void(const MessageRecordedEvent &event)>;

        /// @brief 使用消息推送能力创建订阅者
        /// @param messagePusher 将消息推送到管理后台的回调
        /// @throws std::invalid_argument 回调为空
        explicit MessageWebSocketSubscriber(MessagePusher messagePusher);

        /// @copydoc EventSubscriber::id
        [[nodiscard]] std::string_view id() const noexcept override;

        /// @copydoc EventSubscriber::registerHandlers
        void registerHandlers(EventBus &eventBus) const override;

    private:
        MessagePusher m_messagePusher;
    };
} // namespace insoulforge
