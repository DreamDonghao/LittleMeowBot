/// @file Config.hpp
/// @brief 全局配置管理
#pragma once
#include <json/value.h>
#include <string>
#include <storage/Database.hpp>

namespace LittleMeowBot {
    struct LLMApiConfig{
        std::string apiKey;
        std::string baseUrl;
        std::string path;
        std::string model;
    };

    struct LLMModelParams{
        int maxTokens = 1024;
        float temperature = 0.7f;
        float topP = 0.9f;
    };

    struct KBApiConfig{
        std::string apiKey;
        std::string baseUrl;
        std::string knowledgeDatasetId;
        std::string memoryDatasetId;
        std::string memoryDocumentId;
    };

    class Config{
    public:
        // Agent 配置
        LLMApiConfig router;
        LLMModelParams routerParams;
        LLMApiConfig planner;
        LLMModelParams plannerParams;
        LLMApiConfig executor;
        LLMModelParams executorParams;
        LLMApiConfig executorThinking;  // Executor 思考模型配置
        LLMModelParams executorThinkingParams;
        LLMApiConfig memory;
        LLMModelParams memoryParams;
        LLMApiConfig image;

        // 记忆配置
        int memoryTriggerCount = 16;
        int memoryChatRecordLimit = 18;
        int shortTermMemoryMax = 15;
        int shortTermMemoryLimit = 20;
        int memoryMigrateCount = 5;

        // QQ Bot 配置
        std::string accessToken;
        std::uint64_t selfQQNumber = 0;
        std::string qqHttpHost;
        std::string botName = "小喵";

        // 知识库配置
        KBApiConfig knowledgeBase;

        static Config& instance();

        /// @brief 从数据库加载单个 LLM 配置
        template <typename ApiCfg, typename ParamsCfg = LLMModelParams*>
        void loadLLMConfig(const std::string_view name, ApiCfg& apiConfig, ParamsCfg modelParams = nullptr){
            auto cfg = Database::instance().getLLMConfig(std::string(name));
            if (cfg.isNull()) return;

            apiConfig.apiKey = cfg["apiKey"].asString();
            apiConfig.baseUrl = cfg["baseUrl"].asString();
            apiConfig.path = cfg["path"].asString();
            apiConfig.model = cfg["model"].asString();

            if constexpr (!std::is_null_pointer_v<ParamsCfg>) {
                if (modelParams) {
                    modelParams->maxTokens = cfg["maxTokens"].asInt();
                    modelParams->temperature = cfg["temperature"].asFloat();
                    modelParams->topP = cfg["topP"].asFloat();
                }
            }
        }

        void loadFromDatabase();

    private:
        Config() = default;
    };
}
