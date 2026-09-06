/// @file MemoryMaintenanceMiddleware.cpp
/// @brief 记忆维护中间件实现

#include <exception>
#include <message/MessageContext.hpp>
#include <message/middleware/MemoryMaintenanceMiddleware.hpp>
#include <service/MemoryService.hpp>
#include <util/Logger.hpp>

namespace insoulforge {
    std::string_view MemoryMaintenanceMiddleware::id() const noexcept { return "memory_maintenance"; }

    drogon::Task<MessageFlow> MemoryMaintenanceMiddleware::handle(MessageContext &context) const {
        try {
            co_await MemoryService::appendAndMergeMemory(context.sessionId());
        } catch (const std::exception &error) {
            Logger::session(context.sessionId()).error("记忆提取异常: {}", error.what());
        }
        co_return MessageFlow::Continue;
    }
} // namespace insoulforge
