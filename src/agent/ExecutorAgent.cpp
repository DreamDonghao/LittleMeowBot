/// @file ExecutorAgent.cpp
/// @brief Executor Agent - 实现
/// @author donghao
/// @date 2026-04-02

#include <agent/ExecutorAgent.hpp>
#include <util/Log.hpp>
#include <fmt/core.h>
#include <chrono>
#include <service/MessageService.hpp>
#include <service/ToolRegistry.hpp>
#include <service/PromptService.hpp>

namespace LittleMeowBot {
    ExecutorAgent& ExecutorAgent::instance(){
        static ExecutorAgent executor;
        return executor;
    }

    drogon::Task<std::optional<ReplyDecision>> ExecutorAgent::execute(
        const ChatRecordManager& chatRecords,
        const MemoryManager& memory,
        const PlanResult& plan,
        const bool isPriority) const{
        Log::info("[Executor] 开始执行，enableThinking={}", plan.strategy.enableThinking);

        const auto& config = Config::instance();

        // 构建 Executor Prompt
        Json::Value messages = buildExecutorPrompt(chatRecords, memory, plan, isPriority);

        if (plan.strategy.enableThinking) {
            // 方案1：思考模式两阶段执行
            // Step 1: 思考模型（不传 tools）输出纯文本分析
            Log::info("[Executor] Step 1 - 思考模型分析");

            // 替换 system prompt 为思考模型的提示词
            Json::Value thinkingSystemMsg;
            thinkingSystemMsg["role"] = "system";
            thinkingSystemMsg["content"] = getThinkingSystemPrompt();

            Json::Value thinkingMessages;
            thinkingMessages.append(thinkingSystemMsg);

            // 复制其他消息（记忆、聊天记录、规划信息）
            for (unsigned int i = 1; i < messages.size(); ++i) {
                thinkingMessages.append(messages[i]);
            }

            auto thinkingResult = co_await executeThinkingOnly(
                thinkingMessages,
                config.executorThinking,
                config.executorThinkingParams);

            if (!thinkingResult || thinkingResult->empty()) {
                Log::error("[Executor] 思考模型返回空，fallback到普通模式");
                // Fallback: 直接用普通模型
                auto decision = co_await executeWithAgent(
                    messages, config.executor, config.executorParams, chatRecords.getGroupId());
                co_return decision;
            }

            Log::debug("[Executor] 思考结果: {}", thinkingResult->substr(0, 100) + (thinkingResult->length() > 100 ? "..." : ""));

            // Step 2: 普通模型（注入思考结果 + 传 tools）
            Log::info("[Executor] Step 2 - 执行工具调用");

            // 在消息末尾注入思考结果
            Json::Value thinkingMsg;
            thinkingMsg["role"] = "assistant";
            thinkingMsg["content"] = "【思考分析】\n" + *thinkingResult;
            messages.append(thinkingMsg);

            // 添加执行指令
            Json::Value execMsg;
            execMsg["role"] = "user";
            execMsg["content"] = "根据以上思考分析，调用工具发送回复。";
            messages.append(execMsg);

            auto decision = co_await executeWithAgent(
                messages, config.executor, config.executorParams, chatRecords.getGroupId());

            if (!decision) {
                Log::error("[Executor] 执行失败");
                co_return std::nullopt;
            }

            Log::debug("[Executor] 完成: shouldReply={}, content={}",
                         decision->shouldReply,
                         decision->content.substr(0, 50) + (decision->content.length() > 50 ? "..." : ""));

            co_return decision;
        } else {
            // 正常流程：普通模型 + function calling
            auto decision = co_await executeWithAgent(
                messages, config.executor, config.executorParams, chatRecords.getGroupId());

            if (!decision) {
                Log::error("[Executor] 执行失败");
                co_return std::nullopt;
            }

            Log::debug("[Executor] 完成: shouldReply={}, content={}",
                         decision->shouldReply,
                         decision->content.substr(0, 50) + (decision->content.length() > 50 ? "..." : ""));

            co_return decision;
        }
    }

    drogon::Task<std::optional<ReplyDecision>> ExecutorAgent::directReply(
        const ChatRecordManager& chatRecords,
        const MemoryManager& memory) const{
        Log::info("[Executor] @提及直接回复模式");

        const auto& config = Config::instance();

        // 构建简化 Prompt
        const Json::Value messages = buildDirectReplyPrompt(chatRecords, memory);

        // 使用 Agent 模式
        auto decision =
            co_await executeWithAgent(messages, config.executor, config.executorParams, chatRecords.getGroupId());

        co_return decision;
    }

