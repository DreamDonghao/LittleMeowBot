/// @file ExecutorAgent.cpp
/// @brief Executor Agent - 实现

#include <agent/ExecutorAgent.hpp>
#include <algorithm>
#include <chrono>
#include <config/Config.hpp>
#include <drogon/HttpAppFramework.h>
#include <fmt/core.h>
#include <ranges>
#include <regex>
#include <service/ChatRecordManager.hpp>
#include <service/LlmClient.hpp>
#include <service/MessageRecall.hpp>
#include <service/PromptService.hpp>
#include <service/ToolRegistry.hpp>
#include <spdlog/spdlog.h>
#include <storage/AffinityStore.hpp>
#include <string>
#include <tuple>
#include <unordered_map>
#include <util/CommonUtil.hpp>
#include <util/HttpUtil.hpp>
#include <util/JsonUtil.hpp>
#include <util/Logger.hpp>
#include <utility>
#include <vector>

namespace insoulforge {
    namespace {
        /// @brief 工具调用循环最大轮数（防止模型无限循环调用工具）
        constexpr int kMaxToolRounds = 6;

        /// @brief 网络异常/临时性 HTTP 错误的最大重试次数
        constexpr int kMaxRetries = 3;

        /// @brief 重试间隔
        constexpr std::chrono::seconds kRetryDelay{1};

        /// @brief LLM 请求超时（秒）
        constexpr double kLlmTimeoutSeconds = 90.0;

        /// @brief 日志中错误响应体的截断长度
        constexpr size_t kErrorBodyMaxChars = 500;

        /// @brief 日志中思考结果的预览长度
        constexpr size_t kThinkingPreviewChars = 100;

        // ==================== Prompt 构建 ====================

        /// @brief 获取系统提示词（私聊与群聊使用各自的人设提示词，差异行按会话类型拼接）
        std::string getSystemPrompt(const RouterDecision &decision) {
            std::string prompt = decision.isPrivate ? PromptService::getExecutorPrivateSystemPrompt()
                                                    : PromptService::getExecutorSystemPrompt();

            prompt += fmt::format("\n\n【回复要求】\n"
                                  "- 字数限制: {} 字\n"
                                  "- 要有自己的判断，不要别人说什么就做什么\n",
              decision.maxLength);
            if (decision.isPrivate) {
                prompt += "- 这是私聊，直接回复即可，不要@对方或引用回复\n";
            } else {
                prompt += "- @人格式: @[QQ:123456]\n"
                          "- 禁言要核实实际情况再决定\n";
            }
            prompt += "- 【重要】表情/图片的CQ码必须通过工具获取(send_sticker/send_face/send_image)。"
                      "先调工具，拿到结果后把返回的[CQ:image...]或[CQ:face...]原样拼接到reply的content中。"
                      "禁止自己编造假CQ标签，工具返回什么就复制什么\n";

            if (decision.isPriority) {
                prompt += decision.isPrivate ? "\n【重要】这是紧急问题，必须回复！"
                                             : "\n【重要】这是@提及或紧急问题，必须回复！";
            }

            return prompt;
        }

