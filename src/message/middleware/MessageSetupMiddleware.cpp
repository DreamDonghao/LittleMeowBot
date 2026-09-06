/// @file MessageSetupMiddleware.cpp
/// @brief 消息模型与会话配置初始化中间件实现

#include <message/MessageContext.hpp>
#include <message/middleware/MessageSetupMiddleware.hpp>
#include <service/SessionConfigManager.hpp>

namespace insoulforge {
    std::string_view MessageSetupMiddleware::id() const noexcept { return "message_setup"; }

    drogon::Task<MessageFlow> MessageSetupMiddleware::handle(MessageContext &context) const {
        context.createMessage();
        if (!SessionConfigManager::contains(context.sessionId())) {
            SessionConfigManager::addConfig(context.sessionId());
        }
        co_return MessageFlow::Continue;
    }
} // namespace insoulforge
