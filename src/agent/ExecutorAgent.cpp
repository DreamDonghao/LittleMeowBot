/// @file ExecutorAgent.cpp
/// @brief Executor Agent - 实现

#include <agent/ExecutorAgent.hpp>
#include <agent/AgentToolManager.hpp>
#include <api/ApiClient.hpp>
#include <util/Log.hpp>
#include <service/ToolRegistry.hpp>
#include <service/PromptService.hpp>
#include <fmt/core.h>
#include <chrono>
#include <memory>
#include <regex>
#include <drogon/HttpClient.h>
#include <drogon/HttpAppFramework.h>

#include "config/Config.hpp"

namespace LittleMeowBot {
    namespace {
        /// @brief 获取系统提示词
        std::string getSystemPrompt(const bool isPriority, int maxLength) {
            std::string prompt = PromptService::getExecutorSystemPrompt();
            prompt += "\n\n" + ToolRegistry::instance().getToolsDescription();

            prompt += fmt::format(
                "\n\n【回复要求】\n"
                "- 字数限制: {} 字\n"
                "- 要有自己的判断，不要别人说什么就做什么\n"
                "- @人格式: @[QQ:123456]\n"
                "- 禁言要核实实际情况再决定\n"
                "- 【重要】表情/图片的CQ码必须通过工具获取(send_sticker/send_face/send_image)。先调工具，拿到结果后把返回的[CQ:image...]或[CQ:face...]原样拼接到reply的content中。禁止自己编造假CQ标签，工具返回什么就复制什么\n",
                maxLength);

            if (isPriority) {
                prompt += "\n【重要】这是@提及或紧急问题，必须回复！";
            }

            return prompt;
        }

        /// @brief 获取思考模型系统提示词
        std::string getThinkingSystemPrompt(int maxLength) {
            std::string prompt = PromptService::getExecutorSystemPrompt();

            prompt += fmt::format(
                "\n\n【思考任务】\n"
                "分析用户请求，给出回复思路。\n\n"
                "【分析要点】\n"
                "- 用户想要什么\n"
                "- 解决方案或回复思路\n"
                "- 回复语气和长度（{}字左右）\n"
                "- 需要引用回复时记下 message_id\n\n"
                "【输出要求】\n"
                "- 直接输出分析和建议的回复内容\n"
                "- 不使用 markdown 代码块\n"
                "- 不输出工具调用格式",
                maxLength);

            return prompt;
        }

        /// @brief 清理回复内容，并在模型忘记拼 CQ 码时自动补上
        std::string finalizeContent(const std::string& rawContent, const std::string& accumulatedCQCodes) {
            std::string content = cleanReplyContent(rawContent);
            if (!accumulatedCQCodes.empty() && content.find("[CQ:") == std::string::npos) {
                content += accumulatedCQCodes;
            }
            return content;
        }

