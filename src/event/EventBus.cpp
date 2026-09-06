/// @file EventBus.cpp
/// @brief 进程内领域事件总线实现

#include <algorithm>
#include <stdexcept>

#include <event/EventBus.hpp>
#include <event/EventSubscriberCatalog.hpp>
#include <util/Logger.hpp>

namespace insoulforge {
    EventBus &EventBus::instance() {
        static EventBus eventBus;
        return eventBus;
    }

    void EventBus::initialize() {
        initialize([](EventBus &eventBus) { EventSubscriberCatalog::registerBuiltinSubscribers(eventBus); });
    }

    void EventBus::initialize(std::function<void(EventBus &)> registrar) {
        if (m_initialized) {
            return;
        }
        if (!registrar) {
            throw std::invalid_argument("领域事件订阅者注册器不能为空");
        }

        m_initializing = true;
        try {
            registrar(*this);
            m_initialized = true;
        } catch (...) {
            for (auto &handlers: m_handlers) {
                handlers.clear();
            }
            m_initializing = false;
            throw;
        }
        m_initializing = false;
    }

    void EventBus::subscribeHandler(
      const DomainEventType eventType, const std::string_view subscriberId, EventHandler handler) {
        if (!m_initializing && !m_initialized) {
            throw std::logic_error("必须先初始化内置领域事件订阅者");
        }
        if (subscriberId.empty() || !handler) {
            throw std::invalid_argument("领域事件订阅者标识和处理函数不能为空");
        }

        auto &handlers = m_handlers[static_cast<size_t>(eventType)];
        if (std::any_of(handlers.begin(), handlers.end(), [subscriberId](const RegisteredHandler &registered) {
                return registered.id == subscriberId;
            })) {
            throw std::invalid_argument("同一领域事件的订阅者标识重复");
        }
        handlers.emplace_back(std::string(subscriberId), std::move(handler));
    }

    void EventBus::ensureInitialized() const {
        if (!m_initialized) {
            throw std::logic_error("领域事件总线尚未初始化");
        }
    }

    drogon::Task<> EventBus::publish(DomainEvent event) const {
        ensureInitialized();

        const DomainEventType eventType = domainEventType(event);
        const auto &handlers = m_handlers[static_cast<size_t>(eventType)];
        const uint64_t sessionId = domainEventSessionId(event);
        const uint64_t messageId = domainEventMessageId(event);
        for (const auto &registered: handlers) {
            try {
                co_await registered.handler(event);
            } catch (const std::exception &error) {
                Logger::session(sessionId)
                  .error("[EventBus] 事件 {} 的订阅者 {} 处理失败: message_id={}, error={}",
                    domainEventTypeName(eventType), registered.id, messageId, error.what());
            } catch (...) {
                Logger::session(sessionId)
                  .error("[EventBus] 事件 {} 的订阅者 {} 处理失败: message_id={}, 未知异常",
                    domainEventTypeName(eventType), registered.id, messageId);
            }
        }
    }
} // namespace insoulforge
