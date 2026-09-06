/// @file MessageWebSocketSubscriber.cpp
/// @brief 消息 WebSocket 推送订阅者实现

#include <event/DomainEvent.hpp>
#include <event/EventBus.hpp>
#include <event/subscribers/MessageWebSocketSubscriber.hpp>
#include <stdexcept>
#include <utility>

namespace insoulforge {
    MessageWebSocketSubscriber::MessageWebSocketSubscriber(MessagePusher messagePusher) :
        m_messagePusher(std::move(messagePusher)) {
        if (!m_messagePusher) {
            throw std::invalid_argument("消息推送回调不能为空");
        }
    }

    std::string_view MessageWebSocketSubscriber::id() const noexcept { return "message_websocket"; }

    void MessageWebSocketSubscriber::registerHandlers(EventBus &eventBus) const {
        auto messagePusher = m_messagePusher;
        eventBus.subscribe<MessageRecordedEvent>(
          id(), [messagePusher = std::move(messagePusher)](const MessageRecordedEvent &event) -> drogon::Task<> {
              messagePusher(event);
              co_return;
          });
    }
} // namespace insoulforge
