/// @file Config.hpp
/// @brief 全局配置管理
#pragma once
#include <json/value.h>
#include <cstdint>
#include <string>
#include <storage/Database.hpp>

namespace LittleMeowBot {
    struct LLMApiConfig {
        std::string apiKey;
        std::string baseUrl;
        std::string path;
        std::string model;
        std::string reasoningEffort; // "none"/"medium"/"high"，空串表示不发送
    };

    struct LLMModelParams {
        int maxTokens = 1024;
        // 用 double 保证 JSON 序列化输出 0.7 而非 0.699999988079071
        double temperature = 0.7;
        double topP = 0.9;
    };

    struct KBApiConfig {
        bool enabled = true; // 是否启用 RAGFlow
        std::string apiKey;
        std::string baseUrl;
        std::string knowledgeDatasetId;
        std::string memoryDatasetId;
        std::string memoryDocumentId;
    };

    class Config {
    public:
        // Agent 配置
        LLMApiConfig router;
        LLMModelParams routerParams;
        LLMApiConfig executor;
        LLMModelParams executorParams;
        LLMApiConfig executorThinking; // Executor 思考模型配置
        LLMModelParams executorThinkingParams;
        LLMApiConfig image;

        // 记忆配置
        int windowTriggerCount = 100; // 上下文窗口超过该条数时触发提取与滑动
        int windowKeepCount = 50; // 触发后保留的最近消息条数
        int memoryExtractMaxTokens = 4000; // 记忆提取 LLM 调用的 maxTokens
        int routerWindowTriggerCount = 20; // Router 子窗口触发条数（批量滑动）
        int routerWindowKeepCount = 10; // Router 子窗口保留条数
        int shortTermMemoryMax = 15;
        int memoryMigrateCount = 5;

        // QQ Bot 配置
        std::string accessToken;
        std::uint64_t selfQQNumber = 0;
        std::string qqHttpHost;
        std::string botName = "小喵";

        // 知识库配置
        KBApiConfig knowledgeBase;

        static Config &instance();

        void loadFromDatabase();

    private:
        Config() = default;
    };
}