        /// @brief 执行思考模型（不带 tools）
        drogon::Task<std::optional<std::string> > executeThinking(
            Json::Value messages,
            int maxLength) {
            const auto &config = Config::instance();

            // 替换 system prompt
            Json::Value thinkingMessages;
            Json::Value thinkingSystem;
            thinkingSystem["role"] = "system";
            thinkingSystem["content"] = getThinkingSystemPrompt(maxLength);
            thinkingMessages.append(thinkingSystem);

            for (unsigned int i = 1; i < messages.size(); ++i) {
                thinkingMessages.append(messages[i]);
            }

            Log::debug("[Executor] 思考模型: {}", config.executorThinking.model);

            auto client = drogon::HttpClient::newHttpClient(config.executorThinking.baseUrl);
            Json::Value body;
            body["model"] = config.executorThinking.model;
            body["messages"] = thinkingMessages;
            body["temperature"] = config.executorThinkingParams.temperature;
            body["max_tokens"] = config.executorThinkingParams.maxTokens;
            body["top_p"] = 0.9f;
            if (!config.executorThinking.reasoningEffort.empty()) {
                body["reasoning_effort"] = config.executorThinking.reasoningEffort;
            }

            auto req = drogon::HttpRequest::newHttpJsonRequest(body);
            req->setMethod(drogon::Post);
            req->setPath(config.executorThinking.path);
            req->addHeader("Authorization", "Bearer " + config.executorThinking.apiKey);
            req->addHeader("Content-Type", "application/json");

            try {
                auto resp = co_await client->sendRequestCoro(req, 90.0);
                auto json = resp->getJsonObject();

                if (resp->getStatusCode() != drogon::k200OK || !json || !json->isMember("choices")) {
                    std::string body = std::string(resp->getBody()).substr(0, 500);
                    Log::error("[Executor] 思考模型失败: status={} body={}",
                               static_cast<int>(resp->getStatusCode()), body);
                    co_return std::nullopt;
                }

                ApiClient::logUsage(*json, config.executorThinking.model);

                const auto &message = (*json)["choices"][0]["message"];
                std::string content;

                // DeepSeek Reasoner 的 reasoning_content 字段
                if (message.isMember("reasoning_content") && !message["reasoning_content"].isNull()) {
                    content = message["reasoning_content"].asString();
                } else if (message.isMember("content") && !message["content"].isNull()) {
                    content = message["content"].asString();
                }

                co_return content;
            } catch (const std::exception& e) {
                Log::error("[Executor] 思考模型请求异常: {}", e.what());
                co_return std::nullopt;
            }
        }


        /// @brief 把记录队列拼接为 JSON 数组字符串
        std::string joinRecords(const std::deque<Json::Value>& records, size_t from, size_t to){
            std::string text = "[";
            bool first = true;
            for (size_t i = from; i < to; ++i) {
                if (!first) text += ",";
                first = false;
                text += records[i]["content"].asString();
            }
            text += "]";
            return text;
        }

        /// @brief 按 UTF-8 字符边界截断
        std::string truncateUtf8(const std::string& text, size_t maxChars){
            size_t i = 0;
            size_t count = 0;
            while (i < text.size() && count < maxChars) {
                const auto c = static_cast<unsigned char>(text[i]);
                if (c < 0x80) {
                    i += 1;
                } else if ((c & 0xE0) == 0xC0) {
                    i += 2;
                } else if ((c & 0xF0) == 0xE0) {
                    i += 3;
                } else if ((c & 0xF8) == 0xF0) {
                    i += 4;
                } else {
                    i += 1;  // 非法字节，跳过
                }
                ++count;
            }
            if (i >= text.size()) return text;
            return text.substr(0, i) + "…";
        }

        /// @brief 截断记录 content(JSON) 的 text 字段，保留 JSON 结构
        std::string truncateRecordText(const std::string& content, size_t maxChars){
            Json::Value record;
            Json::CharReaderBuilder readerBuilder;
            std::string errs;
            std::unique_ptr<Json::CharReader> reader(readerBuilder.newCharReader());
            if (!reader->parse(content.data(), content.data() + content.size(), &record, &errs)
                || !record.isObject()) {
                return truncateUtf8(content, maxChars);
            }
            if (record.isMember("text") && record["text"].isString()) {
                record["text"] = truncateUtf8(record["text"].asString(), maxChars);
            }
            Json::StreamWriterBuilder writerBuilder;
            writerBuilder["indentation"] = "";
            writerBuilder["emitUTF8"] = true;
            return Json::writeString(writerBuilder, record);
        }

        /// @brief 构建聊天记录上下文（窗口内，旧 → 新）：
        /// 最新 8 条原样保留，更早的每条 text 截断到 500 字
        std::string buildChatContextText(const ChatRecordManager& chatRecords){
            constexpr size_t kRecentFullCount = 8;
            constexpr size_t kOldRecordMaxChars = 500;

            const auto records = chatRecords.getRecords(); // 旧 → 新
            const size_t olderCount = records.size() > kRecentFullCount
                                          ? records.size() - kRecentFullCount
                                          : 0;

            std::string context;
            if (olderCount > 0) {
                std::string olderText = "[";
                for (size_t i = 0; i < olderCount; ++i) {
                    if (i > 0) olderText += ",";
                    olderText += truncateRecordText(
                        records[i]["content"].asString(), kOldRecordMaxChars);
                }
                olderText += "]";
                context += "【更早对话】\n" + olderText + "\n\n";
            }

            context += "【最近对话】\n" + joinRecords(records, olderCount, records.size());
            return context;
        }

