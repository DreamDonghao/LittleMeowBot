/// @file AgentSystem.cpp
/// @brief Agent 系统 - 实现（简化为2层架构）
/// @details Router → Executor
///          Router: 判断是否回复 + 规划策略
///          Executor: 执行回复

#include <agent/AgentSystem.hpp>
#include <agent/AgentToolManager.hpp>
#include <agent/ExecutorAgent.hpp>
#include <agent/RouterAgent.hpp>
#include <chrono>
#include <drogon/HttpAppFramework.h>
#include <service/PromptService.hpp>
#include <spdlog/spdlog.h>
#include <util/Logger.hpp>

namespace insoulforge {
    AgentSystem &AgentSystem::instance() {
        static AgentSystem system;
        return system;
    }

    bool AgentSystem::isRunning() const noexcept { return m_running.load(std::memory_order_acquire); }

    void AgentSystem::setRunning(const bool running) noexcept { m_running.store(running, std::memory_order_release); }

    void AgentSystem::initialize() {
        AgentToolManager::registerAllTools();
        AgentToolManager::registerCustomTools();
        PromptService::initialize();
        m_initialized = true;
    }

    drogon::Task<std::optional<std::string>> AgentSystem::process(
      const ChatRecordManager &chatRecords, const MemoryManager &memory, QQMessage message) {
        if (!isRunning()) {
            co_return std::nullopt;
        }

        const uint64_t sessionId = message.getSessionId();
        if (!m_initialized) {
            Logger::session(sessionId).error("AgentSystem 未初始化");
            co_return std::nullopt;
        }

        const bool isPriority = message.isPriorityMessage();
        auto generation = tryStartProcessing(sessionId, isPriority);
        if (generation == 0) {
            // 高优先级消息（@/私聊/系统定时任务）排队等待：在跑的是普通消息则打断，是优先消息则不打断。
            // 每轮重试都尝试打断，堵住旧处理退出后普通消息插队抢先的漏洞
            if (isPriority) {
                do {
                    cancelNonPriorityProcessing(sessionId);
                    co_await drogon::sleepCoro(drogon::app().getLoop(), std::chrono::milliseconds(50));
                    generation = tryStartProcessing(sessionId, isPriority);
                } while (generation == 0);
            } else {
                Logger::session(sessionId).debug("正在处理中，跳过");
                co_return std::nullopt;
            }
        }

        struct ProcessingGuard {
            AgentSystem *sys;
            uint64_t sid;
            ~ProcessingGuard() { sys->finishProcessing(sid); }
        } guard{.sys = this, .sid = sessionId};

        Logger::session(sessionId).info("======== 开始处理消息 ========");

        // ========== Layer 1: Router Agent（判断 + 规划）==========
        Logger::session(sessionId).info("[Router] 分析消息...");

        // 会话类型随决策传给 Executor（选择对应人设提示词）；route 按值接管 message，先取出标记
        const bool isPrivateChat = message.isPrivate();
        auto decision = co_await route(chatRecords, std::move(message));
        decision.isPrivate = isPrivateChat;

        Logger::session(sessionId).info("[Router] 结果: {} | shouldReply={} | maxLength={}", decision.action,
          decision.shouldReply, decision.maxLength);

        // 检查处理代际是否被 @消息取消
        if (!isCurrentGeneration(sessionId, generation)) {
            Logger::session(sessionId).info("[Router] 处理被新消息中断");
            co_return std::nullopt;
        }

        // Router 决定不回复
        if (!decision.shouldReply) {
            Logger::session(sessionId).info("[Router] 决定不回复: {}", decision.reason);
            co_return std::nullopt;
        }

        // ========== Layer 2: Executor Agent（执行回复）==========
        Logger::session(sessionId).info("[Executor] 执行回复...");

        const auto reply = co_await execute(chatRecords, memory, std::move(decision));

        // 检查处理代际是否被 @消息取消
        if (!isCurrentGeneration(sessionId, generation)) {
            Logger::session(sessionId).info("[Executor] 处理被新消息中断");
            co_return std::nullopt;
        }

        if (!reply || !reply->shouldReply || reply->content.empty()) {
            Logger::session(sessionId).error("[Executor] 执行失败或无回复");
            co_return std::nullopt;
        }

        Logger::session(sessionId).info("======== 处理完成 ========");
        co_return cleanReplyContent(reply->content);
    }

    uint64_t AgentSystem::tryStartProcessing(const uint64_t sessionId, const bool isPriority) {
        std::lock_guard lock(m_processingMutex);
        if (const auto it = m_processingSessions.find(sessionId); it != m_processingSessions.end()) {
            return 0; // 会话正在处理中
        }
        constexpr uint64_t gen = 1;
        m_processingSessions[sessionId] = {.generation = gen, .isPriority = isPriority};
        return gen;
    }

    void AgentSystem::cancelNonPriorityProcessing(const uint64_t sessionId) {
        std::lock_guard lock(m_processingMutex);
        if (const auto it = m_processingSessions.find(sessionId);
            it != m_processingSessions.end() && !it->second.isPriority) {
            ++it->second.generation; // 递增代际，通知当前处理者中断（优先消息在处理则不打断，调用方排队等待）
        }
    }

    bool AgentSystem::isCurrentGeneration(const uint64_t sessionId, const uint64_t generation) {
        std::lock_guard lock(m_processingMutex);
        const auto it = m_processingSessions.find(sessionId);
        return it != m_processingSessions.end() && it->second.generation == generation;
    }

    void AgentSystem::finishProcessing(const uint64_t sessionId) {
        std::lock_guard lock(m_processingMutex);
        m_processingSessions.erase(sessionId);
    }
} // namespace insoulforge
