/// @file MessagePipeline.cpp
/// @brief 入站消息处理链路实现

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

#include <message/MessageContext.hpp>
#include <message/MessageMiddleware.hpp>
#include <message/MessageMiddlewareCatalog.hpp>
#include <message/MessagePipeline.hpp>
#include <util/Logger.hpp>

namespace insoulforge {
    MessagePipeline &MessagePipeline::instance() {
        static MessagePipeline pipeline;
        return pipeline;
    }

    void MessagePipeline::initialize() {
        initialize(MessageMiddlewareCatalog::createBuiltinMiddlewares());
    }

    void MessagePipeline::initialize(std::vector<std::unique_ptr<MessageMiddleware>> middlewares) {
        if (m_initialized) {
            return;
        }

        // 先在局部容器中完成全部校验，校验失败时不污染正在使用的链路。
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
        ensureInitialized();
        validateMiddleware(middleware);
        m_middlewares.push_back(std::move(middleware));
    }

    void MessagePipeline::insertBefore(const std::string_view anchorId, std::unique_ptr<MessageMiddleware> middleware) {
        ensureInitialized();
        validateMiddleware(middleware);
        const size_t index = findMiddlewareIndex(anchorId);
        m_middlewares.insert(m_middlewares.begin() + static_cast<std::ptrdiff_t>(index), std::move(middleware));
    }

    void MessagePipeline::insertAfter(const std::string_view anchorId, std::unique_ptr<MessageMiddleware> middleware) {
        ensureInitialized();
        validateMiddleware(middleware);
        const size_t index = findMiddlewareIndex(anchorId);
        m_middlewares.insert(m_middlewares.begin() + static_cast<std::ptrdiff_t>(index + 1), std::move(middleware));
    }

    void MessagePipeline::ensureInitialized() const {
        if (!m_initialized) {
            throw std::logic_error("必须先初始化内置消息中间件");
        }
    }

    void MessagePipeline::validateMiddleware(const std::unique_ptr<MessageMiddleware> &middleware) const {
        if (!middleware || middleware->id().empty()) {
            throw std::invalid_argument("消息中间件及其标识不能为空");
        }
        if (std::any_of(m_middlewares.begin(), m_middlewares.end(), [&middleware](const auto &existing) {
                return existing->id() == middleware->id();
            })) {
            throw std::invalid_argument("消息中间件标识重复");
        }
    }

    size_t MessagePipeline::findMiddlewareIndex(const std::string_view anchorId) const {
        if (anchorId.empty()) {
            throw std::invalid_argument("中间件锚点标识不能为空");
        }
        const auto it = std::find_if(m_middlewares.begin(), m_middlewares.end(), [anchorId](const auto &middleware) {
            return middleware->id() == anchorId;
        });
        if (it == m_middlewares.end()) {
            throw std::out_of_range("未找到中间件锚点");
        }
        return static_cast<size_t>(it - m_middlewares.begin());
    }

    drogon::Task<> MessagePipeline::process(json event) const {
        ensureInitialized();
        MessageContext context(std::move(event));
        for (const auto &middleware: m_middlewares) {
            try {
                // Stop 是正常的短路结果，例如非消息事件、命令或禁用会话。
                if (co_await middleware->handle(context) == MessageFlow::Stop) {
                    co_return;
                }
            } catch (const std::exception &error) {
                Logger::session(context.logSessionId())
                  .error("[MessagePipeline] 节点 {} 处理失败: message_id={}, error={}", middleware->id(),
                    context.logMessageId(), error.what());
                co_return;
            } catch (...) {
                Logger::session(context.logSessionId())
                  .error("[MessagePipeline] 节点 {} 处理失败: message_id={}, 未知异常", middleware->id(),
                    context.logMessageId());
                co_return;
            }
        }
    }
} // namespace insoulforge
