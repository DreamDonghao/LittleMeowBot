/// @file EventSubscriber.hpp
/// @brief 领域事件订阅者接口

#pragma once

#include <string_view>

namespace insoulforge {
    class EventBus;

    /// @brief 一组领域事件处理函数的显式注册单元
    class EventSubscriber {
    public:
        /// @brief 虚析构，支持通过接口指针管理订阅者
        virtual ~EventSubscriber() = default;

        /// @brief 获取订阅者的稳定标识
        /// @return 非空订阅者标识
        [[nodiscard]] virtual std::string_view id() const noexcept = 0;

        /// @brief 向事件总线注册该订阅者关心的事件处理函数
        /// @param eventBus 已进入初始化状态的事件总线
        virtual void registerHandlers(EventBus &eventBus) const = 0;
    };
} // namespace insoulforge
