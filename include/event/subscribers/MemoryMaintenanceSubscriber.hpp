/// @file MemoryMaintenanceSubscriber.hpp
/// @brief 记忆维护订阅者

#pragma once

#include <event/EventSubscriber.hpp>

namespace insoulforge {
    /// @brief 在消息主处理完成后按窗口条件维护会话记忆
    class MemoryMaintenanceSubscriber final : public EventSubscriber {
    public:
        /// @copydoc EventSubscriber::id
        [[nodiscard]] std::string_view id() const noexcept override;

        /// @copydoc EventSubscriber::registerHandlers
        void registerHandlers(EventBus &eventBus) const override;
    };
} // namespace insoulforge
