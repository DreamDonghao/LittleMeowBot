/// @file ExecutorAgent.cpp
/// @brief Executor Agent - 实现

#include <agent/ExecutorAgent.hpp>
#include <config/Config.hpp>
#include <fmt/core.h>
#include <model/QQMessage.hpp>
#include <ranges>
#include <regex>
#include <service/ChatRecordManager.hpp>
#include <service/LlmClient.hpp>
#include <service/MessageRecall.hpp>
#include <service/PromptService.hpp>
#include <service/ToolRegistry.hpp>
#include <spdlog/spdlog.h>
#include <storage/AffinityStore.hpp>
#include <storage/SessionStore.hpp>
#include <string>
#include <tuple>
#include <unordered_map>
#include <util/CommonUtil.hpp>
#include <util/JsonUtil.hpp>
#include <util/Logger.hpp>
#include <utility>
#include <vector>

namespace insoulforge {
    namespace {
        /// @brief 工具调用循环最大轮数（防止模型无限循环调用工具）
        constexpr int kMaxToolRounds = 6;

        /// @brief 获取系统提示词（私聊与群聊使用各自的人设提示词，差异行按会话类型拼接）
        std::string getSystemPrompt(const RouterDecision &decision) {
            std::string prompt = decision.isPrivate ? PromptService::getExecutorPrivateSystemPrompt()
                                                    : PromptService::getExecutorSystemPrompt();

            prompt += "\n\n【回复要求】\n"
                      "- 要有自己的判断，不要别人说什么就做什么\n";
            if (decision.isPrivate) {
                prompt += "- 这是私聊，直接回复即可，不要@对方或引用回复\n";
            } else {
                prompt += "- @人格式: @[QQ:123456]\n"
                          "- 禁言要核实实际情况再决定\n";
            }
            prompt += "- 【重要】表情/图片的CQ码必须通过工具获取(send_sticker/send_face/send_image)。"
                      "先调工具，拿到结果后把返回的[CQ:image...]或[CQ:face...]原样拼接到reply的content中。"
                      "禁止自己编造假CQ标签，工具返回什么就复制什么。"
                      "send_sticker 例外：调用后表情包直接发出（独立消息，不拼进reply），"
                      "若表情包就是全部回复，发完调no_reply收尾\n";

            if (decision.isPriority) {
                prompt += decision.isPrivate ? "\n【重要】这是紧急问题，必须回复！"
                                             : "\n【重要】这是@提及或紧急问题，必须回复！";
            }

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

        /// @brief 聊天记录上下文（窗口内，旧 → 新）
        struct ChatContext {
            json earlier = json::array(); // 更早记录：去 images、text 截断到 500 字
            json recent = json::array(); // 最近记录：注入好感度与召回的长期记忆
        };

        /// @brief 构建聊天记录上下文：
        /// 最新 kRecentRecordCount 条原样保留，更早的每条 text 截断到 500 字；
        /// 每条 sender 注入当前好感度 affinity；最近记录按 message_id 注入召回的长期记忆 memories
        /// （同一条记忆被多条消息命中时只挂在相似度最高的那条消息上）
        ChatContext buildChatContext(const ChatRecordManager &chatRecords) {
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

            ChatContext context;

            // 处理更早的对话
            for (const auto &record: olderRecords) {
                context.earlier.push_back(injectAffinity(processOlderRecord(record), affinityMap));
            }

            // 处理最近对话（注入好感度与召回记忆）
            size_t recentIndex = 0;
            for (const auto &record: recentRecords) {
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
                context.recent.push_back(std::move(content));
            }
            return context;
        }

        /// @brief 把短期记忆文本（每行一条）拆为字符串数组，跳过空白行
        [[nodiscard]] json splitMemoryLines(const std::string &text) {
            json lines = json::array();
            for (size_t start = 0; start < text.size();) {
                const size_t end = text.find('\n', start);
                const size_t stop = end == std::string::npos ? text.size() : end;
                if (std::string line = trim(text.substr(start, stop - start)); !line.empty())
                    lines.push_back(std::move(line));
                start = stop + 1;
            }
            return lines;
        }

        /// @brief 构建 Executor 消息列表（system + 单条 user）
        /// @details 上下文组装为键序固定的单个 JSON 对象（ordered_json 按插入顺序序列化）作为单条
        /// user 消息：session → short_term_memory → earlier_conversation → recent_conversation →
        /// response_requirements，
        /// 避免连续多条 user（部分 OpenAI 兼容后端不支持）
        [[nodiscard]] json buildPrompt(
          const ChatRecordManager &chatRecords, const MemoryManager &memory, const RouterDecision &decision) {
            json messages = json::array();

            json systemMsg;
            systemMsg["role"] = "system";
            systemMsg["content"] = getSystemPrompt(decision);
            messages.push_back(systemMsg);

            auto [earlierRecords, recentRecords] = buildChatContext(chatRecords);

            json context;
            // 会话详情放最前：群名/群号供模型直接取用，无需再向工具查询
            const uint64_t sessionId = chatRecords.getSessionId();
            json sessionInfo;
            if (QQMessage::isPrivateSession(sessionId)) {
                sessionInfo["type"] = "private";
                sessionInfo["qq"] = QQMessage::parseSessionTarget(sessionId).second;
            } else {
                sessionInfo["type"] = "group";
                sessionInfo["group_id"] = sessionId;
                if (const std::string groupName = SessionStore::getSessionName(sessionId); !groupName.empty()) {
                    sessionInfo["group_name"] = groupName;
                }
            }
            context["session"] = std::move(sessionInfo);
            context["short_term_memory"] = splitMemoryLines(memory.getMemory());
            context["earlier_conversation"] = std::move(earlierRecords);
            context["recent_conversation"] = std::move(recentRecords);
            json requirements;
            requirements["tone"] = decision.tone;
            requirements["max_length"] = decision.maxLength;
            requirements["reply_reason"] = decision.reason;
            context["response_requirements"] = std::move(requirements);

            json userMsg;
            userMsg["role"] = "user";
            userMsg["content"] = dumpJson(context);
            messages.push_back(userMsg);

            return messages;
        }

        // ==================== 工具调用 ====================

        /// @brief 结果为内联 CQ 码、需在产出 reply 时自动拼入正文的工具（send_sticker 已直接发送，不经此路径）
        [[nodiscard]] bool isCqCodeTool(const std::string &name) { return name == "send_face" || name == "send_image"; }

        /// @brief 清理回复内容，并在模型忘记拼接 CQ 码时自动补上已获取的 CQ 码
        [[nodiscard]] std::string finalizeContent(
          const std::string &rawContent, const std::string &accumulatedCQCodes) {
            std::string content = cleanReplyContent(rawContent);
            if (!accumulatedCQCodes.empty() && content.find("[CQ:") == std::string::npos) {
                content += accumulatedCQCodes;
            }
            return content;
        }

        /// @brief 处理回复工具（no_reply / reply / reply_with_quote），把决策写入 decision
        /// @return 是否为回复工具；参数缺失时不产生决策也不回传工具结果（回复工具不降级为普通工具执行）
        [[nodiscard]] bool applyReplyTool(
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
            return false; // 非回复工具，交由 ToolRegistry 执行
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

        /// @brief 逐个处理本轮工具调用：回复工具直接产出回复决策；其余工具经 ToolRegistry 执行并把结果
        /// 作为 tool 消息回传，CQ 码类工具的结果累积备用
        /// @return {回复工具决策（同轮多个以最后一个为准，未命中为 nullopt）, 回传工具结果后的消息列表, 累积的 CQ 码}
        drogon::Task<std::tuple<std::optional<ReplyDecision>, json, std::string>> processToolCalls(
          json message, json messages, std::string accumulatedCQCodes, const uint64_t sessionId) {
            ReplyDecision decision;
            bool hasDecision = false;

            for (const auto &toolCall: message["tool_calls"]) {
                const std::string name = jsonToString(atOrNull(atOrNull(toolCall, "function"), "name"));
                Logger::session(sessionId).info("[Executor] 工具: {}", name);

                json args;
                std::ignore = tryParseJson(jsonToString(atOrNull(atOrNull(toolCall, "function"), "arguments")), args);

                if (applyReplyTool(name, args, accumulatedCQCodes, decision)) {
                    hasDecision = true; // 回复工具：结束本轮，不回传工具结果
                    continue;
                }

                // deep_think 以会话上下文为参考材料：快照 system 之后的完整消息列表（含已获取的工具结果）
                // 随调用传给 handler；其余工具不拷贝这份上下文
                ToolCallContext ctx;
                ctx.sessionId = sessionId;
                if (name == "deep_think") {
                    ctx.conversationContext = json::array();
                    for (size_t i = 1; i < messages.size(); ++i) {
                        ctx.conversationContext.push_back(messages[i]);
                    }
                }

                const std::string result =
                  co_await ToolRegistry::instance().executeTool(name, std::move(args), std::move(ctx));
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
                co_return {decision, std::move(messages), std::move(accumulatedCQCodes)};
            }
            co_return {std::nullopt, std::move(messages), std::move(accumulatedCQCodes)};
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
                const auto respJson = co_await LlmClient::requestChat(
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

                // 有工具调用：回传 assistant 消息后逐个处理，未命中回复工具则继续下一轮
                messages.push_back(buildAssistantToolCallMessage(message));
                std::optional<ReplyDecision> roundDecision;
                std::tie(roundDecision, messages, accumulatedCQCodes) =
                  co_await processToolCalls(message, std::move(messages), std::move(accumulatedCQCodes), sessionId);
                if (roundDecision) {
                    co_return std::move(roundDecision);
                }
            }

            Logger::session(sessionId).error("[Executor] 达到最大迭代次数");
            co_return std::nullopt;
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
      const ChatRecordManager &chatRecords, const MemoryManager &memory, RouterDecision decision) {
        const uint64_t sessionId = chatRecords.getSessionId();
        Logger::session(sessionId).info(
          "[Executor] 开始执行 | priority={} | maxLength={}", decision.isPriority, decision.maxLength);

        json messages = buildPrompt(chatRecords, memory, decision);

        co_return co_await executeWithAgent(std::move(messages), sessionId);
    }
} // namespace insoulforge
