/// @file MemoryMaintenanceSubscriber.cpp
/// @brief 记忆维护订阅者实现

#include <event/DomainEvent.hpp>
#include <event/EventBus.hpp>
#include <event/subscribers/MemoryMaintenanceSubscriber.hpp>
#include <stdexcept>
#include <utility>

namespace insoulforge {
    MemoryMaintenanceSubscriber::MemoryMaintenanceSubscriber(MemoryMaintainer memoryMaintainer) :
        m_memoryMaintainer(std::move(memoryMaintainer)) {
        if (!m_memoryMaintainer) {
            throw std::invalid_argument("记忆维护回调不能为空");
        }
    }

    std::string_view MemoryMaintenanceSubscriber::id() const noexcept { return "memory_maintenance"; }

    void MemoryMaintenanceSubscriber::registerHandlers(EventBus &eventBus) const {
        auto memoryMaintainer = m_memoryMaintainer;
        eventBus.subscribe<MessageProcessingCompletedEvent>(id(),
          [memoryMaintainer = std::move(memoryMaintainer)](
            const MessageProcessingCompletedEvent &event) -> drogon::Task<> { co_await memoryMaintainer(event); });
    }
} // namespace insoulforge
