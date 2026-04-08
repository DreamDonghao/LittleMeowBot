/// @file RAGFlowClient.hpp
/// @brief RAGFlow 知识库检索客户端
/// @author donghao
/// @date 2026-04-02
/// @details 提供 RAGFlow 知识库和记忆库的检索功能：
///          - 知识库检索：searchKnowledge()
///          - 记忆库检索：searchMemory()
///          - 添加记忆：addMemory()

#pragma once

#include <drogon/HttpClient.h>
#include <drogon/utils/coroutine.h>
#include <json/value.h>
#include <string>
#include <optional>

namespace LittleMeowBot {
    /// @brief RAGFlow 知识库检索客户端（单例模式）
    /// @details 封装 RAGFlow API 调用，支持知识检索和记忆存储
    class RAGFlowClient{
    public:
        /// @brief 获取单例实例
        /// @return RAGFlowClient 实例引用
        static RAGFlowClient& instance();

        /// @brief 检索知识库
        /// @param question 查询问题
        /// @param topK 返回结果数量，默认3
        /// @return 检索结果文本，失败返回 std::nullopt
        drogon::Task<std::optional<std::string>> searchKnowledge(
            const std::string& question,
            int topK = 3) const;

        /// @brief 检索记忆库（长期记忆检索）
        /// @param question 查询问题
        /// @param topK 返回结果数量，默认3
        /// @return 检索结果文本，失败返回 std::nullopt
        drogon::Task<std::optional<std::string>> searchMemory(
            const std::string& question,
            int topK = 3) const;

        /// @brief 添加记忆到记忆库
        /// @param content 记忆内容文本
        /// @return 是否成功
        drogon::Task<bool> addMemory(const std::string& content) const;

    private:
        RAGFlowClient() = default;

        /// @brief 解析检索结果
        /// @param json RAGFlow API 返回的 JSON
        /// @return 格式化后的检索结果文本
        [[nodiscard]] static std::string parseSearchResult(const Json::Value& json);
    };
}