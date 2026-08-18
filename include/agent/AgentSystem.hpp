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
#include <unordered_set>
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

        // 正在处理中的群聊（防止同时处理多条消息）
        std::unordered_set<uint64_t> m_processingGroups;
        std::mutex m_processingMutex;

        bool isProcessing(uint64_t groupId);

        void markProcessing(uint64_t groupId);

        void unmarkProcessing(uint64_t groupId);
    };
}
