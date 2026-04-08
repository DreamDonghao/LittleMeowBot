/// @file ApiClient.hpp
/// @brief API 客户端 - LLM API 请求封装
/// @author donghao
/// @date 2026-04-02
/// @details 封装所有 LLM API 请求功能：
///          - 天气查询：fetchWeather()
///          - 网络搜索：searchWeb()
///          - LLM 请求：requestLLM(), requestAgent()
///          - 工具调用：requestWithTools(), requestReplyAgent()

#pragma once
#include <service/ToolRegistry.hpp>
#include <agent/AgentTypes.hpp>
#include <drogon/HttpAppFramework.h>
#include <drogon/utils/coroutine.h>
#include <json/value.h>
#include <optional>
#include <string>
#include <vector>

namespace LittleMeowBot {
    /// @brief 工具调用结果
    struct ToolCall{
        std::string id;        ///< 工具调用ID
        std::string name;      ///< 工具名
        std::string arguments; ///< 参数JSON字符串
    };

    /// @brief API响应，包含内容或工具调用
    struct ApiResponse{
        std::string content;              ///< 文本内容
        std::vector<ToolCall> toolCalls;  ///< 工具调用列表

        /// @brief 检查是否有工具调用
        /// @return 是否有工具调用
        [[nodiscard]] bool hasToolCalls() const{ return !toolCalls.empty(); }

        /// @brief 检查是否有文本内容
        /// @return 是否有内容
        [[nodiscard]] bool hasContent() const{ return !content.empty(); }
    };

    /// @brief API 客户端类（单例模式）
    /// @details 封装所有 LLM API 请求，支持工具调用
    class ApiClient{
    public:
        /// @brief 获取单例实例
        /// @return ApiClient 实例引用
        static ApiClient& instance();

        /// @brief 注册工具（兼容旧接口）
        /// @param tool 工具定义
        void registerTool(const Tool& tool) const;

        /// @brief 获取工具定义列表（用于API请求）
        /// @return 工具定义 JSON 数组
        [[nodiscard]] Json::Value getToolsJson() const;

        /// @brief 执行工具（异步）
        /// @param name 工具名
        /// @param args 参数 JSON
        /// @return 执行结果
        drogon::Task<std::string> executeTool(const std::string& name, const Json::Value& args) const;

        /// @brief 异步查询天气
        /// @param city 城市名
        /// @return 天气信息文本
        static drogon::Task<std::string> fetchWeather(const std::string& city);

        /// @brief 异步网络搜索
        /// @param query 搜索关键词
        /// @return 搜索结果文本
        static drogon::Task<std::string> searchWeb(const std::string& query);

        /// @brief 请求 LLM API（使用 Executor 配置）
        /// @param messages 消息列表
        /// @param temperature 温度参数
        /// @param top_p Top-P 采样参数
        /// @param max_tokens 最大 token 数
        /// @return 响应文本，失败返回 std::nullopt
        drogon::Task<std::optional<std::string>> requestLLM(
            const Json::Value& messages,
            float temperature = 1.35f,
            float top_p = 0.92f,
            int max_tokens = 1024) const;

        /// @brief 带工具调用的API请求（返回完整响应）
        /// @param messages 消息列表
        /// @param tools 工具定义
        /// @param base_url API 基础 URL
        /// @param api_key API 密钥
        /// @param model 模型名
        /// @param temperature 温度参数
        /// @param top_p Top-P 采样参数
        /// @param max_tokens 最大 token 数
        /// @return API 响应，失败返回 std::nullopt
        drogon::Task<std::optional<ApiResponse>> requestWithTools(
            const Json::Value& messages,
            const Json::Value& tools, const std::string& base_url,
            const std::string& api_key,


            const std::string& model,
            float temperature = 0.7f,
            float top_p = 0.9f,
            int max_tokens = 1024) const;

        /// @brief Agent循环：自动处理工具调用直到生成最终回复
        /// @param messages 消息列表
        /// @param base_url API 基础 URL
        /// @param api_key API 密钥
        /// @param model 模型名
        /// @param maxIterations 最大迭代次数
        /// @return 最终回复，失败返回 std::nullopt
        drogon::Task<std::optional<std::string>> requestAgent(
            Json::Value messages,
            const std::string& base_url,
            const std::string& api_key,
            const std::string& model,
            int maxIterations = 5) const;

        /// @brief 专门处理回复决策的Agent（支持多轮工具调用）
        /// @param messages 消息列表
        /// @param base_url API 基础 URL
        /// @param api_key API 密钥
        /// @param model 模型名
        /// @param maxIterations 最大迭代次数
        /// @return 回复决策，失败返回 std::nullopt
        drogon::Task<std::optional<ReplyDecision>> requestReplyAgent(
            Json::Value messages,
            const std::string& base_url, const std::string& api_key, const std::string& model,
            int maxIterations = 5) const;

        /// @brief 通用 API 请求函数
        /// @param messages 消息列表
        /// @param base_url API 基础 URL
        /// @param path API 路径
        /// @param api_key API 密钥
        /// @param model 模型名
        /// @param temperature 温度参数
        /// @param top_p Top-P 采样参数
        /// @param max_tokens 最大 token 数
        /// @return 响应文本，失败返回 std::nullopt
        static drogon::Task<std::optional<std::string>> requestStr(
            const Json::Value& messages,
            const std::string& base_url,
            const std::string& path,
            const std::string& api_key,
            const std::string& model,
            float temperature,
            float top_p,
            int max_tokens);

    private:
        ApiClient() = default;

        /// @brief 构建模型请求体
        /// @param messages 消息列表
        /// @param model 模型名
        /// @param temperature 温度参数
        /// @param top_p Top-P 采样参数
        /// @param max_tokens 最大 token 数
        /// @return 请求体 JSON
        static Json::Value buildModelReq(
            const Json::Value& messages,
            const std::string& model,
            float temperature,
            float top_p,
            int max_tokens);
    };
}