        /// @brief 构建 Executor Prompt
        drogon::Task<Json::Value> buildPrompt(
            const ChatRecordManager &chatRecords,
            const MemoryManager &memory,
            const RouterDecision &decision) {
            Json::Value messages;

            // System Prompt
            Json::Value systemMsg;
            systemMsg["role"] = "system";
            systemMsg["content"] = getSystemPrompt(decision.isPriority, decision.maxLength);
            messages.append(systemMsg);

            // 短期记忆
            if (std::string shortMemory = memory.getMemory(); !shortMemory.empty()) {
                Json::Value memoryMsg;
                memoryMsg["role"] = "user";
                memoryMsg["content"] = fmt::format("【短期记忆】\n{}\n\n结合记忆处理下面的对话。", shortMemory);
                messages.append(memoryMsg);
            }

            // 聊天记录（窗口内：最近8条原文 + 更早的每条截断到500字）
            Json::Value chatMsg;
            chatMsg["role"] = "user";
            chatMsg["content"] = buildChatContextText(chatRecords);
            messages.append(chatMsg);

            // 策略提示
            Json::Value strategyMsg;
            strategyMsg["role"] = "user";
            strategyMsg["content"] = fmt::format(
                "【回复要求】\n语气: {}\n字数限制: {} 字\n原因: {}",
                decision.tone, decision.maxLength, decision.reason);
            messages.append(strategyMsg);

            co_return messages;
        }

