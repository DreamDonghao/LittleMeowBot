/// @file EventSubscriberCatalog.hpp
/// @brief 内置领域事件订阅者的显式组合

#pragma once

namespace insoulforge {
    class EventBus;

    namespace EventSubscriberCatalog {
        /// @brief 按注册顺序注册全部内置订阅者
        /// @param eventBus 正在初始化的事件总线
        void registerBuiltinSubscribers(EventBus &eventBus);
    } // namespace EventSubscriberCatalog
} // namespace insoulforge
