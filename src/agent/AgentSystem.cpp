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
#include <util/Logger.hpp>

namespace LittleMeowBot {
    AgentSystem &AgentSystem::instance() {
        static AgentSystem system;
        return system;
    }

    bool AgentSystem::isRunning() const noexcept {
        return m_running.load(std::memory_order_acquire);
    }

    void AgentSystem::setRunning(const bool running) noexcept {
        m_running.store(running, std::memory_order_release);
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
        if (!isRunning()) {
            co_return std::nullopt;
        }

        const uint64_t groupId = message.getGroupId();
        if (!m_initialized) {
            Logger::group(groupId).error("AgentSystem 未初始化");
            co_return std::nullopt;
        }

        auto generation = tryStartProcessing(groupId);
        if (generation == 0) {
            // @消息：取消当前非@消息的处理，排队等待
            if (message.atMe()) {
                cancelProcessing(groupId);
                do {
                    co_await drogon::sleepCoro(drogon::app().getLoop(), std::chrono::milliseconds(50));
                    generation = tryStartProcessing(groupId);
                } while (generation == 0);
            } else {
                Logger::group(groupId).debug("正在处理中，跳过");
                co_return std::nullopt;
            }
        }

        struct ProcessingGuard {
            AgentSystem *sys;
            uint64_t gid;
            ~ProcessingGuard() { sys->finishProcessing(gid); }
        } guard{.sys = this, .gid = groupId};

        Logger::group(groupId).info("======== 开始处理消息 ========");

        // ========== Layer 1: Router Agent（判断 + 规划）==========
        Logger::group(groupId).info("[Router] 分析消息...");

        const auto decision = co_await route(
            chatRecords, memory, message);

        Logger::group(groupId).info("[Router] 结果: {} | shouldReply={} | thinking={} | maxLength={}",
                                     decision.action, decision.shouldReply, decision.enableThinking, decision.maxLength);

        // 检查处理代际是否被 @消息取消
        if (!isCurrentGeneration(groupId, generation)) {
            Logger::group(groupId).info("[Router] 处理被 @消息中断");
            co_return std::nullopt;
        }

        // Router 决定不回复
        if (!decision.shouldReply) {
            Logger::group(groupId).info("[Router] 决定不回复: {}", decision.reason);
            co_return std::nullopt;
        }

        // ========== Layer 2: Executor Agent（执行回复）==========
        Logger::group(groupId).info("[Executor] 执行回复...");

        const auto reply = co_await execute(chatRecords, memory, decision);

        // 检查处理代际是否被 @消息取消
        if (!isCurrentGeneration(groupId, generation)) {
            Logger::group(groupId).info("[Executor] 处理被 @消息中断");
            co_return std::nullopt;
        }

        if (!reply || !reply->shouldReply || reply->content.empty()) {
            Logger::group(groupId).error("[Executor] 执行失败或无回复");
            co_return std::nullopt;
        }

        Logger::group(groupId).info("======== 处理完成 ========");
        co_return cleanReplyContent(reply->content);
    }

    uint64_t AgentSystem::tryStartProcessing(const uint64_t groupId) {
        std::lock_guard lock(m_processingMutex);
        auto it = m_processingGroups.find(groupId);
        if (it != m_processingGroups.end()) {
            return 0; // 群正在处理中
        }
        const uint64_t gen = 1;
        m_processingGroups[groupId] = gen;
        return gen;
    }

    void AgentSystem::cancelProcessing(const uint64_t groupId) {
        std::lock_guard lock(m_processingMutex);
        auto it = m_processingGroups.find(groupId);
        if (it != m_processingGroups.end()) {
            ++it->second; // 递增代际，通知当前处理者中断
        }
    }

    bool AgentSystem::isCurrentGeneration(const uint64_t groupId, const uint64_t generation) {
        std::lock_guard lock(m_processingMutex);
        auto it = m_processingGroups.find(groupId);
        return it != m_processingGroups.end() && it->second == generation;
    }

    void AgentSystem::finishProcessing(const uint64_t groupId) {
        std::lock_guard lock(m_processingMutex);
        m_processingGroups.erase(groupId);
    }
}
