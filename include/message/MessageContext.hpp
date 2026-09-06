/// @file MessageContext.hpp
/// @brief 入站消息处理链路的共享上下文

#pragma once

#include <drogon/utils/coroutine.h>
#include <optional>
#include <string>

#include <model/QQMessage.hpp>
#include <service/ChatRecordManager.hpp>
#include <service/MemoryManager.hpp>

namespace insoulforge {
    /// @brief 在中间件之间传递一次入站消息的处理状态
    /// @details 聊天记录与短期记忆仅在实际需要时创建，避免被短路的事件访问数据库。
    class MessageContext {
    public:
        /// @brief 使用 OneBot 原始事件创建上下文
        /// @param event OneBot 原始事件或由中间件合成的消息事件
        explicit MessageContext(json event);

        /// @brief 获取当前可变事件
        /// @return 在消息模型创建前可由归一化中间件替换的事件 JSON
        [[nodiscard]] json &event();

        /// @brief 用归一化后的事件创建 QQ 消息模型
        /// @pre 事件已被归一化为 `post_type=message`。
        void createMessage();

        /// @brief 获取已创建的 QQ 消息模型
        /// @pre 已调用 createMessage()。
        /// @return 可修改的 QQ 消息模型
        [[nodiscard]] QQMessage &message();

        /// @brief 获取已创建的 QQ 消息模型
        /// @pre 已调用 createMessage()。
        /// @return 只读 QQ 消息模型
        [[nodiscard]] const QQMessage &message() const;

        /// @brief 获取统一会话标识
        /// @pre 已调用 createMessage()。
        /// @return 群号，或带私聊标志位的用户 QQ 号
        [[nodiscard]] uint64_t sessionId() const;

        /// @brief 获取用于异常日志关联的会话 ID
        /// @return 已创建消息时返回其会话 ID；否则从原始事件提取，无法识别时返回 0
        /// @details 可在 MessageSetupMiddleware 之前安全调用，不依赖 createMessage()。
        [[nodiscard]] uint64_t logSessionId() const;

        /// @brief 获取用于异常日志关联的消息 ID
        /// @return 已创建消息时返回其消息 ID；否则从原始事件提取，缺失时返回 0
        /// @details 可在 MessageSetupMiddleware 之前安全调用，不依赖 createMessage()。
        [[nodiscard]] uint64_t logMessageId() const;

        /// @brief 获取当前会话的聊天记录管理器，首次调用时构造
        /// @pre 已调用 createMessage()。
        /// @return 当前会话的聊天记录管理器
        [[nodiscard]] ChatRecordManager &chatRecords();

        /// @brief 获取当前会话的短期记忆管理器，首次调用时构造
        /// @pre 已调用 createMessage()。
        /// @return 当前会话的短期记忆管理器
        [[nodiscard]] MemoryManager &memory();

        /// @brief 向当前消息所在的群聊或私聊发送回复
        /// @param content 待发送的回复文本
        /// @pre 已调用 createMessage()。
        drogon::Task<> sendReply(std::string content);

    private:
        json m_event; ///< 归一化前或归一化后的 OneBot 事件
        std::optional<QQMessage> m_message; ///< MessageSetupMiddleware 创建的消息模型
        std::optional<ChatRecordManager> m_chatRecords; ///< 按需创建的聊天记录管理器
        std::optional<MemoryManager> m_memory; ///< 按需创建的短期记忆管理器
    };
} // namespace insoulforge
