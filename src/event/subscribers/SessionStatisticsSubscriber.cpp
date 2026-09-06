/// @file SessionStatisticsSubscriber.cpp
/// @brief 会话统计订阅者实现

#include <event/DomainEvent.hpp>
#include <event/EventBus.hpp>
#include <event/subscribers/SessionStatisticsSubscriber.hpp>
#include <stdexcept>
#include <utility>

namespace insoulforge {
    SessionStatisticsSubscriber::SessionStatisticsSubscriber(StatisticsRecorder statisticsRecorder) :
        m_statisticsRecorder(std::move(statisticsRecorder)) {
        if (!m_statisticsRecorder) {
            throw std::invalid_argument("会话统计回调不能为空");
        }
    }

    std::string_view SessionStatisticsSubscriber::id() const noexcept { return "session_statistics"; }

    void SessionStatisticsSubscriber::registerHandlers(EventBus &eventBus) const {
        auto statisticsRecorder = m_statisticsRecorder;
        eventBus.subscribe<MessageProcessingCompletedEvent>(id(),
          [statisticsRecorder = std::move(statisticsRecorder)](
            const MessageProcessingCompletedEvent &event) -> drogon::Task<> {
              statisticsRecorder(event);
              co_return;
          });
    }
} // namespace insoulforge