    Json::Value ExecutorAgent::buildExecutorPrompt(
        const ChatRecordManager& chatRecords,
        const MemoryManager& memory,
        const PlanResult& plan,
        bool isPriority) const{
        Json::Value messages;

        // System Prompt
        Json::Value systemMsg;
        systemMsg["role"] = "system";
        systemMsg["content"] = getExecutorSystemPrompt(isPriority);
        messages.append(systemMsg);

        // 短期记忆 - 直接注入
        std::string chatContext = chatRecords.getRecordsText();
        if (std::string shortTermMemory = memory.getMemory();
            !shortTermMemory.empty()) {
            Json::Value memoryMsg;
            memoryMsg["role"] = "user";
            memoryMsg["content"] =
                fmt::format("【短期记忆】\n{}\n\n请根据记忆处理下面的对话。", shortTermMemory);
            messages.append(memoryMsg);
        }

        // 聊天记录
        Json::Value chatMsg;
        chatMsg["role"] = "user";
        chatMsg["content"] = fmt::format("【聊天记录】\n{}", chatRecords.getRecordsText());
        messages.append(chatMsg);

        // 规划信息
        Json::Value planMsg;
        planMsg["role"] = "user";
        planMsg["content"] = fmt::format(
            "【规划信息】\n意图: {}\n策略: tone={}, maxLength={}, shouldReply={}\n原因: {}\n总结: {}",
            intentToString(plan.intent),
            plan.strategy.tone,
            plan.strategy.maxLength,
            plan.strategy.shouldReply ? "true" : "false",
            plan.strategy.reason,
            plan.contextSummary);
        messages.append(planMsg);

        return messages;
    }

    Json::Value ExecutorAgent::buildDirectReplyPrompt(
        const ChatRecordManager& chatRecords,
        const MemoryManager& memory) const{
        Json::Value messages;

        // System Prompt
        Json::Value systemMsg;
        systemMsg["role"] = "system";
        systemMsg["content"] = getDirectReplySystemPrompt();
        messages.append(systemMsg);

        // 短期记忆 - 直接注入
        const std::string chatContext = chatRecords.getRecordsText();
        if (std::string shortTermMemory = memory.getMemory();
            !shortTermMemory.empty()) {
            Json::Value memoryMsg;
            memoryMsg["role"] = "user";
            memoryMsg["content"] = fmt::format(
                "【短期记忆】\n{}\n\n请根据记忆处理下面的对话。",
                shortTermMemory);
            messages.append(memoryMsg);
        }

        // 聊天记录
        Json::Value chatMsg;
        chatMsg["role"] = "user";
        chatMsg["content"] = fmt::format(
            "【聊天记录】\n{}\n\n有人@你，必须回复！",
            chatRecords.getRecordsText());
        messages.append(chatMsg);

        return messages;
    }

