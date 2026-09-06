/// @file MessageMiddleware.hpp
/// @brief 入站消息处理中间件接口

#pragma once

#include <drogon/utils/coroutine.h>
#include <string_view>

namespace insoulforge {
    class MessageContext;

    /// @brief 中间件执行后的链路控制结果
    enum class MessageFlow {
        Continue, ///< 继续执行下一个中间件
        Stop, ///< 当前事件处理完毕，跳过后续中间件
    };

    /// @brief 入站消息处理的单个阶段
    /// @details 中间件应只处理一个职责；返回 Stop 可安全终止后续阶段。
    class MessageMiddleware {
    public:
        /// @brief 虚析构，支持通过接口指针释放具体中间件
        virtual ~MessageMiddleware() = default;

        /// @brief 获取全局唯一的中间件标识
        /// @return 不为空的稳定标识；MessagePipeline 用其校验重复注册
        [[nodiscard]] virtual std::string_view id() const noexcept = 0;

        /// @brief 执行当前处理阶段
        /// @param context 本次入站消息的共享上下文
        /// @return Continue 继续链路，Stop 终止后续处理
        virtual drogon::Task<MessageFlow> handle(MessageContext &context) const = 0;
    };
} // namespace insoulforge
