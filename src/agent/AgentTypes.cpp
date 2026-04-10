/// @file AgentTypes.cpp
/// @brief Agent 类型定义 - 实现
/// @author donghao
/// @date 2026-04-02

#include <agent/AgentTypes.hpp>
#include <json/reader.h>

namespace LittleMeowBot {
    std::optional<PlanResult> PlanResult::fromJson(const std::string& jsonStr){
        Json::Value root;
        if (Json::Reader reader;
            !reader.parse(jsonStr, root)) {
            return std::nullopt;
        }

        PlanResult result;

        // 解析 intent
        if (root.isMember("intent")) {
            result.intent = intentFromString(root["intent"].asString());
        }

        // 解析 strategy
        if (root.isMember("strategy")) {
            const auto& strat = root["strategy"];
            // 支持 should_reply 和 shouldReply 两种命名
            if (strat.isMember("should_reply")) {
                result.strategy.shouldReply = strat["should_reply"].asBool();
            } else if (strat.isMember("shouldReply")) {
                result.strategy.shouldReply = strat["shouldReply"].asBool();
            }
            // 支持 enable_thinking 和 enableThinking 两种命名
            if (strat.isMember("enable_thinking")) {
                result.strategy.enableThinking = strat["enable_thinking"].asBool();
            } else if (strat.isMember("enableThinking")) {
                result.strategy.enableThinking = strat["enableThinking"].asBool();
            }
            if (strat.isMember("tone")) {
                result.strategy.tone = strat["tone"].asString();
            }
            // 支持 max_length 和 maxLength 两种命名
            if (strat.isMember("max_length")) {
                result.strategy.maxLength = strat["max_length"].asInt();
            } else if (strat.isMember("maxLength")) {
                result.strategy.maxLength = strat["maxLength"].asInt();
            }
            if (strat.isMember("reason")) {
                result.strategy.reason = strat["reason"].asString();
            }
        }

        // 解析 context_summary
        if (root.isMember("context_summary")) {
            result.contextSummary = root["context_summary"].asString();
        }

        return result;
    }

    Json::Value PlanResult::toJson() const{
        Json::Value root;

        root["intent"] = std::string(intentToString(intent));

        Json::Value strategyJson;
        strategyJson["should_reply"] = strategy.shouldReply;
        strategyJson["enable_thinking"] = strategy.enableThinking;
        strategyJson["tone"] = strategy.tone;
        strategyJson["max_length"] = strategy.maxLength;
        strategyJson["reason"] = strategy.reason;
        root["strategy"] = strategyJson;

        root["context_summary"] = contextSummary;

        return root;
    }
} // namespace LittleMeowBot