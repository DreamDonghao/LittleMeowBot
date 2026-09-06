/// @file DomainEvent.hpp
/// @brief 领域事件的强类型载荷定义

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace insoulforge {
    /// @brief 已支持的领域事件类别
    enum class DomainEventType : size_t {
        MessageRecorded,
        MessageProcessingCompleted,
        Count,
    };

    /// @brief 获取领域事件类别的稳定日志名称
    /// @param type 领域事件类别
    /// @return 适合日志与诊断输出的事件名称
    [[nodiscard]] constexpr std::string_view domainEventTypeName(const DomainEventType type) {
        switch (type) {
        case DomainEventType::MessageRecorded:
            return "message_recorded";
        case DomainEventType::MessageProcessingCompleted:
            return "message_processing_completed";
        case DomainEventType::Count:
            return "unknown";
        }
        return "unknown";
    }

    /// @brief 聊天记录的消息角色
    enum class MessageRole {
        User,
        Assistant,
    };

    /// @brief 转换消息角色为管理后台使用的字符串
    [[nodiscard]] constexpr std::string_view messageRoleName(const MessageRole role) {
        return role == MessageRole::User ? "user" : "assistant";
    }

    /// @brief 一条消息已写入聊天记录
    struct MessageRecordedEvent {
        static constexpr DomainEventType kType = DomainEventType::MessageRecorded;

        uint64_t sessionId; ///< 所属会话 ID
        uint64_t messageId; ///< OneBot 消息 ID
        MessageRole role; ///< 消息角色
        std::string recordContent; ///< 写入聊天记录的格式化内容
        std::string displayContent; ///< 推送给管理后台的展示内容
    };

    /// @brief 一条普通消息的主处理链路已经结束
    struct MessageProcessingCompletedEvent {
        static constexpr DomainEventType kType = DomainEventType::MessageProcessingCompleted;

        uint64_t sessionId; ///< 所属会话 ID
        uint64_t messageId; ///< OneBot 消息 ID
        size_t contentSize; ///< 格式化消息的字节长度，用于会话统计
    };

    /// @brief 所有领域事件载荷的封闭联合
    using DomainEvent = std::variant<MessageRecordedEvent, MessageProcessingCompletedEvent>;

    /// @brief 获取领域事件类别
    /// @param event 领域事件
    /// @return 对应的事件类别
    [[nodiscard]] constexpr DomainEventType domainEventType(const DomainEvent &event) {
        return std::visit([](const auto &payload) { return std::decay_t<decltype(payload)>::kType; }, event);
    }

    /// @brief 获取领域事件所属会话 ID
    /// @param event 领域事件
    /// @return 事件的会话 ID
    [[nodiscard]] constexpr uint64_t domainEventSessionId(const DomainEvent &event) {
        return std::visit([](const auto &payload) { return payload.sessionId; }, event);
    }

    /// @brief 获取领域事件关联的消息 ID
    /// @param event 领域事件
    /// @return 事件的消息 ID
    [[nodiscard]] constexpr uint64_t domainEventMessageId(const DomainEvent &event) {
        return std::visit([](const auto &payload) { return payload.messageId; }, event);
    }
} // namespace insoulforge
