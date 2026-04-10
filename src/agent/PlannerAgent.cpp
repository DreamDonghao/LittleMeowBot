/// @file PlannerAgent.cpp
/// @brief Planner Agent - 实现
/// @author donghao
/// @date 2026-04-02

#include <agent/PlannerAgent.hpp>
#include <spdlog/spdlog.h>
#include <fmt/core.h>
#include <drogon/HttpClient.h>
#include <config/Config.hpp>
namespace LittleMeowBot {
    PlannerAgent& PlannerAgent::instance(){
        static PlannerAgent planner;
        return planner;
    }

    drogon::Task<std::optional<PlanResult>> PlannerAgent::plan(
        const ChatRecordManager& chatRecords,
        const MemoryManager& memory,
        const RouterDecision& routerDecision) const{
        spdlog::info("Planner: 开始规划...");

        const auto& config = Config::instance();

        // 构建 Planner Prompt
        Json::Value messages = buildPlannerPrompt(chatRecords, memory, routerDecision);

        // 使用 Planner 专用配置
        spdlog::debug("Planner请求配置: baseUrl={}, model={}", config.planner.baseUrl, config.planner.model);
        auto client = drogon::HttpClient::newHttpClient(config.planner.baseUrl);
        Json::Value body;
        body["model"] = config.planner.model;
        body["messages"] = messages;
        body["temperature"] = config.plannerParams.temperature;
        body["max_tokens"] = config.plannerParams.maxTokens;
        body["top_p"] = 0.9f;

        auto req = drogon::HttpRequest::newHttpJsonRequest(body);
        req->setMethod(drogon::Post);
        req->setPath(config.planner.path);
        req->addHeader("Authorization", "Bearer " + config.planner.apiKey);
        req->addHeader("Content-Type", "application/json");

        auto resp = co_await client->sendRequestCoro(req);
        auto json = resp->getJsonObject();

        if (resp->getStatusCode() != drogon::k200OK || !json || !json->isMember("choices")) {
            spdlog::error("Planner LLM请求失败: status={}, body={}", resp->getStatusCode(), resp->getBody());
            co_return std::nullopt;
        }

        std::string content = (*json)["choices"][0]["message"]["content"].asString();
        spdlog::info("Planner LLM响应: {}", content);

        // 解析 JSON 结果
        auto planResult = PlanResult::fromJson(extractJson(content));

        if (!planResult) {
            spdlog::error("Planner JSON解析失败");
            // 返回默认计划
            PlanResult defaultPlan;
            defaultPlan.intent = PlanResult::Intent::CHAT;
            defaultPlan.strategy.shouldReply = true;
            defaultPlan.strategy.maxLength = 25;
            defaultPlan.strategy.tone = "friendly";
            defaultPlan.strategy.reason = "无法解析规划结果，使用默认策略";
            co_return defaultPlan;
        }

        spdlog::info("Planner: 规划完成 - intent={}, shouldReply={}, enableThinking={}",
                     planResult->intent,
                     planResult->strategy.shouldReply,
                     planResult->strategy.enableThinking);

        co_return planResult;
    }

    Json::Value PlannerAgent::buildPlannerPrompt(
        const ChatRecordManager& chatRecords,
        const MemoryManager& memory,
        const RouterDecision& routerDecision) const{
        Json::Value messages;

        // System Prompt
        Json::Value systemMsg;
        systemMsg["role"] = "system";
        systemMsg["content"] = R"(你是消息规划器，分析用户意图并规划回复策略。

分析内容：
1. 用户意图类型：QUESTION(提问)/CHAT(闲聊)/HELP(求助)/ATTACK(攻击)/GREETING(问候)/UNKNOWN(未知)
2. 回复策略：shouldReply(true/false), enableThinking(true/false), tone(语气), maxLength(字数限制)

思考模式启用条件（enableThinking=true）：
- 复杂问题需要深度思考（数学计算、逻辑推理、复杂技术问题）
- 多步骤任务规划
- 需要综合多条信息分析

默认不启用思考模式（enableThinking=false），仅用于简单闲聊。

【特殊处理】
- Router决策为priority_reply时（@提及、紧急问题）：必须回复，maxLength可放宽至100

输出格式（JSON）：
{
  "intent": "QUESTION/CHAT/HELP/ATTACK/GREETING/UNKNOWN",
  "strategy": {"shouldReply": true, "enableThinking": false, "tone": "friendly", "maxLength": 25},
  "context_summary": "简短上下文总结"
}

重要：只输出JSON，简洁分析。)";
        messages.append(systemMsg);

        // 聊天记录
        std::string chatText = chatRecords.getRecordsText();

        // 短期记忆 - 直接注入
        if (std::string shortTermMemory = memory.getMemory();
            !shortTermMemory.empty()) {
            Json::Value memoryMsg;
            memoryMsg["role"] = "user";
            memoryMsg["content"] = fmt::format(
                "【短期记忆】\n{}\n请根据记忆处理下面的对话。",
                shortTermMemory);
            messages.append(memoryMsg);
        }

        Json::Value chatMsg;
        chatMsg["role"] = "user";
        chatMsg["content"] = fmt::format(
            "【聊天记录】\n{}\nRouter决策: {} ({})",
            chatText,
            actionToString(routerDecision.action),
            routerDecision.reason);
        messages.append(chatMsg);

        return messages;
    }

    std::string PlannerAgent::extractJson(const std::string& content) const{
        std::string jsonStr = content;

        // 尝试找到 JSON 部分
        const size_t start = jsonStr.find('{');
        if (const size_t end = jsonStr.rfind('}');
            start != std::string::npos && end != std::string::npos) {
            jsonStr = jsonStr.substr(start, end - start + 1);
        }

        return jsonStr;
    }

    std::string PlannerAgent::actionToString(RouterDecision::Action action) const{
        return std::string(RouterDecision::actionToString(action));
    }
}
