/// @file RouterAgent.cpp
/// @brief Router Agent - 实现（合并路由判断与规划）

#include <agent/RouterAgent.hpp>
#include <api/ApiClient.hpp>
#include <config/Config.hpp>
#include <model/QQMessage.hpp>
#include <model/ChatRecordManager.hpp>
#include <service/PromptService.hpp>
#include <util/HttpUtil.hpp>
#include <util/Logger.hpp>
#include <spdlog/spdlog.h>
#include <fmt/core.h>
#include <regex>
#include <ranges>
#include <algorithm>
#include <string>
#include <deque>
#include <string>
#include <model/QQMessage.hpp>
#include <model/ChatRecordManager.hpp>

namespace insoulforge {
    namespace {
        /// @brief 刷屏检测
        [[nodiscard]] bool checkSpam(const QQMessage &message) {
            std::string rawMsg = message.getRawMessage();
            std::erase_if(rawMsg, [](const char c) { return std::isspace(static_cast<unsigned char>(c)); });

            if (rawMsg.empty()) return true;
            if (rawMsg.length() <= 2) return true;

            // 纯表情包
            static const std::regex facePattern(R"(^\[CQ:face.*\]$)");
            if (rawMsg.find("[CQ:face") != std::string::npos
                && std::regex_match(rawMsg, facePattern)) {
                return true;
            }

            return false;
        }

        /// @brief 压缩文本中的 CQ 码为语义标记（Router 只需语义，URL/ID 无用）
        [[nodiscard]] std::string compressCQCodes(std::string text) {
            static const std::regex replyPattern(R"(\[CQ:reply,id=\d+\])");
            static const std::regex stickerPattern(R"(\[CQ:image,[^\]]*summary=([^,\]]+)[^\]]*\])");
            static const std::regex imagePattern(R"(\[CQ:image,[^\]]*\])");
            static const std::regex facePattern(R"(\[CQ:face,[^\]]*\])");
            text = std::regex_replace(text, replyPattern, "[回复]");
            text = std::regex_replace(text, stickerPattern, "[表情:$1]");
            text = std::regex_replace(text, imagePattern, "[图片]");
            text = std::regex_replace(text, facePattern, "[表情]");
            return text;
        }

        /// @brief 提取 Router 需要的最小字段：发送者名字 + 压缩后文本
        /// @details content 为 formatMessage 生成的 JSON，message_id/reply_to/qq/时间戳/images URL 对路由决策无用
        [[nodiscard]] std::string compactContent(const std::string &content, const bool includeSender) {
            Json::Value root;
            if (Json::Reader reader; reader.parse(content, root)) {
                const std::string name = root["sender"]["name"].asString();
                const std::string text = compressCQCodes(root["text"].asString());
                if (includeSender && !name.empty()) return name + ": " + text;
                return text;
            }
            return content;
        }

