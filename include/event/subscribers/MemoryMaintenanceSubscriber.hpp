/// @file MemoryMaintenanceSubscriber.hpp
/// @brief 记忆维护订阅者

#pragma once

#include <functional>

#include <drogon/utils/coroutine.h>
#include <event/DomainEvent.hpp>
#include <event/EventSubscriber.hpp>

namespace insoulforge {
    /// @brief 在消息主处理完成后按窗口条件维护会话记忆
    class MemoryMaintenanceSubscriber final : public EventSubscriber {
    public:
        /// @brief 维护一条已完成消息所属会话记忆的异步副作用
        using MemoryMaintainer = std::function<drogon::Task<>(const MessageProcessingCompletedEvent &event)>;

        /// @brief 使用记忆维护能力创建订阅者
        /// @param memoryMaintainer 维护会话记忆的异步回调
        /// @throws std::invalid_argument 回调为空
        explicit MemoryMaintenanceSubscriber(MemoryMaintainer memoryMaintainer);

        /// @copydoc EventSubscriber::id
        [[nodiscard]] std::string_view id() const noexcept override;

        /// @copydoc EventSubscriber::registerHandlers
        void registerHandlers(EventBus &eventBus) const override;

    private:
        MemoryMaintainer m_memoryMaintainer;
    };
} // namespace insoulforge
