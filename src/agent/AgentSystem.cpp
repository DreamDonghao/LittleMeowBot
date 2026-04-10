/// @file AgentSystem.cpp
/// @brief Agent 系统 - 实现
/// @author donghao
/// @date 2026-04-02

#include <agent/AgentSystem.hpp>
#include <agent/ExecutorAgent.hpp>
#include <agent/PlannerAgent.hpp>
#include <agent/RouterAgent.hpp>
#include <util/Log.hpp>
#include <service/PromptService.hpp>

namespace LittleMeowBot {
    AgentSystem& AgentSystem::instance(){
        static AgentSystem system;
        return system;
    }

    void AgentSystem::initialize(){
        // 注册所有内置工具
        AgentToolManager::instance().registerAllTools();

        // 注册自定义工具
        AgentToolManager::instance().registerCustomTools();

        // 初始化提示词服务（从数据库加载默认提示词）
        PromptService::instance().initialize();

        m_initialized = true;
    }

    drogon::Task<std::optional<std::string>> AgentSystem::process(
        const ChatRecordManager& chatRecords,
        const MemoryManager& memory,
        const QQMessage& message){
        if (!m_initialized) {
            Log::error("AgentSystem 未初始化，请先调用 initialize()");
            co_return std::nullopt;
        }

        // 检查是否正在处理该群的消息
        uint64_t groupId = message.getGroupId();
        if (isProcessing(groupId)) {
            Log::debug("群 {} 正在处理中，跳过本次回复", groupId);
            co_return std::nullopt;
        }

        // 标记为处理中
        markProcessing(groupId);

        // 使用 RAII 确保处理完成后释放锁
        struct ProcessingGuard{
            AgentSystem* sys;
            uint64_t gid;
            ~ProcessingGuard(){ sys->unmarkProcessing(gid); }
        } guard{this, groupId};

        const auto& router = RouterAgent::instance();
        const auto& planner = PlannerAgent::instance();
        const auto& executor = ExecutorAgent::instance();

        Log::info("======== 开始处理消息 ========");

        // ========== Layer 1: Router Agent ==========
        Log::info("[Router] 分析消息意图...");
        auto routerDecision = co_await router.route(chatRecords, message);

        Log::info("[Router] 决策: {} ({})", routerDecision.action, routerDecision.reason);

        // Router SKIP → 直接结束
        if (routerDecision.action == RouterDecision::Action::SKIP) {
            Log::info("[Router] 决定跳过回复");
            co_return std::nullopt;
        }

        // ========== Layer 2: Planner Agent ==========
        PlanResult plan;

        Log::info("[Planner] 规划回复策略...");

        if (const auto planResult =
            co_await planner.plan(chatRecords, memory, routerDecision); !planResult) {
            Log::error("[Planner] 规划失败，使用默认策略");
            // 使用默认计划
            plan.intent = routerDecision.action == RouterDecision::Action::PRIORITY_REPLY
                          ? PlanResult::Intent::QUESTION : PlanResult::Intent::CHAT;
            plan.strategy.shouldReply = true;
            plan.strategy.maxLength = routerDecision.action == RouterDecision::Action::PRIORITY_REPLY ? 100 : 25;
            plan.strategy.tone = "friendly";
            plan.strategy.reason = "Planner失败，使用默认策略";
        } else {
            plan = planResult.value();
        }

        Log::info("[Planner] 规划完成: intent={}, shouldReply={}, enableThinking={}",
                 plan.intent, plan.strategy.shouldReply, plan.strategy.enableThinking);

        // Planner 决定不回复 → 结束
        if (!plan.strategy.shouldReply) {
            Log::info("[Planner] 决定不回复");
            co_return std::nullopt;
        }

        // ========== Layer 3: Executor Agent ==========
        Log::info("[Executor] 执行回复...");

        const bool isPriority = routerDecision.action == RouterDecision::Action::PRIORITY_REPLY;
        auto replyDecision
            = co_await executor.execute(chatRecords, memory, plan, isPriority);

        if (!replyDecision) {
            Log::error("[Executor] 执行失败");
            co_return std::nullopt;
        }

        Log::info("======== 处理完成 ========");

        if (replyDecision->shouldReply && !replyDecision->content.empty()) {
            co_return replyDecision->content;
        }

        co_return std::nullopt;
    }

    drogon::Task<std::optional<std::string>> AgentSystem::processAtMention(
        const ChatRecordManager& chatRecords,
        const MemoryManager& memory) const{
        auto& executor = ExecutorAgent::instance();

        Log::info("[@提及] 直接回复模式");

        if (auto replyDecision =
                co_await executor.directReply(chatRecords, memory);
            replyDecision && replyDecision->shouldReply && !replyDecision->content.empty()) {
            co_return replyDecision->content;
        }

        co_return std::nullopt;
    }

    bool AgentSystem::isProcessing(uint64_t groupId){
        std::lock_guard lock(m_processingMutex);
        return m_processingGroups.contains(groupId);
    }

    void AgentSystem::markProcessing(uint64_t groupId){
        std::lock_guard lock(m_processingMutex);
        m_processingGroups.insert(groupId);
    }

    void AgentSystem::unmarkProcessing(uint64_t groupId){
        std::lock_guard lock(m_processingMutex);
        m_processingGroups.erase(groupId);
    }
} // namespace LittleMeowBot
