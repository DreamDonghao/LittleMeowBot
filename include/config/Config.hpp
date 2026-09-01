/// @file Config.hpp
/// @brief 全局配置管理
#pragma once
#include <json/value.h>
#include <string>

namespace insoulforge {
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
        LLMApiConfig embedding; // Embedding 模型配置（长期记忆向量化）

        // 记忆配置
        int windowTriggerCount = 100; // 上下文窗口超过该条数时触发提取与滑动
        int windowKeepCount = 50; // 触发后保留的最近消息条数
        int memoryExtractMaxTokens = 4000; // 记忆提取 LLM 调用的 maxTokens
        int routerWindowTriggerCount = 20; // Router 子窗口触发条数（批量滑动）
        int routerWindowKeepCount = 10; // Router 子窗口保留条数
        int shortTermMemoryMax = 15;
        double longTermRecallThreshold = 0.65; // 长期记忆召回合并的相似度阈值（独立于 recall_memory 的 0.3）
        double longTermInjectThreshold = 0.45; // 长期记忆被动注入提示词的相似度阈值（消息入库时逐条召回）

        // QQ Bot 配置
        std::string accessToken;
        std::uint64_t selfQQNumber = 0;
        std::string qqHttpHost;
        std::string botName = "小喵";

        static Config &instance();

        void loadFromDatabase();

    private:
        Config() = default;
    };
} // namespace insoulforge
