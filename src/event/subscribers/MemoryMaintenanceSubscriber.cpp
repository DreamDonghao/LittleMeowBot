/// @file MemoryMaintenanceSubscriber.cpp
/// @brief 记忆维护订阅者实现

#include <event/DomainEvent.hpp>
#include <event/EventBus.hpp>
#include <event/subscribers/MemoryMaintenanceSubscriber.hpp>
#include <service/MemoryService.hpp>

namespace insoulforge {
    std::string_view MemoryMaintenanceSubscriber::id() const noexcept { return "memory_maintenance"; }

    void MemoryMaintenanceSubscriber::registerHandlers(EventBus &eventBus) const {
        eventBus.subscribe<MessageProcessingCompletedEvent>(
          id(), [](const MessageProcessingCompletedEvent &event) -> drogon::Task<> {
              co_await MemoryService::appendAndMergeMemory(event.sessionId);
          });
    }
} // namespace insoulforge