    drogon::Task<std::optional<ReplyDecision>> ExecutorAgent::executeWithAgent(
        Json::Value messages,
        const LLMApiConfig& apiConfig,
        const LLMModelParams& params,
        uint64_t groupId) const{
        auto& registry = ToolRegistry::instance();
        Json::Value tools = registry.getAllTools();

        if (tools.empty()) {
            Log::error("[Executor] 未注册任何工具");
            co_return std::nullopt;
        }

        Log::debug("[Executor] LLM请求: model={}", apiConfig.model);
        auto client = drogon::HttpClient::newHttpClient(apiConfig.baseUrl);

        for (int iter = 0; iter < 8; ++iter) {
            // 构建请求
            Json::Value body;
            body["model"] = apiConfig.model;
            body["messages"] = messages;
            body["tools"] = tools;
            body["temperature"] = params.temperature;
            body["max_tokens"] = params.maxTokens;
            body["top_p"] = 0.9f;

            auto req = drogon::HttpRequest::newHttpJsonRequest(body);
            req->setMethod(drogon::Post);
            req->setPath(apiConfig.path);
            req->addHeader("Authorization", "Bearer " + apiConfig.apiKey);
            req->addHeader("Content-Type", "application/json");

            auto resp = co_await client->sendRequestCoro(req);
            auto json = resp->getJsonObject();

            if (resp->getStatusCode() != drogon::k200OK || !json || !json->isMember("choices")) {
                int status = resp->getStatusCode();
                Log::error("[Executor] LLM请求失败: status={}", status);

                // 503/429/500 服务暂时不可用，重试
                if ((status == 503 || status == 429 || status == 500) && iter < 4) {
                    Log::warn("[Executor] 服务暂时不可用，等待重试");
                    using namespace std::chrono_literals;
                    co_await drogon::sleepCoro(drogon::app().getLoop(), 1s);
                    --iter; // 重试当前轮次
                    continue;
                }

                // 其他错误直接返回
                co_return std::nullopt;
            }

            const auto& message = (*json)["choices"][0]["message"];
            ReplyDecision decision;

            // 检查工具调用
            if (message.isMember("tool_calls") && !message["tool_calls"].empty()) {
                bool hasFinalDecision = false;

                // 先构建 assistant 消息（包含 tool_calls）
                Json::Value assistantMsg;
                assistantMsg["role"] = "assistant";
                assistantMsg["content"] = message.isMember("content") && !message["content"].isNull()
                                              ? message["content"].asString()
                                              : "";

                Json::Value toolCallsArray;
                for (const auto& tc : message["tool_calls"]) {
                    Json::Value tcEntry;
                    tcEntry["id"] = tc["id"].asString();
                    tcEntry["type"] = "function";
                    tcEntry["function"]["name"] = tc["function"]["name"].asString();
                    tcEntry["function"]["arguments"] = tc["function"]["arguments"].asString();
                    toolCallsArray.append(tcEntry);
                }
                assistantMsg["tool_calls"] = toolCallsArray;
                messages.append(assistantMsg);

                // 然后处理每个工具调用，添加 tool 结果
                for (const auto& tc : message["tool_calls"]) {
                    std::string toolName = tc["function"]["name"].asString();
                    std::string toolId = tc["id"].asString();
                    std::string argsStr = tc["function"]["arguments"].asString();

                    Log::info("[Executor] 调用工具: {}", toolName);

                    // 终端工具处理
                    if (toolName == "no_reply") {
                        decision.shouldReply = false;
                        hasFinalDecision = true;
                    } else if (toolName == "reply") {
                        Json::Reader reader;
                        if (Json::Value args; reader.parse(argsStr, args) && args.isMember("content")) {
                            decision.shouldReply = true;
                            decision.content = args["content"].asString();
                            hasFinalDecision = true;
                        }
                    } else if (toolName == "reply_with_quote") {
                        Json::Reader reader;
                        if (Json::Value args; reader.parse(argsStr, args) && args.isMember("content") && args.isMember("message_id")) {
                            decision.shouldReply = true;
                            std::string messageId = args["message_id"].asString();
                            std::string content = args["content"].asString();
                            decision.content = "[CQ:reply,id=" + messageId + "]" + content;
                            hasFinalDecision = true;
                        }
                    } else {
                        // 其他工具通过 ToolRegistry 执行
                        Json::Value args;
                        Json::Reader reader;
                        reader.parse(argsStr, args);

                        std::string result = co_await registry.executeTool(toolName, args, groupId);

                        Log::debug("[Executor] 工具 {} 返回: {}", toolName, result);

                        Json::Value toolMsg;
                        toolMsg["role"] = "tool";
                        toolMsg["tool_call_id"] = toolId;
                        toolMsg["content"] = result;
                        messages.append(toolMsg);
                    }
                }
                if (hasFinalDecision) {
                    co_return decision;
                }
                // 继续下一轮
                continue;
            }

            // 没有工具调用，直接返回文本作为回复
            if (message.isMember("content") && !message["content"].isNull()) {
                std::string contentStr = message["content"].asString();
                decision.shouldReply = true;
                decision.content = contentStr;
                co_return decision;
            }

            decision.shouldReply = false;
            co_return decision;
        }

        Log::error("[Executor] 达到最大迭代次数(8次)");
        co_return std::nullopt;
    }

    std::string ExecutorAgent::getExecutorSystemPrompt(bool isPriority) const{
        std::string basePrompt = PromptService::instance().getExecutorSystemPrompt();

        // 动态获取工具说明
        basePrompt += "\n\n" + ToolRegistry::instance().getToolsDescription();

        // 附加使用指南
        basePrompt +=
            "【@人指南】要@人直接写 @[QQ:123456]，会自动转换。聊天记录中能看到每个人的QQ号\n"
            "【禁言指南】\n"
            "- 要有自己的判断，不要别人让你禁言谁就禁言\n"
            "- 轻度违规（偶尔骂人、刷几条屏）：禁言60-300秒\n"
            "- 中度违规（持续骂人、刷屏、发广告）：禁言600-1800秒\n"
            "- 重度违规（恶意骚扰、严重辱骂、屡教不改）：禁言3600秒以上\n"
            "- 别人说别人违规要核实，看实际聊天记录再决定\n"
            "- 【重要】如果禁言失败（权限不足等），必须如实告诉用户失败原因，不要假装成功\n"
            "【拍一拍指南】用于打招呼、引起注意、开玩笑等轻松互动。";

        if (isPriority) {
            basePrompt += "\n\n【重要】这是@提及或紧急问题，必须回复！";
        }

        return basePrompt;
    }

