/// @file MemoryService.hpp
/// @brief 记忆服务 - 短期记忆提取、召回归类与窗口滑动
/// @author donghao
/// @date 2026-04-02
/// @details 提供记忆系统的核心功能：
///          - 窗口超限时从滑出的记录提取新记忆（严格 JSON 契约）
///          - 以新记忆召回相似长期记忆，与短期记忆一并归类为短期/长期两部分
///          - 归类成功后原子写短期记忆+水位线；长期记忆先插新条目、后删被取代的召回条目
///          - 逐批推进水位线（失败自愈、崩溃安全）

#pragma once
#include <service/LlmClient.hpp>

/// @brief 记忆服务 - 短期记忆提取、合并与迁移逻辑
namespace insoulforge::MemoryService {
    /// @brief 窗口超限时提取记忆并滑动窗口（完整流程：提取 → 召回 → 归类 → 写入）
    /// @param sessionId 会话 ID（私聊会话带标志位）
    /// @details 窗口条数超过 windowTriggerCount 时触发，删除至 windowKeepCount 条。
    ///          提取或归类失败时水位线不推进，下条消息自动重试；提取为空时正常推进水位线。
    drogon::Task<> appendAndMergeMemory(uint64_t sessionId);
} // namespace insoulforge::MemoryService
