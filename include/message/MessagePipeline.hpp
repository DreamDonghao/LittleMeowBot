/// @file MessagePipeline.hpp
/// @brief 入站 OneBot 消息处理链路

#pragma once

#include <cstddef>
#include <drogon/utils/coroutine.h>
#include <memory>
#include <string_view>
#include <vector>

#include <util/JsonUtil.hpp>

namespace insoulforge {
    class MessageMiddleware;

    /// @brief 按固定顺序执行入站消息中间件
    /// @details 初始化后可通过 addMiddleware 扩展链路；内置链路由 MessageMiddlewareCatalog 显式注册。
    class MessagePipeline {
    public:
        /// @brief 获取进程内唯一的消息处理链路
        /// @return 单例 MessagePipeline
        [[nodiscard]] static MessagePipeline &instance();

        /// @brief 注册内置中间件；重复调用无副作用
        /// @throws std::invalid_argument 内置节点为空、标识为空或标识重复时抛出
        /// @pre 必须在 HTTP 服务开始接收请求前调用。
        void initialize();

        /// @brief 使用指定中间件列表初始化处理链路
        /// @param middlewares 按执行顺序排列的中间件列表
        /// @throws std::invalid_argument 中间件为空、标识为空或标识重复时抛出
        /// @pre 必须在 HTTP 服务开始接收请求前调用。
        /// @details 用于自定义组合和契约测试；重复调用无副作用。
        void initialize(std::vector<std::unique_ptr<MessageMiddleware>> middlewares);

        /// @brief 添加一个自定义中间件
        /// @throws std::invalid_argument 中间件为空、标识为空或与已有节点重名时抛出
        /// @throws std::logic_error 内置中间件尚未初始化时抛出
        /// @pre 必须在 initialize() 完成后、开始接收消息前调用。
        void addMiddleware(std::unique_ptr<MessageMiddleware> middleware);

        /// @brief 在指定节点之前插入一个自定义中间件
        /// @param anchorId 作为插入位置的已有中间件标识
        /// @param middleware 待插入的中间件，调用后所有权转交给 Pipeline
        /// @throws std::invalid_argument 中间件或标识无效、或标识与已有节点重复时抛出
        /// @throws std::logic_error 内置中间件尚未初始化时抛出
        /// @throws std::out_of_range 找不到 anchorId 时抛出
        /// @pre 必须在 initialize() 完成后、开始接收消息前调用。
        void insertBefore(std::string_view anchorId, std::unique_ptr<MessageMiddleware> middleware);

        /// @brief 在指定节点之后插入一个自定义中间件
        /// @param anchorId 作为插入位置的已有中间件标识
        /// @param middleware 待插入的中间件，调用后所有权转交给 Pipeline
        /// @throws std::invalid_argument 中间件或标识无效、或标识与已有节点重复时抛出
        /// @throws std::logic_error 内置中间件尚未初始化时抛出
        /// @throws std::out_of_range 找不到 anchorId 时抛出
        /// @pre 必须在 initialize() 完成后、开始接收消息前调用。
        void insertAfter(std::string_view anchorId, std::unique_ptr<MessageMiddleware> middleware);

        /// @brief 处理已经通过 HTTP 校验的 OneBot 事件
        /// @param event OneBot 上报的对象 JSON
        /// @throws std::logic_error 未完成 initialize() 时抛出
        /// @details 中间件依次执行，首个返回 MessageFlow::Stop 的节点终止本次处理。
        drogon::Task<> process(json event) const;

    private:
        /// @brief 确保内置中间件已注册
        /// @throws std::logic_error 初始化尚未完成时抛出
        void ensureInitialized() const;

        /// @brief 校验一个待注册的中间件
        /// @param middleware 待校验的中间件
        /// @throws std::invalid_argument 中间件为空、标识为空或标识重复时抛出
        void validateMiddleware(const std::unique_ptr<MessageMiddleware> &middleware) const;

        /// @brief 查找锚点中间件的位置
        /// @param anchorId 目标中间件标识
        /// @return 锚点在执行链路中的下标
        /// @throws std::invalid_argument anchorId 为空时抛出
        /// @throws std::out_of_range 未找到锚点时抛出
        [[nodiscard]] size_t findMiddlewareIndex(std::string_view anchorId) const;

        std::vector<std::unique_ptr<MessageMiddleware>> m_middlewares; ///< 固定执行顺序的中间件列表
        bool m_initialized{false}; ///< 内置节点是否已通过校验并完整注册
    };
} // namespace insoulforge