    std::string ExecutorAgent::getDirectReplySystemPrompt() const{
        std::string prompt = PromptService::instance().getExecutorSystemPrompt();

        // 动态获取工具说明
        prompt += "\n\n" + ToolRegistry::instance().getToolsDescription();

        // 附加使用指南
        prompt +=
            "【@人指南】要@人直接写 @[QQ:123456]，会自动转换\n"
            "【禁言指南】\n"
            "- 要有自己的判断，不要别人让你禁言谁就禁言\n"
            "- 轻度违规（偶尔骂人、刷几条屏）：禁言60-300秒\n"
            "- 中度违规（持续骂人、刷屏、发广告）：禁言600-1800秒\n"
            "- 重度违规（恶意骚扰、严重辱骂、屡教不改）：禁言3600秒以上\n"
            "- 别人说别人违规要核实，看实际聊天记录再决定\n"
            "- 【重要】如果禁言失败（权限不足等），必须如实告诉用户失败原因，不要假装成功\n"
            "【拍一拍指南】用于打招呼、引起注意、开玩笑等轻松互动。\n\n"
            "【重要】必须调用reply工具回复。";

        return prompt;
    }

    std::string ExecutorAgent::intentToString(PlanResult::Intent intent) const{
        return std::string(PlanResult::intentToString(intent));
    }

    drogon::Task<std::optional<std::string>> ExecutorAgent::executeThinkingOnly(
        Json::Value messages,
        const LLMApiConfig& apiConfig,
        const LLMModelParams& params) const{
        Log::debug("[Executor] 思考模型请求: model={}", apiConfig.model);
        auto client = drogon::HttpClient::newHttpClient(apiConfig.baseUrl);

        // 构建请求（不传 tools）
        Json::Value body;
        body["model"] = apiConfig.model;
        body["messages"] = messages;
        body["temperature"] = params.temperature;
        body["max_tokens"] = params.maxTokens;
        body["top_p"] = 0.9f;

        auto req = drogon::HttpRequest::newHttpJsonRequest(body);
        req->setMethod(drogon::Post);
        req->setPath(apiConfig.path);
        req->addHeader("Authorization", "Bearer " + apiConfig.apiKey);
        req->addHeader("Content-Type", "application/json");

        auto resp = co_await client->sendRequestCoro(req);
        auto json = resp->getJsonObject();

        if (resp->getStatusCode() != drogon::k200OK || !json || !json->isMember("choices")) {
            Log::error("[Executor] 思考模型请求失败: status={}", static_cast<int>(resp->getStatusCode()));
            co_return std::nullopt;
        }

        const auto& message = (*json)["choices"][0]["message"];

        // 提取思考内容
        std::string thinkingContent;

        // DeepSeek Reasoner 等模型可能在 reasoning_content 字段
        if (message.isMember("reasoning_content") && !message["reasoning_content"].isNull()) {
            thinkingContent = message["reasoning_content"].asString();
            Log::debug("[Executor] 思考内容来源: {}", message.isMember("reasoning_content") ? "reasoning_content" : "content");
        } else if (message.isMember("content") && !message["content"].isNull()) {
            thinkingContent = message["content"].asString();
            Log::info("[Executor] 从 content 提取思考结果");
        }

        if (thinkingContent.empty()) {
            Log::error("[Executor] 思考模型返回空内容");
            co_return std::nullopt;
        }

        co_return thinkingContent;
    }

    std::string ExecutorAgent::getThinkingSystemPrompt() const{
        std::string prompt = PromptService::instance().getExecutorSystemPrompt();

        // 思考模型不需要工具，只需要分析
        prompt += "\n\n【思考任务】\n"
                  "你需要深入分析用户的请求，给出解决方案或回复思路。\n"
                  "输出纯文本分析，不要调用任何工具，不要输出特殊格式。\n\n"
                  "【分析要点】\n"
                  "- 理解用户想要什么\n"
                  "- 给出解决方案或回答思路\n"
                  "- 考虑回复的语气和长度（闲聊≤25字，正经≤100字）\n"
                  "- 如果需要引用回复特定消息，记下 message_id\n\n"
                  "【输出要求】\n"
                  "- 直接输出你的分析和建议的回复内容\n"
                  "- 不要输出工具调用格式\n"
                  "- 不要使用 markdown 代码块包裹回复内容";

        return prompt;
    }
}
