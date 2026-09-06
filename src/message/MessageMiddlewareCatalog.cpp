/// @file MessageMiddlewareCatalog.cpp
/// @brief 内置入站消息中间件注册

#include <message/MessageMiddlewareCatalog.hpp>
#include <message/middleware/AgentAvailabilityMiddleware.hpp>
#include <message/middleware/AgentReplyMiddleware.hpp>
#include <message/middleware/CommandMessageMiddleware.hpp>
#include <message/middleware/EventNormalizationMiddleware.hpp>
#include <message/middleware/FormatMessageMiddleware.hpp>
#include <message/middleware/MemoryMaintenanceMiddleware.hpp>
#include <message/middleware/MessageSetupMiddleware.hpp>
#include <message/middleware/RecordMessageMiddleware.hpp>
#include <message/middleware/SessionEnabledMiddleware.hpp>
#include <message/middleware/SessionStatisticsMiddleware.hpp>

namespace insoulforge {
    std::vector<std::unique_ptr<MessageMiddleware>> MessageMiddlewareCatalog::createBuiltinMiddlewares() {
        std::vector<std::unique_ptr<MessageMiddleware>> middlewares;
        // 顺序是行为契约：命令必须先于会话开关，记录必须先于 Agent，维护必须位于末尾。
        middlewares.emplace_back(std::make_unique<EventNormalizationMiddleware>());
        middlewares.emplace_back(std::make_unique<AgentAvailabilityMiddleware>());
        middlewares.emplace_back(std::make_unique<MessageSetupMiddleware>());
        middlewares.emplace_back(std::make_unique<CommandMessageMiddleware>());
        middlewares.emplace_back(std::make_unique<SessionEnabledMiddleware>());
        middlewares.emplace_back(std::make_unique<FormatMessageMiddleware>());
        middlewares.emplace_back(std::make_unique<RecordMessageMiddleware>());
        middlewares.emplace_back(std::make_unique<AgentReplyMiddleware>());
        middlewares.emplace_back(std::make_unique<SessionStatisticsMiddleware>());
        middlewares.emplace_back(std::make_unique<MemoryMaintenanceMiddleware>());
        return middlewares;
    }
} // namespace insoulforge
