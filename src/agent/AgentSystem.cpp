/// @file AgentSystem.cpp
/// @brief Agent 系统 - 实现（简化为2层架构）
/// @details Router → Executor
///          Router: 判断是否回复 + 规划策略
///          Executor: 执行回复

#include <chrono>
#include <drogon/HttpAppFramework.h>
#include <spdlog/spdlog.h>
#include <agent/AgentSystem.hpp>
#include <agent/ExecutorAgent.hpp>
#include <agent/RouterAgent.hpp>
#include <agent/AgentToolManager.hpp>
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
            spdlog::error("AgentSystem 未初始化");
            co_return std::nullopt;
        }

        const uint64_t groupId = message.getGroupId();
        // 同一群的消息串行处理：非@消息在处理中时跳过（防止积压），
        // @消息则排队等待，确保不会被跳过
        while (!tryMarkProcessing(groupId)) {
            if (!message.atMe()) {
                spdlog::debug("群 {} 正在处理中，跳过", groupId);
                co_return std::nullopt;
            }
            co_await drogon::sleepCoro(drogon::app().getLoop(), std::chrono::milliseconds(50));
        }

        struct ProcessingGuard {
            AgentSystem *sys;
            uint64_t gid;
            ~ProcessingGuard() { sys->unmarkProcessing(gid); }
        } guard{.sys = this, .gid = groupId};

        spdlog::info("======== 群 {} 开始处理消息 ========", groupId);

        // ========== Layer 1: Router Agent（判断 + 规划）==========
        spdlog::info("[Router] 群 {} 分析消息...", groupId);

        const auto decision = co_await route(
            chatRecords, memory, message);

        spdlog::info("[Router] 群 {} 结果: {} | shouldReply={} | thinking={} | maxLength={}",
                  groupId, decision.action, decision.shouldReply, decision.enableThinking, decision.maxLength);

        // Router 决定不回复
        if (!decision.shouldReply) {
            spdlog::info("[Router] 群 {} 决定不回复: {}", groupId, decision.reason);
            co_return std::nullopt;
        }

        // ========== Layer 2: Executor Agent（执行回复）==========
        spdlog::info("[Executor] 群 {} 执行回复...", groupId);

        const auto reply = co_await execute(chatRecords, memory, decision);

        if (!reply || !reply->shouldReply || reply->content.empty()) {
            spdlog::error("[Executor] 群 {} 执行失败或无回复", groupId);
            co_return std::nullopt;
        }

        spdlog::info("======== 群 {} 处理完成 ========", groupId);
        co_return cleanReplyContent(reply->content);
    }

    bool AgentSystem::tryMarkProcessing(const uint64_t groupId) {
        std::lock_guard lock(m_processingMutex);
        if (m_processingGroups.contains(groupId)) {
            return false;
        }
        m_processingGroups.insert(groupId);
        return true;
    }

    void AgentSystem::unmarkProcessing(const uint64_t groupId) {
        std::lock_guard lock(m_processingMutex);
        m_processingGroups.erase(groupId);
    }
}
