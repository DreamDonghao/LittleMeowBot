/// @file AgentSystem.cpp
/// @brief Agent 系统 - 实现（简化为2层架构）
/// @details Router → Executor
///          Router: 判断是否回复 + 规划策略
///          Executor: 执行回复

#include <agent/AgentSystem.hpp>
#include <agent/ExecutorAgent.hpp>
#include <agent/RouterAgent.hpp>
#include <agent/AgentToolManager.hpp>
#include <util/Log.hpp>
#include <service/PromptService.hpp>

namespace LittleMeowBot {
    AgentSystem &AgentSystem::instance() {
        static AgentSystem system;
        return system;
    }

    void AgentSystem::initialize() {
        AgentToolManager::registerAllTools();
        AgentToolManager::registerCustomTools();
        PromptService::initialize();
        m_initialized = true;
    }

    drogon::Task<std::optional<std::string> > AgentSystem::process(
        const ChatRecordManager &chatRecords,
        const MemoryManager &memory,
        const QQMessage &message) {
        if (!m_initialized) {
            Log::error("AgentSystem 未初始化");
            co_return std::nullopt;
        }

        const uint64_t groupId = message.getGroupId();
        if (isProcessing(groupId)) {
            Log::debug("群 {} 正在处理中，跳过", groupId);
            co_return std::nullopt;
        }

        markProcessing(groupId);
        struct ProcessingGuard {
            AgentSystem *sys;
            uint64_t gid;
            ~ProcessingGuard() { sys->unmarkProcessing(gid); }
        } guard{.sys = this, .gid = groupId};

        Log::info("======== 开始处理消息 ========");

        // ========== Layer 1: Router Agent（判断 + 规划）==========
        Log::info("[Router] 分析消息...");

        const auto decision = co_await route(
            chatRecords, memory, message);

        Log::info("[Router] 结果: {} | shouldReply={} | thinking={} | maxLength={}",
                  decision.action, decision.shouldReply, decision.enableThinking, decision.maxLength);

        // Router 决定不回复
        if (!decision.shouldReply) {
            Log::info("[Router] 决定不回复: {}", decision.reason);
            co_return std::nullopt;
        }

        // ========== Layer 2: Executor Agent（执行回复）==========
        Log::info("[Executor] 执行回复...");

        const auto reply = co_await execute(chatRecords, memory, decision);

        if (!reply || !reply->shouldReply || reply->content.empty()) {
            Log::error("[Executor] 执行失败或无回复");
            co_return std::nullopt;
        }

        Log::info("======== 处理完成 ========");
        co_return cleanReplyContent(reply->content);
    }

    bool AgentSystem::isProcessing(const uint64_t groupId) {
        std::lock_guard lock(m_processingMutex);
        return m_processingGroups.contains(groupId);
    }

    void AgentSystem::markProcessing(const uint64_t groupId) {
        std::lock_guard lock(m_processingMutex);
        m_processingGroups.insert(groupId);
    }

    void AgentSystem::unmarkProcessing(const uint64_t groupId) {
        std::lock_guard lock(m_processingMutex);
        m_processingGroups.erase(groupId);
    }
}
