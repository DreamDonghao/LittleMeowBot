/// @file MessageRuntime.hpp
/// @brief 消息处理链路的可替换运行时依赖

#pragma once

#include <drogon/utils/coroutine.h>
#include <memory>
#include <optional>
#include <string>

#include <event/DomainEvent.hpp>

namespace insoulforge {
    class ChatRecordManager;
    class MemoryManager;
    class QQMessage;

    /// @brief 为消息处理中间件提供 Agent、回复与事件能力
    /// @details MessagePipeline 仅依赖此接口，不直接访问进程级单例。生产环境使用
    /// BuiltinMessageRuntime，测试或嵌入场景可注入具备相同行为的替代实现。
    class MessageRuntime {
    public:
        /// @brief 虚析构，支持通过接口指针管理运行时实现
        virtual ~MessageRuntime() = default;

        /// @brief 判断 Agent 是否接受新的消息处理请求
        /// @return Agent 可处理消息时返回 true
        [[nodiscard]] virtual bool isAgentRunning() const = 0;

        /// @brief 交由 Agent 处理已格式化的消息
        /// @param chatRecords 当前会话的聊天记录
        /// @param memory 当前会话的短期记忆
        /// @param message 已格式化的入站消息
        /// @return Agent 决定回复时返回文本，否则返回 std::nullopt
        virtual drogon::Task<std::optional<std::string>> processAgent(
          ChatRecordManager &chatRecords, MemoryManager &memory, const QQMessage &message) const = 0;

        /// @brief 向当前消息对应的会话发送文本回复
        /// @param message 用于确定群聊或私聊目标的入站消息
        /// @param chatRecords 当前会话的聊天记录
        /// @param content 待发送的回复文本
        virtual drogon::Task<> sendReply(
          const QQMessage &message, const ChatRecordManager &chatRecords, std::string content) const = 0;

        /// @brief 发布消息域产生的领域事件
        /// @param event 待发布的事件
        virtual drogon::Task<> publish(DomainEvent event) const = 0;
    };

    /// @brief 创建使用当前进程服务的默认消息运行时
    /// @return 可安全交给 MessagePipeline 长期持有的运行时实例
    /// @details 该适配器集中保留对 AgentSystem、EventBus 与 MessageService 的既有访问方式，
    /// 使消息中间件不再感知全局单例。
    [[nodiscard]] std::shared_ptr<const MessageRuntime> createBuiltinMessageRuntime();
} // namespace insoulforge
