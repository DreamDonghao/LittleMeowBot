/// @file Config.cpp
/// @brief 全局配置管理 - 实现

#include <config/Config.hpp>
#include <storage/Database.hpp>
#include <spdlog/spdlog.h>

namespace LittleMeowBot {
    Config& Config::instance(){
        static Config config{};
        return config;
    }

    void Config::loadFromDatabase(){
        loadLLMConfig("router", router, &routerParams);
        loadLLMConfig("planner", planner, &plannerParams);
        loadLLMConfig("executor", executor, &executorParams);
        loadLLMConfig("executorThinking", executorThinking, &executorThinkingParams);
        loadLLMConfig("memory", memory, &memoryParams);
        loadLLMConfig("image", image);

        // 加载知识库配置
        if (auto kbCfg = Database::instance().getKBConfig();
            !kbCfg.isNull()) {
            knowledgeBase.apiKey = kbCfg["apiKey"].asString();
            knowledgeBase.baseUrl = kbCfg["baseUrl"].asString();
            knowledgeBase.knowledgeDatasetId = kbCfg["knowledgeDatasetId"].asString();
            knowledgeBase.memoryDatasetId = kbCfg["memoryDatasetId"].asString();
            if (kbCfg.isMember("memoryDocumentId")) {
                knowledgeBase.memoryDocumentId = kbCfg["memoryDocumentId"].asString();
            }
            spdlog::info("知识库配置已从数据库加载");
        }

        // 加载记忆配置
        if (auto memCfg = Database::instance().getMemoryConfig();
            !memCfg.isNull()) {
            memoryTriggerCount = memCfg["memoryTriggerCount"].asInt();
            memoryChatRecordLimit = memCfg["memoryChatRecordLimit"].asInt();
            shortTermMemoryMax = memCfg["shortTermMemoryMax"].asInt();
            shortTermMemoryLimit = memCfg["shortTermMemoryLimit"].asInt();
            memoryMigrateCount = memCfg["memoryMigrateCount"].asInt();
            spdlog::info("记忆配置已从数据库加载");
        }

        // 加载 QQ Bot 配置
        if (auto qqCfg = Database::instance().getQQConfig();
            !qqCfg.isNull()) {
            accessToken = qqCfg["accessToken"].asString();
            selfQQNumber = qqCfg["selfQQNumber"].asInt64();
            qqHttpHost = qqCfg["qqHttpHost"].asString();
            if (qqCfg.isMember("botName")) {
                botName = qqCfg["botName"].asString();
            }
            spdlog::info("QQ Bot 配置已从数据库加载");
        }

        spdlog::info("所有配置已从数据库加载");
    }
}