/// @file SessionEnabledMiddleware.cpp
/// @brief 会话启用状态检查中间件实现

#include <message/MessageContext.hpp>
#include <message/middleware/SessionEnabledMiddleware.hpp>
#include <storage/SessionStore.hpp>

namespace insoulforge {
    std::string_view SessionEnabledMiddleware::id() const noexcept { return "session_enabled"; }

    drogon::Task<MessageFlow> SessionEnabledMiddleware::handle(MessageContext &context) const {
        co_return SessionStore::isSessionEnabled(context.sessionId()) ? MessageFlow::Continue : MessageFlow::Stop;
    }
} // namespace insoulforge
