/// @file MessageMiddlewareCatalog.hpp
/// @brief 内置消息中间件的显式组合

#pragma once

#include <memory>
#include <vector>

namespace insoulforge {
    class MessageMiddleware;

    namespace MessageMiddlewareCatalog {
        /// @brief 按处理顺序创建全部内置中间件
        /// @return 已完成构造但尚未执行的中间件列表
        /// @details 返回顺序即 MessagePipeline 的执行顺序，不应在调用方重新排序。
        [[nodiscard]] std::vector<std::unique_ptr<MessageMiddleware>> createBuiltinMiddlewares();
    } // namespace MessageMiddlewareCatalog
} // namespace insoulforge
