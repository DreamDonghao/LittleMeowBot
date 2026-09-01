/// @file LlmClient.hpp
/// @brief API 客户端 - LLM API 请求封装
/// @date 2026-04-02
/// @details 封装 LLM API 请求与用量统计：
///          - LLM 请求：requestLLM()
///          - Embedding 请求：requestEmbedding()
///          - 缓存命中率日志：logUsage()

#pragma once
#include <drogon/utils/coroutine.h>
#include <json/value.h>
#include <optional>
#include <string>
#include <vector>

/// @brief API 客户端 - 封装 LLM API 请求与用量统计
namespace insoulforge::LlmClient {
    /// @brief 请求 LLM API（使用 Executor 配置）
    /// @param messages 消息列表
    /// @param temperature 温度参数
    /// @param top_p Top-P 采样参数
    /// @param max_tokens 最大 token 数
    /// @param role
    /// @param sessionId
    /// @return 响应文本，失败返回 std::nullopt
    drogon::Task<std::optional<std::string>> requestLLM(const Json::Value &messages, double temperature = 1.35,
      double top_p = 0.92, int max_tokens = 1024, const std::string &role = "memory",
      std::optional<uint64_t> sessionId = std::nullopt);

    /// @brief 从 API 响应中提取 usage 信息并输出缓存命中率日志
    /// @param responseJson API 返回的完整 JSON
    /// @param model 模型名
    /// @param role 角色名（router/executor/executorThinking/memory/image/embedding）
    /// @param sessionId
    void logUsage(const Json::Value &responseJson, const std::string &model, const std::string &role,
      std::optional<uint64_t> sessionId = std::nullopt);

    /// @brief 请求 Embedding API（使用 Embedding 配置）
    /// @param text 待向量化的文本
    /// @param sessionId
    /// @return 向量，未配置或失败返回 std::nullopt
    drogon::Task<std::optional<std::vector<float>>> requestEmbedding(
      const std::string &text, std::optional<uint64_t> sessionId = std::nullopt);
} // namespace insoulforge::LlmClient