        /// @brief 获取思考模型系统提示词
        std::string getThinkingSystemPrompt(int maxLength) {
            std::string prompt = PromptService::getExecutorSystemPrompt();

            prompt += fmt::format("\n\n【思考任务】\n"
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

        /// @brief 解析记录的 content 字段（由本服务写入的 JSON 对象字符串）
        [[nodiscard]] json parseRecordContent(const json &record) {
            json parsed;
            if (!tryParseJson(getStr(record, "content"), parsed) || !parsed.is_object()) {
                return {};
            }
            return parsed;
        }

        /// @brief 按 UTF-8 字符边界截断，超长时末尾追加省略号
        [[nodiscard]] std::string truncateUtf8(const std::string &text, const size_t maxChars) {
            size_t i = 0;
            size_t count = 0;
            while (i < text.size() && count < maxChars) {
                if (const auto c = static_cast<unsigned char>(text[i]); (c & 0xE0) == 0xC0) {
                    i += 2;
                } else if ((c & 0xF0) == 0xE0) {
                    i += 3;
                } else if ((c & 0xF8) == 0xF0) {
                    i += 4;
                } else {
                    i += 1;
                }
                ++count;
            }
            if (i >= text.size())
                return text;
            return text.substr(0, i) + "…";
        }

        /// @brief 处理较早的单条消息记录：删除 images 字段、截断 text 字段后作为数组元素返回
        [[nodiscard]] json processOlderRecord(const json &record) {
            json content = parseRecordContent(record);
            if (!content.is_object())
                return content; // 历史存量可能是纯文本，解析失败原样保留
            content.erase("images");
            if (content.contains("text") && content["text"].is_string()) {
                constexpr size_t kOldRecordMaxChars = 500;
                content["text"] = truncateUtf8(content["text"].get<std::string>(), kOldRecordMaxChars);
            }
            return content;
        }

        /// @brief 注入发送者当前好感度（读时注入，保证 LLM 看到的永远是最新值）
        /// @details qq 非数字（机器人记录的 "self"）跳过；映射中不存在的用户按 0（中立）注入
        [[nodiscard]] json injectAffinity(json content, const std::unordered_map<uint64_t, int> &affinityMap) {
            if (!content.is_object() || !content.contains("sender"))
                return content;
            if (const uint64_t qq = parseUInt64(getStr(content["sender"], "qq")); qq > 0) {
                const auto it = affinityMap.find(qq);
                content["sender"]["affinity"] = it != affinityMap.end() ? it->second : 0;
            }
            return content;
        }

        /// @brief 把记录范围经 transform 逐条处理后拼为 JSON 数组字符串
        template<std::ranges::input_range Range, typename Transform>
        [[nodiscard]] std::string joinRecords(const Range &records, const Transform &transform) {
            json array = json::array();
            for (const auto &record: records) {
                array.push_back(transform(record));
            }
            return dumpJson(array);
        }

        /// @brief 构建聊天记录上下文（窗口内，旧 → 新）：
        /// 最新 kRecentRecordCount 条原样保留，更早的每条 text 截断到 500 字；
        /// 每条 sender 注入当前好感度 affinity；最近记录按 message_id 注入召回的长期记忆 memories
        /// （同一条记忆被多条消息命中时只挂在相似度最高的那条消息上）
        std::string buildChatContextText(const ChatRecordManager &chatRecords) {
            const auto records = chatRecords.getRecords(); // 旧 → 新
            const size_t totalRecords = records.size();
            const size_t olderCount = totalRecords > kRecentRecordCount ? totalRecords - kRecentRecordCount : 0;
            const auto olderRecords = records | std::views::take(olderCount);
            const auto recentRecords = records | std::views::drop(olderCount);

            const auto affinityMap = AffinityStore::getAffinityMap(chatRecords.getSessionId());

            // 最近记录的召回缓存（下标与 recentRecords 旧 → 新对齐），并统计每条记忆的最佳归属消息
            std::vector<std::vector<MessageRecallHit>> recentHits;
            std::unordered_map<int64_t, std::pair<float, size_t>> bestOwner; // 记忆 id → (最高相似度, 消息下标)
            for (const auto &record: recentRecords) {
                std::vector<MessageRecallHit> hits;
                const std::string messageIdStr = getStr(parseRecordContent(record), "message_id");
                if (const uint64_t messageId = parseUInt64(messageIdStr); messageId > 0)
                    hits = MessageRecall::getHits(chatRecords.getSessionId(), messageId);
                for (const auto &hit: hits) {
                    if (auto [it, inserted] = bestOwner.try_emplace(hit.id, hit.similarity, recentHits.size());
                      !inserted && hit.similarity > it->second.first)
                        it->second = {hit.similarity, recentHits.size()};
                }
                recentHits.push_back(std::move(hits));
            }

            std::string context;

            // 处理更早的对话
            if (olderCount > 0) {
                context += "【更早对话】\n" + joinRecords(olderRecords, [&affinityMap](const json &record) {
                    return injectAffinity(processOlderRecord(record), affinityMap);
                }) + "\n\n";
            }

            // 处理最近对话（注入好感度与召回记忆）
            size_t recentIndex = 0;
            context += "【最近对话】\n" + joinRecords(recentRecords, [&](const json &record) {
                const size_t index = recentIndex++;
                json content = injectAffinity(parseRecordContent(record), affinityMap);
                if (!recentHits[index].empty()) {
                    json memories = json::array();
                    for (const auto &hit: recentHits[index]) {
                        if (bestOwner.at(hit.id).second == index)
                            memories.push_back(hit.content);
                    }
                    if (!memories.empty())
                        content["memories"] = memories;
                }
                return content;
            });
            return context;
        }

        /// @brief 构建 Executor 消息列表（system + 单条 user）
        /// @details 短期记忆+聊天记录+回复要求合并为单条 user 消息，避免连续多条 user（部分 OpenAI 兼容后端不支持）
        [[nodiscard]] json buildPrompt(
          const ChatRecordManager &chatRecords, const MemoryManager &memory, const RouterDecision &decision) {
            json messages = json::array();

            json systemMsg;
            systemMsg["role"] = "system";
            systemMsg["content"] = getSystemPrompt(decision);
            messages.push_back(systemMsg);

            std::string userContent;
            if (std::string shortMemory = memory.getMemory(); !shortMemory.empty()) {
                userContent += fmt::format("【短期记忆】\n{}\n\n结合记忆处理下面的对话。\n\n", shortMemory);
            }
            userContent += buildChatContextText(chatRecords);
            userContent += fmt::format("\n\n【回复要求】\n语气: {}\n字数限制: {} 字\n原因: {}", decision.tone,
              decision.maxLength, decision.reason);

            json userMsg;
            userMsg["role"] = "user";
            userMsg["content"] = userContent;
            messages.push_back(userMsg);

            return messages;
        }

        // ==================== LLM 请求 ====================

        /// @brief 是否为可重试的临时性 HTTP 状态码
        [[nodiscard]] bool isRetryableStatus(const int status) {
            return status == 503 || status == 429 || status == 500;
        }

        /// @brief 发送一次 chat completion 请求并校验响应，网络异常或临时性 HTTP 错误（503/429/500）时重试
        /// @param label 日志标签（"LLM" / "思考模型"）
        /// @param usageRole 用量记录中的角色标识（executor / executorThinking）
        /// @param apiConfig LLM API 配置
        /// @param params 采样参数
        /// @param messages 消息列表
        /// @param tools 工具定义（null 表示不附带）
        /// @param sessionId 会话 ID
        /// @return 校验通过的响应 JSON；不可恢复错误或重试耗尽时返回 std::nullopt
        drogon::Task<std::optional<json>> requestChat(const std::string &label, const std::string &usageRole,
          const LLMApiConfig &apiConfig, const LLMModelParams &params, const json &messages, const json &tools,
          const uint64_t sessionId) {
            Logger::session(sessionId).debug("[Executor] {}: {}", label, apiConfig.model);

            for (int attempt = 0;; ++attempt) {
                const json body = LlmClient::buildChatRequestBody(apiConfig, params, messages, tools);
                const auto resp = co_await HttpUtil::send("[Executor]", apiConfig.baseUrl, apiConfig.path, drogon::Post,
                  body, apiConfig.apiKey, kLlmTimeoutSeconds, sessionId);

                if (!resp) {
                    Logger::session(sessionId).warn("[Executor] {}网络异常", label);
                } else if (const auto json = LlmClient::validChatJson(*resp)) {
                    LlmClient::logUsage(*json, apiConfig.model, usageRole, sessionId);
                    co_return json;
                } else {
                    const int status = static_cast<int>((*resp)->getStatusCode());
                    const std::string respBody = std::string((*resp)->getBody()).substr(0, kErrorBodyMaxChars);
                    Logger::session(sessionId).error("[Executor] {}失败: status={} body={}", label, status, respBody);
                    if (!isRetryableStatus(status)) {
                        co_return std::nullopt; // 不可恢复的错误（鉴权、参数等）
                    }
                    Logger::session(sessionId).warn("[Executor] {}临时性错误", label);
                }

                if (attempt >= kMaxRetries) {
                    co_return std::nullopt; // 重试耗尽
                }
                Logger::session(sessionId).warn("[Executor] {}第 {}/{} 次重试", label, attempt + 1, kMaxRetries);
                co_await drogon::sleepCoro(drogon::app().getLoop(), kRetryDelay);
            }
        }

        // ==================== 工具调用 ====================

        /// @brief 结果为 CQ 码、需在产出 reply 时自动拼入正文的工具
        [[nodiscard]] bool isCqCodeTool(const std::string &name) {
            return name == "send_sticker" || name == "send_face" || name == "send_image";
        }

        /// @brief 清理回复内容，并在模型忘记拼接 CQ 码时自动补上已获取的 CQ 码
        [[nodiscard]] std::string finalizeContent(
          const std::string &rawContent, const std::string &accumulatedCQCodes) {
            std::string content = cleanReplyContent(rawContent);
            if (!accumulatedCQCodes.empty() && content.find("[CQ:") == std::string::npos) {
                content += accumulatedCQCodes;
            }
            return content;
        }

        /// @brief 处理终端工具（no_reply / reply / reply_with_quote），把决策写入 decision
        /// @return 是否为终端工具；参数缺失时不产生决策也不回传工具结果（终端工具不降级为普通工具执行）
        [[nodiscard]] bool applyTerminalTool(
          const std::string &name, const json &args, const std::string &accumulatedCQCodes, ReplyDecision &decision) {
            if (name == "no_reply") {
                decision.shouldReply = false;
                return true;
            }
            if (name == "reply") {
                if (args.contains("content")) {
                    decision.shouldReply = true;
                    decision.content = finalizeContent(jsonToString(args["content"]), accumulatedCQCodes);
                }
                return true;
            }
            if (name == "reply_with_quote") {
                if (args.contains("content") && args.contains("message_id")) {
                    decision.shouldReply = true;
                    decision.content = fmt::format("[CQ:reply,id={}]", jsonToString(args["message_id"])) +
                                       finalizeContent(jsonToString(args["content"]), accumulatedCQCodes);
                }
                return true;
            }
            return false; // 非终端工具，交由 ToolRegistry 执行
        }

        /// @brief 把模型返回的 tool_calls 转为需回传以补全上下文的 assistant 消息
        [[nodiscard]] json buildAssistantToolCallMessage(const json &message) {
            json assistantMsg;
            assistantMsg["role"] = "assistant";
            assistantMsg["content"] = message.contains("content") ? jsonToString(message["content"]) : "";

            json toolCalls = json::array();
            for (const auto &toolCall: message["tool_calls"]) {
                json entry;
                entry["id"] = jsonToString(atOrNull(toolCall, "id"));
                entry["type"] = "function";
                entry["function"]["name"] = jsonToString(atOrNull(atOrNull(toolCall, "function"), "name"));
                entry["function"]["arguments"] = jsonToString(atOrNull(atOrNull(toolCall, "function"), "arguments"));
                toolCalls.push_back(entry);
            }
            assistantMsg["tool_calls"] = toolCalls;
            return assistantMsg;
        }

        /// @brief 逐个处理本轮工具调用：终端工具直接产出回复决策；其余工具经 ToolRegistry 执行并把结果
        /// 作为 tool 消息回传，CQ 码类工具的结果累积备用
        /// @return 命中终端工具时返回决策（同轮多个终端工具以最后一个为准），否则返回空（继续下一轮）
        drogon::Task<std::optional<ReplyDecision>> processToolCalls(
          const json &message, json &messages, std::string &accumulatedCQCodes, const uint64_t sessionId) {
            ReplyDecision decision;
            bool hasDecision = false;

            for (const auto &toolCall: message["tool_calls"]) {
                const std::string name = jsonToString(atOrNull(atOrNull(toolCall, "function"), "name"));
                Logger::session(sessionId).info("[Executor] 工具: {}", name);

                json args;
                std::ignore = tryParseJson(jsonToString(atOrNull(atOrNull(toolCall, "function"), "arguments")), args);

                if (applyTerminalTool(name, args, accumulatedCQCodes, decision)) {
                    hasDecision = true; // 终端工具：结束本轮，不回传工具结果
                    continue;
                }

                const std::string result = co_await ToolRegistry::instance().executeTool(name, args, sessionId);
                Logger::session(sessionId).debug("[Executor] 工具结果: {}", result);
                if (isCqCodeTool(name)) {
                    accumulatedCQCodes += result;
                }

                json toolMsg;
                toolMsg["role"] = "tool";
                toolMsg["tool_call_id"] = jsonToString(atOrNull(toolCall, "id"));
                toolMsg["content"] = result;
                messages.push_back(toolMsg);
            }

            if (hasDecision) {
                co_return decision;
            }
            co_return std::nullopt;
        }

        // ==================== 执行流程 ====================

        /// @brief 执行思考模型（不带 tools，产出回复思路）
        /// @return 思考模型输出（优先取 reasoning_content 字段）；请求失败返回 std::nullopt
        drogon::Task<std::optional<std::string>> executeThinking(
          const json &messages, const int maxLength, const uint64_t sessionId) {
            const auto &config = Config::instance();

            // 仅替换 system prompt 为思考任务指令，其余消息原样保留
            json thinkingMessages = json::array();
            json systemMsg;
            systemMsg["role"] = "system";
            systemMsg["content"] = getThinkingSystemPrompt(maxLength);
            thinkingMessages.push_back(systemMsg);
            for (size_t i = 1; i < messages.size(); ++i) {
                thinkingMessages.push_back(messages[i]);
            }

            const auto respJson = co_await requestChat("思考模型", "executorThinking", config.executorThinking,
              config.executorThinkingParams, thinkingMessages, {}, sessionId);
            if (!respJson) {
                co_return std::nullopt;
            }

            // DeepSeek Reasoner 等模型将分析过程放在 reasoning_content，优先取用
            const json &message = atOrNull((*respJson)["choices"][0], "message");
            std::string content;
            if (message.contains("reasoning_content") && !message["reasoning_content"].is_null()) {
                content = jsonToString(message["reasoning_content"]);
            } else if (message.contains("content") && !message["content"].is_null()) {
                content = jsonToString(message["content"]);
            }
            co_return content;
        }

        /// @brief Agent 模式执行（带 tools）：循环「请求模型 → 处理工具调用」，直到产出回复决策或达最大轮数
        drogon::Task<std::optional<ReplyDecision>> executeWithAgent(json messages, const uint64_t sessionId) {
            const auto &config = Config::instance();
            const json tools = ToolRegistry::instance().getAllTools();
            if (tools.empty()) {
                Logger::session(sessionId).error("[Executor] 未注册工具");
                co_return std::nullopt;
            }

            std::string accumulatedCQCodes; // 跨轮累积 CQ 码，产出 reply 时自动拼入正文

            for (int round = 0; round < kMaxToolRounds; ++round) {
                const auto respJson = co_await requestChat(
                  "LLM", "executor", config.executor, config.executorParams, messages, tools, sessionId);
                if (!respJson) {
                    co_return std::nullopt;
                }

                const json &message = atOrNull((*respJson)["choices"][0], "message");

                // 无工具调用：文本即回复；无文本视为不回复
                if (!message.contains("tool_calls") || !message["tool_calls"].is_array() ||
                    message["tool_calls"].empty()) {
                    ReplyDecision decision;
                    if (message.contains("content") && !message["content"].is_null()) {
                        decision.shouldReply = true;
                        decision.content = cleanReplyContent(jsonToString(message["content"]));
                    }
                    co_return decision;
                }

                // 有工具调用：回传 assistant 消息后逐个处理，未命中终端工具则继续下一轮
                messages.push_back(buildAssistantToolCallMessage(message));
                if (const auto decision = co_await processToolCalls(message, messages, accumulatedCQCodes, sessionId)) {
                    co_return decision;
                }
            }

            Logger::session(sessionId).error("[Executor] 达到最大迭代次数");
            co_return std::nullopt;
        }

        /// @brief 思考模式执行（两阶段：思考模型分析 → 执行模型带工具生成回复；思考失败时回退普通模式）
        drogon::Task<std::optional<ReplyDecision>> executeWithThinking(
          json messages, const int maxLength, const uint64_t sessionId) {
            Logger::session(sessionId).info("[Executor] 思考模式 - Step 1: 分析");

            const std::optional<std::string> thinkingResult = co_await executeThinking(messages, maxLength, sessionId);
            if (!thinkingResult || thinkingResult->empty()) {
                Logger::session(sessionId).warn("[Executor] 思考模型返回空，fallback");
                co_return co_await executeWithAgent(std::move(messages), sessionId);
            }

            Logger::session(sessionId).debug("[Executor] 思考结果: {}...",
              thinkingResult->substr(0, std::min(kThinkingPreviewChars, thinkingResult->length())));

            // Step 2: 注入思考结果，交由执行模型带工具生成回复
            Logger::session(sessionId).info("[Executor] 思考模式 - Step 2: 执行");

            json thinkingMsg;
            thinkingMsg["role"] = "assistant";
            thinkingMsg["content"] = "【思考分析】\n" + *thinkingResult;
            messages.push_back(thinkingMsg);

            json execMsg;
            execMsg["role"] = "user";
            execMsg["content"] = "根据以上分析，调用工具发送回复。";
            messages.push_back(execMsg);

            co_return co_await executeWithAgent(std::move(messages), sessionId);
        }
    } // namespace

    /// @brief 清理模型输出的污染内容（think标签、tool_call标签、DSML标签等）
    std::string cleanReplyContent(const std::string &text) {
        // 按序应用的净化规则：Qwen 的 DSML 标签、DeepSeek 的 think 标签、tool_call 残留等
        static const std::vector<std::pair<std::regex, std::string>> rules = {
          // <tool_call>...</tool_call> 块及残留的独立标签
          {std::regex("<tool_call>[^<]*</tool_call>", std::regex::icase), ""},
          {std::regex("</?tool_call>", std::regex::icase), ""},
          // DSML 工具调用块（兼容全角竖线｜）及残留标签
          {std::regex(R"(<[|｜]*DSML[|｜]+invoke[^>]*>[\s\S]*?<\/[|｜]*DSML[|｜]+invoke>)", std::regex::icase), ""},
          {std::regex(R"(<\/?[|｜]*DSML[|｜]+[^>]*>)", std::regex::icase), ""},
          // </think> 与 function(...) 调用残留
          {std::regex("</think>", std::regex::icase), ""},
          {std::regex(R"((reply_with_quote|reply|no_reply)\s*\([^)]*\))"), ""},
          // 压缩空白：去除首尾空白与 3 个以上连续换行
          {std::regex("^\\s+|\\s+$"), ""},
          {std::regex("\n{3,}"), "\n\n"},
        };

        std::string result = text;
        for (const auto &[pattern, replacement]: rules) {
            result = std::regex_replace(result, pattern, replacement);
        }

        // 清理后为空但原文非空：保留原文，宁可原样输出也不发空消息
        if (result.empty() && !text.empty()) {
            return text;
        }
        return result;
    }

    drogon::Task<std::optional<ReplyDecision>> execute(
      const ChatRecordManager &chatRecords, const MemoryManager &memory, const RouterDecision &decision) {
        const uint64_t sessionId = chatRecords.getSessionId();
        Logger::session(sessionId).info("[Executor] 开始执行 | thinking={} | priority={} | maxLength={}",
          decision.enableThinking, decision.isPriority, decision.maxLength);

        const json messages = buildPrompt(chatRecords, memory, decision);

        // 思考模式两阶段执行（思考模型分析 → 执行模型生成），普通模式单阶段直接生成
        if (decision.enableThinking) {
            co_return co_await executeWithThinking(messages, decision.maxLength, sessionId);
        }
        co_return co_await executeWithAgent(messages, sessionId);
    }
} // namespace insoulforge