        /// @brief 构建聊天上下文文本
        /// @details Router 子窗口（触发/保留可配置，默认 20/10）：窗口大小由记录数派生
        ///          （keep + count % slide），批量滑动而非逐条滑动，使 prompt 前缀在批次内稳定，
        ///          最大化 LLM 上下文缓存命中。被滑出的记录仍在水位线之后，由 Executor 的 eviction 统一提取。
        ///          行格式：[小喵]: 文本 / [用户] 名字: 文本
        ///          末尾附带【发言间隔】：精确统计窗口内机器人距上次发言隔了多少条消息
        ///          （LLM 不会数数，由代码计算后作为事实告知，策略由提示词决定）。
        std::string buildChatContext(const ChatRecordManager &chatRecords) {
            const auto &config = Config::instance();
            const auto keep = static_cast<size_t>(config.routerWindowKeepCount);
            const auto slide = std::max<size_t>(1, static_cast<size_t>(config.routerWindowTriggerCount) - keep);

            const auto &records = chatRecords.getRecords();
            const size_t windowSize = keep + (records.size() % slide);
            const size_t startIdx = std::ssize(records) > windowSize ? std::ssize(records) - windowSize : 0;

            std::string context = "【聊天记录】（最后一条是当前消息）\n";
            bool spokeInWindow = false;
            size_t silentCount = 0;

            // 使用范围循环简化代码
            for (const auto &record : records | std::views::drop(startIdx)) {
                const bool isAssistant = record["role"].asString() == "assistant";
                if (isAssistant) {
                    spokeInWindow = true;
                    silentCount = 0;
                } else {
                    ++silentCount;
                }
                context += isAssistant
                               ? fmt::format("[{}]: {}\n", config.botName,
                                             compactContent(record["content"].asString(), false))
                               : fmt::format("[用户] {}\n", compactContent(record["content"].asString(), true));
            }

            // 发言间隔作为事实注入末尾（置于末尾以保持前缀稳定，缓存不受影响）
            const std::string &botName = Config::instance().botName;
            if (spokeInWindow) {
                context += fmt::format("\n【发言间隔】{} 距上次发言已隔 {} 条消息。\n", botName, silentCount);
                Logger::session(chatRecords.getSessionId()).info("[Router] 发言间隔: 距上次发言 {} 条消息", silentCount);
            } else {
                const size_t windowLen = records.size() - startIdx;
                context += fmt::format("\n【发言间隔】聊天记录中看不到 {} 的发言，已沉默至少 {} 条消息。\n", botName, windowLen);
                Logger::session(chatRecords.getSessionId()).info("[Router] 发言间隔: 窗口内无发言记录(至少已沉默 {} 条)", windowLen);
            }
            return context;
        }

        /// @brief 解析 LLM 响应
        [[nodiscard]] std::optional<RouterDecision> parseResponse(
            const std::string &content, const uint64_t sessionId) {
            // 提取 JSON
            size_t start = content.find('{');
            size_t end = content.rfind('}');
            if (start == std::string::npos || end == std::string::npos) {
                Logger::session(sessionId).error("[Router] 未找到JSON: {}", content);
                return std::nullopt;
            }

            std::string jsonStr = content.substr(start, end - start + 1);

            Json::Value root;
            if (Json::Reader reader; !reader.parse(jsonStr, root)) {
                Logger::session(sessionId).error("[Router] JSON解析失败: {}", jsonStr);
                return std::nullopt;
            }

            RouterDecision decision;

            // 解析 action
            std::string actionStr = root.get("action", "reply").asString();
            std::ranges::transform(actionStr, actionStr.begin(), ::tolower);
            decision.action = (actionStr == "skip")
                                  ? RouterDecision::Action::SKIP
                                  : RouterDecision::Action::REPLY;

            // 解析 reason
            decision.reason = root.get("reason", "").asString();

            // 解析 strategy
            if (root.isMember("strategy") && root["strategy"].isObject()) {
                const auto &strategy = root["strategy"];
                decision.enableThinking = strategy.get("enableThinking", false).asBool();
                decision.tone = strategy.get("tone", "friendly").asString();
                int maxLen = strategy.get("maxLength", 25).asInt();
                decision.maxLength = std::clamp(maxLen, 10, 500);
            }

            decision.shouldReply = (decision.action == RouterDecision::Action::REPLY);

            return decision;
        }

        /// @brief 构建 LLM Prompt
        Json::Value buildPrompt(
            const ChatRecordManager &chatRecords,
            const bool isPrivate) {
            Json::Value messages;

            // System Prompt（数据库存储，管理后台可编辑；私聊使用独立的私聊路由提示词）
            Json::Value systemMsg;
            systemMsg["role"] = "system";
            systemMsg["content"] = isPrivate ? PromptService::getRouterPrivateSystemPrompt()
                                             : PromptService::getRouterSystemPrompt();
            messages.append(systemMsg);

            Json::Value userMsg;
            userMsg["role"] = "user";
            userMsg["content"] = buildChatContext(chatRecords);
            messages.append(userMsg);

            return messages;
        }

