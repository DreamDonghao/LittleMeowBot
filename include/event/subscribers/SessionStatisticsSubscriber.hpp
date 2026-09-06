/// @file SessionStatisticsSubscriber.hpp
/// @brief 会话统计订阅者

#pragma once

#include <functional>

#include <event/DomainEvent.hpp>
#include <event/EventSubscriber.hpp>

namespace insoulforge {
    /// @brief 在消息主处理完成后更新会话统计数据
    class SessionStatisticsSubscriber final : public EventSubscriber {
    public:
        /// @brief 记录一条已完成消息的统计数据
        using StatisticsRecorder = std::function<void(const MessageProcessingCompletedEvent &event)>;

        /// @brief 使用会话统计能力创建订阅者
        /// @param statisticsRecorder 更新会话统计的回调
        /// @throws std::invalid_argument 回调为空
        explicit SessionStatisticsSubscriber(StatisticsRecorder statisticsRecorder);

        /// @copydoc EventSubscriber::id
        [[nodiscard]] std::string_view id() const noexcept override;

        /// @copydoc EventSubscriber::registerHandlers
        void registerHandlers(EventBus &eventBus) const override;

    private:
        StatisticsRecorder m_statisticsRecorder;
    };
} // namespace insoulforge
