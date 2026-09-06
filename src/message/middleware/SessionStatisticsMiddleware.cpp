/// @file SessionStatisticsMiddleware.cpp
/// @brief 会话统计更新中间件实现

#include <message/MessageContext.hpp>
#include <message/middleware/SessionStatisticsMiddleware.hpp>
#include <service/SessionConfigManager.hpp>
#include <util/Logger.hpp>

namespace insoulforge {
    std::string_view SessionStatisticsMiddleware::id() const noexcept { return "session_statistics"; }

    drogon::Task<MessageFlow> SessionStatisticsMiddleware::handle(MessageContext &context) const {
        SessionConfigManager::incrementMessageCount(context.sessionId(), context.message().getFormatMessage().size());
        const auto [messageCount, characterCount] = SessionConfigManager::getConfig(context.sessionId());
        Logger::session(context.sessionId())
          .info("会话统计数据: 接收总消息数{}条,接收总字符(字节)数{}个", messageCount, characterCount);
        co_return MessageFlow::Continue;
    }
} // namespace insoulforge