        /// @brief Agent 模式执行（带 tools）
        drogon::Task<std::optional<ReplyDecision> > executeWithAgent(
            Json::Value messages,
            const LLMApiConfig &apiConfig,
            const LLMModelParams &params,
            uint64_t groupId) {
            auto &registry = ToolRegistry::instance();
            Json::Value tools = registry.getAllTools();

            if (tools.empty()) {
                Log::error("[Executor] 未注册工具");
                co_return std::nullopt;
            }

            Log::debug("[Executor] LLM: {}", apiConfig.model);
            auto client = drogon::HttpClient::newHttpClient(apiConfig.baseUrl);

            std::string accumulatedCQCodes; // 跨轮累积CQ码，reply时自动拼入

            for (int iter = 0; iter < 6; ++iter) {
                Json::Value body;
                body["model"] = apiConfig.model;
                body["messages"] = messages;
                body["tools"] = tools;
                body["temperature"] = params.temperature;
                body["max_tokens"] = params.maxTokens;
                body["top_p"] = params.topP;
                if (!apiConfig.reasoningEffort.empty()) {
                    body["reasoning_effort"] = apiConfig.reasoningEffort;
                }

                auto req = drogon::HttpRequest::newHttpJsonRequest(body);
                req->setMethod(drogon::Post);
                req->setPath(apiConfig.path);
                req->addHeader("Authorization", "Bearer " + apiConfig.apiKey);
                req->addHeader("Content-Type", "application/json");

                std::shared_ptr<drogon::HttpResponse> resp;
                bool networkError = false;
                try {
                    resp = co_await client->sendRequestCoro(req, 90.0);
                } catch (const std::exception& e) {
                    Log::error("[Executor] LLM请求异常: {}", e.what());
                    networkError = true;
                }

                if (networkError) {
                    if (iter < 3) {
                        Log::warn("[Executor] 网络异常重试...");
                        using namespace std::chrono_literals;
                        co_await drogon::sleepCoro(drogon::app().getLoop(), 1s);
                        --iter;
                        continue;
                    }
                    co_return std::nullopt;
                }
                auto json = resp->getJsonObject();

                if (resp->getStatusCode() != drogon::k200OK || !json || !json->isMember("choices")) {
                    int status = static_cast<int>(resp->getStatusCode());
                    std::string body = std::string(resp->getBody()).substr(0, 500);
                    Log::error("[Executor] LLM失败: status={} body={}", status, body);

                    // 重试（503/429/500）
                    if ((status == 503 || status == 429 || status == 500) && iter < 3) {
                        Log::warn("[Executor] 重试...");
                        using namespace std::chrono_literals;
                        co_await drogon::sleepCoro(drogon::app().getLoop(), 1s);
                        --iter;
                        continue;
                    }
                    co_return std::nullopt;
                }

                ApiClient::logUsage(*json, apiConfig.model);

                const auto &message = (*json)["choices"][0]["message"];
                ReplyDecision decision;

                // 工具调用
                if (message.isMember("tool_calls") && !message["tool_calls"].empty()) {
                    bool hasDecision = false;

                    // 构建 assistant 消息
                    Json::Value assistantMsg;
                    assistantMsg["role"] = "assistant";
                    assistantMsg["content"] = message.isMember("content")
                                                  ? message["content"].asString()
                                                  : "";

                    Json::Value toolCallsArray(Json::arrayValue);
                    for (const auto &tc: message["tool_calls"]) {
                        Json::Value tcEntry;
                        tcEntry["id"] = tc["id"].asString();
                        tcEntry["type"] = "function";
                        tcEntry["function"]["name"] = tc["function"]["name"].asString();
                        tcEntry["function"]["arguments"] = tc["function"]["arguments"].asString();
                        toolCallsArray.append(tcEntry);
                    }
                    assistantMsg["tool_calls"] = toolCallsArray;
                    messages.append(assistantMsg);

                    // 处理工具
                    for (const auto &tc: message["tool_calls"]) {
                        std::string name = tc["function"]["name"].asString();
                        std::string id = tc["id"].asString();
                        std::string argsStr = tc["function"]["arguments"].asString();

                        Log::info("[Executor] 工具: {}", name);

                        if (name == "no_reply") {
                            decision.shouldReply = false;
                            hasDecision = true;
                        } else if (name == "reply") {
                            Json::Value args;
                            Json::Reader().parse(argsStr, args);
                            if (args.isMember("content")) {
                                decision.shouldReply = true;
                                decision.content = finalizeContent(
                                    args["content"].asString(), accumulatedCQCodes);
                                hasDecision = true;
                            }
                        } else if (name == "reply_with_quote") {
                            Json::Value args;
                            Json::Reader().parse(argsStr, args);
                            if (args.isMember("content") && args.isMember("message_id")) {
                                decision.shouldReply = true;
                                decision.content = "[CQ:reply,id=" +
                                                   args["message_id"].asString() + "]" +
                                                   finalizeContent(args["content"].asString(),
                                                                   accumulatedCQCodes);
                                hasDecision = true;
                            }
                        } else {
                            // 其他工具
                            Json::Value args;
                            Json::Reader().parse(argsStr, args);

                            std::string result = co_await registry.executeTool(name, args, groupId);
                            Log::debug("[Executor] 工具结果: {}", result);

                            // 记录CQ码工具的结果
                            if (name == "send_sticker" || name == "send_face" || name == "send_image") {
                                accumulatedCQCodes += result;
                            }

                            Json::Value toolMsg;
                            toolMsg["role"] = "tool";
                            toolMsg["tool_call_id"] = id;
                            toolMsg["content"] = result;
                            messages.append(toolMsg);
                        }
                    }

                    if (hasDecision) {
                        co_return decision;
                    }
                    continue; // 继续下一轮
                }

                // 无工具调用，直接返回文本
                if (message.isMember("content") && !message["content"].isNull()) {
                    decision.shouldReply = true;
                    decision.content = cleanReplyContent(message["content"].asString());
                    co_return decision;
                }

                decision.shouldReply = false;
                co_return decision;
            }

            Log::error("[Executor] 达到最大迭代次数");
            co_return std::nullopt;
        }

