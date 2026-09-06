/// @file EventBus.hpp
/// @brief 进程内领域事件总线

#pragma once

#include <array>
#include <concepts>
#include <drogon/utils/coroutine.h>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <event/DomainEvent.hpp>

namespace insoulforge {
    /// @brief 进程内领域事件发布与订阅中心
    /// @details 订阅者按注册顺序执行；单个订阅者失败仅记录日志，不影响其他订阅者或事件发布者。
    class EventBus {
    public:
        /// @brief 获取进程内唯一的事件总线
        /// @return 单例 EventBus
        [[nodiscard]] static EventBus &instance();

        /// @brief 注册内置领域事件订阅者；重复调用无副作用
        /// @throws std::invalid_argument 内置订阅者无效或同一事件内标识重复时抛出
        /// @pre 必须在消息处理链路开始前调用。
        void initialize();

        /// @brief 使用指定注册器初始化事件总线
        /// @param registrar 向当前事件总线注册订阅者的函数
        /// @throws std::invalid_argument 注册器为空、订阅者无效或标识重复时抛出
        /// @pre 必须在消息处理链路开始前调用。
        /// @details 用于自定义组合和契约测试；重复调用无副作用。
        void initialize(std::function<void(EventBus &)> registrar);

        /// @brief 为指定事件类型注册订阅者
        /// @tparam Event 事件载荷类型
        /// @tparam Handler 接收 `const Event &` 并返回 `drogon::Task<>` 的可调用对象
        /// @param subscriberId 在当前事件类型内唯一的订阅者标识
        /// @param handler 异步事件处理函数
        /// @throws std::invalid_argument 标识为空、处理函数为空或标识重复时抛出
        /// @throws std::logic_error 内置订阅者尚未初始化时抛出
        /// @pre 应在 HTTP 服务开始接收请求前调用。
        template<typename Event, typename Handler>
            requires std::same_as<std::invoke_result_t<Handler, const Event &>, drogon::Task<>>
        void subscribe(std::string_view subscriberId, Handler &&handler) {
            subscribeHandler(Event::kType, subscriberId,
              [handler = std::forward<Handler>(handler)](const DomainEvent &event) mutable -> drogon::Task<> {
                  co_await std::invoke(handler, std::get<Event>(event));
              });
        }

        /// @brief 发布一个领域事件
        /// @tparam Event 事件载荷类型
        /// @param event 待分发的事件载荷
        /// @throws std::logic_error 内置订阅者尚未初始化时抛出
        template<typename Event>
        drogon::Task<> publish(Event event) const {
            co_await publish(DomainEvent{std::move(event)});
        }

        /// @brief 发布类型擦除后的领域事件
        /// @param event 待分发的领域事件
        /// @throws std::logic_error 内置订阅者尚未初始化时抛出
        drogon::Task<> publish(DomainEvent event) const;

    private:
        using EventHandler = std::function<drogon::Task<>(const DomainEvent &)>;

        struct RegisteredHandler {
            std::string id; ///< 订阅者标识
            EventHandler handler; ///< 已类型擦除的异步处理函数
        };

        static constexpr size_t kEventTypeCount = static_cast<size_t>(DomainEventType::Count);

        /// @brief 注册类型擦除后的事件处理函数
        /// @param eventType 订阅的事件类型
        /// @param subscriberId 在该事件类型内唯一的订阅者标识
        /// @param handler 已类型擦除的异步处理函数
        void subscribeHandler(DomainEventType eventType, std::string_view subscriberId, EventHandler handler);

        /// @brief 确保内置订阅者已完成注册
        /// @throws std::logic_error 初始化尚未完成时抛出
        void ensureInitialized() const;

        std::array<std::vector<RegisteredHandler>, kEventTypeCount> m_handlers; ///< 按事件类型分组的订阅者
        bool m_initializing{false}; ///< 是否正在注册内置订阅者
        bool m_initialized{false}; ///< 内置订阅者是否已完整注册
    };
} // namespace insoulforge
