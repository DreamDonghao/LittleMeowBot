/// @file MessagePipeline.cpp
/// @brief 入站消息处理链路实现

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

#include <message/MessageContext.hpp>
#include <message/MessageMiddleware.hpp>
#include <message/MessageMiddlewareCatalog.hpp>
#include <message/MessagePipeline.hpp>

namespace insoulforge {
    MessagePipeline &MessagePipeline::instance() {
        static MessagePipeline pipeline;
        return pipeline;
    }

    void MessagePipeline::initialize() {
        if (m_initialized) {
            return;
        }

        // 先在局部容器中完成全部校验，校验失败时不污染正在使用的链路。
        auto middlewares = MessageMiddlewareCatalog::createBuiltinMiddlewares();
        std::unordered_set<std::string_view> ids;
        for (const auto &middleware: middlewares) {
            if (!middleware || middleware->id().empty() || !ids.insert(middleware->id()).second) {
                throw std::invalid_argument("内置消息中间件无效或标识重复");
            }
        }
        m_middlewares = std::move(middlewares);
        m_initialized = true;
    }

    void MessagePipeline::addMiddleware(std::unique_ptr<MessageMiddleware> middleware) {
        if (!m_initialized) {
            throw std::logic_error("必须先初始化内置消息中间件");
        }
        if (!middleware || middleware->id().empty()) {
            throw std::invalid_argument("消息中间件及其标识不能为空");
        }
        if (std::any_of(m_middlewares.begin(), m_middlewares.end(), [&middleware](const auto &existing) {
                return existing->id() == middleware->id();
            })) {
            throw std::invalid_argument("消息中间件标识重复");
        }
        m_middlewares.push_back(std::move(middleware));
    }

    drogon::Task<> MessagePipeline::process(json event) const {
        if (!m_initialized) {
            throw std::logic_error("消息处理链路尚未初始化");
        }
        MessageContext context(std::move(event));
        for (const auto &middleware: m_middlewares) {
            // Stop 是正常的短路结果，例如非消息事件、命令或禁用会话。
            if (co_await middleware->handle(context) == MessageFlow::Stop) {
                co_return;
            }
        }
    }
} // namespace insoulforge
