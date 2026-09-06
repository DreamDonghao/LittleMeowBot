/// @file SessionStatisticsSubscriber.cpp
/// @brief 会话统计订阅者实现

#include <event/DomainEvent.hpp>
#include <event/EventBus.hpp>
#include <event/subscribers/SessionStatisticsSubscriber.hpp>
#include <service/SessionConfigManager.hpp>
#include <util/Logger.hpp>

namespace insoulforge {
    std::string_view SessionStatisticsSubscriber::id() const noexcept { return "session_statistics"; }

    void SessionStatisticsSubscriber::registerHandlers(EventBus &eventBus) const {
        eventBus.subscribe<MessageProcessingCompletedEvent>(
          id(), [](const MessageProcessingCompletedEvent &event) -> drogon::Task<> {
              SessionConfigManager::incrementMessageCount(event.sessionId, event.contentSize);
              const auto [messageCount, characterCount] = SessionConfigManager::getConfig(event.sessionId);
              Logger::session(event.sessionId)
                .info("会话统计数据: 接收总消息数{}条,接收总字符(字节)数{}个", messageCount, characterCount);
              co_return;
          });
    }
} // namespace insoulforge