        /// @brief 思考模式执行（两阶段：思考 → 执行）
        drogon::Task<std::optional<ReplyDecision> > executeWithThinking(
            Json::Value messages, const uint64_t groupId, const int maxLength) {
            const auto &config = Config::instance();

            Log::info("[Executor] 思考模式 - Step 1: 分析");

            // Step 1: 思考模型分析
            const std::optional<std::string> thinkingResult = co_await executeThinking(messages, maxLength);

            if (!thinkingResult || thinkingResult->empty()) {
                Log::warn("[Executor] 思考模型返回空，fallback");
                co_return co_await executeWithAgent(
                    messages, config.executor, config.executorParams, groupId);
            }

            Log::debug("[Executor] 思考结果: {}...",
                       thinkingResult->substr(0, std::min<size_t>(100, thinkingResult->length())));

            // Step 2: 注入思考结果，执行工具调用
            Log::info("[Executor] 思考模式 - Step 2: 执行");

            Json::Value thinkingMsg;
            thinkingMsg["role"] = "assistant";
            thinkingMsg["content"] = "【思考分析】\n" + *thinkingResult;
            messages.append(thinkingMsg);

            Json::Value execMsg;
            execMsg["role"] = "user";
            execMsg["content"] = "根据以上分析，调用工具发送回复。";
            messages.append(execMsg);

            co_return co_await executeWithAgent(
                messages, config.executor, config.executorParams, groupId);
        }
    }

    /// @brief 清理模型输出的污染内容（think标签、tool_call标签、DSML标签等）
    std::string cleanReplyContent(const std::string &text) {
        std::string result = text;

        // 移除 <tool_call>...</tool_call> 块
        static const std::regex toolCallTag("<tool_call>[^<]*</tool_call>", std::regex::icase);
        result = std::regex_replace(result, toolCallTag, "");

        // 移除单独的 <tool_call> 或 </tool_call>
        static const std::regex toolCallOpen("</?tool_call>", std::regex::icase);
        result = std::regex_replace(result, toolCallOpen, "");

        // 移除 DSML 工具调用块（Qwen 系列模型的格式，兼容全角竖线｜）
        static const std::regex dsmlInvoke(
            R"(<[|｜]*DSML[|｜]+invoke[^>]*>[\s\S]*?<\/[|｜]*DSML[|｜]+invoke>)", std::regex::icase);
        result = std::regex_replace(result, dsmlInvoke, "");

        // 移除残留的 DSML 标签（tool_calls、parameter 等）
        static const std::regex dsmlTag(R"(<\/?[|｜]*DSML[|｜]+[^>]*>)", std::regex::icase);
        result = std::regex_replace(result, dsmlTag, "");

        // 移除 </think>
        static const std::regex thinkTag("</think>", std::regex::icase);
        result = std::regex_replace(result, thinkTag, "");

        // 移除 function(...) 调用残留
        static const std::regex funcCall(
            R"((reply_with_quote|reply|no_reply)\s*\([^)]*\))");
        result = std::regex_replace(result, funcCall, "");

        // 去除首尾空白
        static const std::regex trimSpace("^\\s+|\\s+$");
        result = std::regex_replace(result, trimSpace, "");
        // 压缩多余换行
        static const std::regex multiNewline("\n{3,}");
        result = std::regex_replace(result, multiNewline, "\n\n");

        if (result.empty() && !text.empty()) {
            return text;
        }

        return result;
    }


    drogon::Task<std::optional<ReplyDecision> > execute(
        const ChatRecordManager &chatRecords,
        const MemoryManager &memory,
        const RouterDecision &decision) {
        Log::info("[Executor] 开始执行 | thinking={} | priority={} | maxLength={}",
                  decision.enableThinking, decision.isPriority, decision.maxLength);

        const auto &config = Config::instance();
        const Json::Value messages = co_await buildPrompt(chatRecords, memory, decision);

        // 思考模式：两阶段执行
        if (decision.enableThinking) {
            co_return co_await executeWithThinking(messages, chatRecords.getGroupId(), decision.maxLength);
        }
        // 普通模式：单次执行
        co_return co_await executeWithAgent(messages, config.executor, config.executorParams, chatRecords.getGroupId());
    }
}
