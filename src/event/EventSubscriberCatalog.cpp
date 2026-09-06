/// @file EventSubscriberCatalog.cpp
/// @brief 内置领域事件订阅者注册

#include <string>

#include <event/DomainEvent.hpp>
#include <event/EventSubscriberCatalog.hpp>
#include <event/subscribers/MemoryMaintenanceSubscriber.hpp>
#include <event/subscribers/MessageWebSocketSubscriber.hpp>
#include <event/subscribers/SessionStatisticsSubscriber.hpp>
#include <service/MemoryService.hpp>
#include <service/SessionConfigManager.hpp>
#include <service/WebSocketManager.hpp>
#include <util/Logger.hpp>

namespace insoulforge {
    void EventSubscriberCatalog::registerBuiltinSubscribers(EventBus &eventBus) {
        MessageWebSocketSubscriber messageWebSocketSubscriber([](const MessageRecordedEvent &event) {
            WebSocketManager::instance().pushMessage(
              event.sessionId, std::string(messageRoleName(event.role)), event.displayContent);
        });
        SessionStatisticsSubscriber sessionStatisticsSubscriber([](const MessageProcessingCompletedEvent &event) {
            SessionConfigManager::incrementMessageCount(event.sessionId, event.contentSize);
            const auto [messageCount, characterCount] = SessionConfigManager::getConfig(event.sessionId);
            Logger::session(event.sessionId)
              .info("会话统计数据: 接收总消息数{}条,接收总字符(字节)数{}个", messageCount, characterCount);
        });
        MemoryMaintenanceSubscriber memoryMaintenanceSubscriber(
          [](const MessageProcessingCompletedEvent &event) -> drogon::Task<> {
              co_await MemoryService::appendAndMergeMemory(event.sessionId);
          });

        messageWebSocketSubscriber.registerHandlers(eventBus);
        sessionStatisticsSubscriber.registerHandlers(eventBus);
        memoryMaintenanceSubscriber.registerHandlers(eventBus);
    }
} // namespace insoulforge
