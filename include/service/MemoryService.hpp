/// @file MemoryService.hpp
/// @brief 记忆服务 - 短期记忆提取、窗口滑动与迁移
/// @author donghao
/// @date 2026-04-02
/// @details 提供记忆系统的核心功能：
///          - 上下文窗口超限时从将被滑出的记录提取短期记忆（提取+合并一次LLM调用完成）
///          - 逐批推进水位线（失败自愈、崩溃安全）
///          - 超限时迁移到长期记忆库（RAGFlow）

#pragma once
#include <api/ApiClient.hpp>


/// @brief 记忆服务 - 短期记忆提取、合并与迁移逻辑
namespace insoulforge::MemoryService {
    /// @brief 窗口超限时提取记忆并滑动窗口（完整流程：提取+合并 → 原子写记忆+水位线 → 超限迁移）
    /// @param sessionId 会话 ID（私聊会话带标志位）
    /// @details 窗口条数超过 windowTriggerCount 时触发，删除至 windowKeepCount 条。
    ///          API 失败时水位线不推进，下条消息自动重试；判定"无"时正常推进水位线。
    drogon::Task<> appendAndMergeMemory(uint64_t sessionId);
}