        /// @brief LLM 路由与规划（合并判断 + 策略）
        [[nodiscard]] drogon::Task<std::optional<RouterDecision> > llmRouteAndPlan(
            const ChatRecordManager &chatRecords,
            const bool isPrivate) {
            const auto &config = Config::instance();

            const Json::Value messages = buildPrompt(chatRecords, isPrivate);

            Json::Value body;
            body["model"] = config.router.model;
            body["messages"] = messages;
            body["temperature"] = config.routerParams.temperature;
            body["max_tokens"] = config.routerParams.maxTokens;
            body["top_p"] = config.routerParams.topP;
            if (!config.router.reasoningEffort.empty()) {
                body["reasoning_effort"] = config.router.reasoningEffort;
            }

            const auto resp = co_await HttpUtil::send("[Router]", config.router.baseUrl, config.router.path,
                                                      drogon::Post, body, config.router.apiKey, 90.0,
                                                      chatRecords.getSessionId());
            if (!resp) {
                co_return std::nullopt;
            }

            const auto json = (*resp)->getJsonObject();
            if ((*resp)->getStatusCode() != drogon::k200OK || !json || !json->isMember("choices")) {
                Logger::session(chatRecords.getSessionId()).error("[Router] LLM请求失败: status={}",
                                                              static_cast<int>((*resp)->getStatusCode()));
                co_return std::nullopt;
            }

            ApiClient::logUsage(*json, config.router.model, "router", chatRecords.getSessionId());

            const std::string content = (*json)["choices"][0]["message"]["content"].asString();
            Logger::session(chatRecords.getSessionId()).debug("[Router] LLM响应: {}", content);

            co_return parseResponse(content, chatRecords.getSessionId());
        }

        /// @brief 构造硬规则决策结果
        RouterDecision makeDecision(const RouterDecision::Action action, std::string reason,
                                    int maxLength = 25, bool priority = false) {
            RouterDecision decision;
            decision.action = action;
            decision.shouldReply = action == RouterDecision::Action::REPLY;
            decision.reason = std::move(reason);
            decision.maxLength = maxLength;
            decision.isPriority = priority;
            return decision;
        }
    }

    drogon::Task<RouterDecision> route(
        const ChatRecordManager &chatRecords,
        const QQMessage &message) {
        // ========== Step 1: 硬规则检查（无需 LLM）==========

        // 1.0 系统定时任务触发 → 高优先级回复（调度器以系统账号合成的消息，确定性放行；
        //     需置于@提及检查之前，否则合成消息携带的@段会先命中导致此处日志不可见）
        if (message.getSenderQQNumber() == QQMessage::kSystemAccountId) {
            Logger::session(chatRecords.getSessionId()).info("[Router] 定时任务触发 → 高优先级回复");
            co_return makeDecision(RouterDecision::Action::REPLY, "系统定时任务触发", 100, true);
        }

        // 1.1 @提及检测 → 高优先级回复（私聊中每条消息都是直接对机器人说的，不适用）
        if (message.atMe()) {
            Logger::session(chatRecords.getSessionId()).info("[Router] @提及 → 高优先级回复");
            co_return makeDecision(RouterDecision::Action::REPLY, "用户@提及", 100, true);
        }

        // 1.2 刷屏检测 → 跳过（仅群聊；私聊中短句如"嗯""哈哈"也应对话，放宽检查）
        if (!message.isPrivate() && checkSpam(message)) {
            Logger::session(chatRecords.getSessionId()).info("[Router] 刷屏消息 → 跳过");
            co_return makeDecision(RouterDecision::Action::SKIP, "刷屏/纯表情");
        }

        // 1.3 自身消息检测 → 跳过
        if (message.getSelfQQNumber() == message.getSenderQQNumber()) {
            Logger::session(chatRecords.getSessionId()).info("[Router] 自身消息 → 跳过");
            co_return makeDecision(RouterDecision::Action::SKIP, "机器人自己发送的消息");
        }

        // ========== Step 2: LLM 路由与规划（私聊使用私聊提示词）==========
        auto llmDecision = co_await llmRouteAndPlan(chatRecords, message.isPrivate());

        if (!llmDecision) {
            // LLM 失败时默认回复（保守策略）
            Logger::session(chatRecords.getSessionId()).warn("[Router] LLM 失败，默认回复");
            co_return makeDecision(RouterDecision::Action::REPLY, "LLM调用失败，保守回复");
        }

        Logger::session(chatRecords.getSessionId()).info("[Router] 决策: {} ({})",
                                                     llmDecision->action, llmDecision->reason);

        co_return llmDecision.value();
    }
}
