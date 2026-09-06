/// @file MessageWebSocketSubscriber.cpp
/// @brief 消息 WebSocket 推送订阅者实现

#include <event/DomainEvent.hpp>
#include <event/EventBus.hpp>
#include <event/subscribers/MessageWebSocketSubscriber.hpp>
#include <service/WebSocketManager.hpp>

namespace insoulforge {
    std::string_view MessageWebSocketSubscriber::id() const noexcept { return "message_websocket"; }

    void MessageWebSocketSubscriber::registerHandlers(EventBus &eventBus) const {
        eventBus.subscribe<MessageRecordedEvent>(id(), [](const MessageRecordedEvent &event) -> drogon::Task<> {
            WebSocketManager::instance().pushMessage(
              event.sessionId, std::string(messageRoleName(event.role)), event.displayContent);
            co_return;
        });
    }
} // namespace insoulforge
