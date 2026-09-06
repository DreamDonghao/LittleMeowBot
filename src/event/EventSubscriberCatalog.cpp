/// @file EventSubscriberCatalog.cpp
/// @brief 内置领域事件订阅者注册

#include <array>

#include <event/EventSubscriber.hpp>
#include <event/EventSubscriberCatalog.hpp>
#include <event/subscribers/MemoryMaintenanceSubscriber.hpp>
#include <event/subscribers/MessageWebSocketSubscriber.hpp>
#include <event/subscribers/SessionStatisticsSubscriber.hpp>

namespace insoulforge {
    void EventSubscriberCatalog::registerBuiltinSubscribers(EventBus &eventBus) {
        const MessageWebSocketSubscriber messageWebSocketSubscriber;
        const SessionStatisticsSubscriber sessionStatisticsSubscriber;
        const MemoryMaintenanceSubscriber memoryMaintenanceSubscriber;
        const std::array<const EventSubscriber *, 3> subscribers = {
          &messageWebSocketSubscriber,
          &sessionStatisticsSubscriber,
          &memoryMaintenanceSubscriber,
        };
        for (const auto *subscriber: subscribers) {
            subscriber->registerHandlers(eventBus);
        }
    }
} // namespace insoulforge
