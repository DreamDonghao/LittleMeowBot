/// @file AgentSystem.hpp
/// @brief Agent 系统 - 两层代理架构
/// @details 协调两层代理流程处理 QQ 消息：
///          - Layer 1 (Router): 判断是否回复 + 规划策略
///          - Layer 2 (Executor): 执行回复生成
///
///          流程：
///          - Router SKIP → 不回复
///          - Router REPLY → Executor 生成回复

#pragma once
#include <model/ChatRecordManager.hpp>
#include <model/MemoryManager.hpp>
#include <model/QQMessage.hpp>
#include <drogon/utils/coroutine.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <mutex>

namespace LittleMeowBot {
    /// @brief Agent 系统单例类
    /// @details 协调两层代理流程，提供统一的消息处理接口
    class AgentSystem {
    public:
        static AgentSystem &instance();

        /// @brief 初始化 Agent System（注册工具）
        void initialize();

        /// @brief 处理消息 - 主流程
        /// @param chatRecords 聊天记录管理器
        /// @param memory 长期记忆管理器
        /// @param message QQ 消息
        /// @return 回复内容（如果需要回复）
        drogon::Task<std::optional<std::string> > process(
            const ChatRecordManager &chatRecords,
            const MemoryManager &memory,
            const QQMessage &message);

    private:
        AgentSystem() = default;

        bool m_initialized = false;

        // 正在处理中的群聊（groupId → generation），用于防止并发处理
        // @消息到达时递增代际，非@消息在关键点检查代际是否被取消
        std::unordered_map<uint64_t, uint64_t> m_processingGroups;
        std::mutex m_processingMutex;

        /// @brief 尝试开始处理群消息
        /// @return 代际号（>0 成功），0 表示群正在处理中
        uint64_t tryStartProcessing(uint64_t groupId);

        /// @brief 取消群当前处理（@消息到达时调用，递增代际通知当前处理者中断）
        void cancelProcessing(uint64_t groupId);

        /// @brief 检查代际是否仍然有效
        bool isCurrentGeneration(uint64_t groupId, uint64_t generation);

        /// @brief 完成处理，移除群标记
        void finishProcessing(uint64_t groupId);
    };
}
