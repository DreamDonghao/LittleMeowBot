/// @file RouterAgent.cpp
/// @brief Router Agent - 实现（合并路由判断与规划）

#include <agent/RouterAgent.hpp>
#include <algorithm>
#include <cctype>
#include <config/Config.hpp>
#include <model/QQMessage.hpp>
#include <ranges>
#include <regex>
#include <service/ChatRecordManager.hpp>
#include <service/LlmClient.hpp>
#include <service/PromptService.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <util/CommonUtil.hpp>
#include <util/HttpUtil.hpp>
#include <util/JsonUtil.hpp>
#include <util/Logger.hpp>

namespace insoulforge {
    namespace {
        /// @brief 刷屏检测
        [[nodiscard]] bool checkSpam(const QQMessage &message) {
            std::string rawMsg = message.getRawMessage();
            std::erase_if(rawMsg, [](const char c) { return std::isspace(static_cast<unsigned char>(c)); });

            if (rawMsg.empty())
                return true;
            if (rawMsg.length() <= 2)
                return true;

            // 纯表情包
            static const std::regex facePattern(R"(^\[CQ:face.*\]$)");
            if (rawMsg.find("[CQ:face") != std::string::npos && std::regex_match(rawMsg, facePattern)) {
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

        /// @brief 提取 Router 需要的最小字段：sender + 压缩后 text
        /// @details content 为 formatMessage 生成的 JSON，message_id/reply_to/qq/时间戳/images URL 对路由决策无用；
        ///          机器人自身消息的 name 已带 "(我)" 后缀。历史存量可能是纯文本，仅保留 text
        [[nodiscard]] json compactRecord(const std::string &content) {
            if (json root; tryParseJson(content, root) && root.is_object()) {
                json record;
                if (const std::string name = getStr(atOrNull(root, "sender"), "name"); !name.empty())
                    record["sender"] = name;
                record["text"] = compressCQCodes(getStr(root, "text"));
                return record;
            }
            json record;
            record["text"] = content;
            return record;
        }

        /// @brief 构建 Router 上下文 JSON（键序固定：chat_records → bot_silence）
        /// @details Router 子窗口（触发/保留可配置，默认 20/10）：窗口大小由记录数派生
        ///          （keep + count % slide），批量滑动而非逐条滑动，使 prompt 前缀在批次内稳定，
        ///          最大化 LLM 上下文缓存命中。被滑出的记录仍在水位线之后，由 Executor 的 eviction 统一提取。
        ///          记录为 {sender, text}，仅最后一条追加 is_current 标记（当前待决策消息）。
        ///          bot_silence 发言间隔：精确统计窗口内机器人距上次发言隔了多少条消息
        ///          （LLM 不会数数，由代码计算后作为事实告知，策略由提示词决定）。
        std::string buildChatContext(const ChatRecordManager &chatRecords) {
            const auto &config = Config::instance();
            const auto keep = static_cast<size_t>(config.routerWindowKeepCount);
            const auto slide = std::max<size_t>(1, static_cast<size_t>(config.routerWindowTriggerCount) - keep);

            const auto &records = chatRecords.getRecords();
            const size_t windowSize = keep + (records.size() % slide);
            const size_t startIdx = records.size() > windowSize ? records.size() - windowSize : 0;

            json recordList = json::array();
            bool spokeInWindow = false;
            size_t silentCount = 0;

            for (const auto &record: records | std::views::drop(startIdx)) {
                if (getStr(record, "role") == "assistant") {
                    spokeInWindow = true;
                    silentCount = 0;
                } else {
                    ++silentCount;
                }
                recordList.push_back(compactRecord(getStr(record, "content")));
            }
            if (!recordList.empty())
                recordList.back()["is_current"] = true;

            json botSilence;
            botSilence["spoke_in_window"] = spokeInWindow;
            botSilence["messages_since_last_speak"] = spokeInWindow ? silentCount : records.size() - startIdx;

            json context;
            context["chat_records"] = std::move(recordList);
            context["bot_silence"] = std::move(botSilence);

            if (spokeInWindow) {
                Logger::session(chatRecords.getSessionId())
                  .info("[Router] 发言间隔: 距上次发言 {} 条消息", silentCount);
            } else {
                Logger::session(chatRecords.getSessionId())
                  .info("[Router] 发言间隔: 窗口内无发言记录(至少已沉默 {} 条)", records.size() - startIdx);
            }
            return dumpJson(context);
        }

        /// @brief 解析 LLM 响应
        [[nodiscard]] std::optional<RouterDecision> parseResponse(
          const std::string &content, const uint64_t sessionId) {
            // 提取 JSON
            std::string jsonStr;
            if (!tryExtractJsonObject(content, jsonStr)) {
                Logger::session(sessionId).error("[Router] 未找到JSON: {}", content);
                return std::nullopt;
            }

            json root;
            if (!tryParseJson(jsonStr, root)) {
                Logger::session(sessionId).error("[Router] JSON解析失败: {}", jsonStr);
                return std::nullopt;
            }

            RouterDecision decision;

            // 解析 action
            std::string actionStr = getStr(root, "action", "reply");
            std::ranges::transform(
              actionStr, actionStr.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            decision.action = (actionStr == "skip") ? RouterDecision::Action::SKIP : RouterDecision::Action::REPLY;

            // 解析 reason
            decision.reason = getStr(root, "reason");

            // 解析 strategy
            if (root.contains("strategy") && root["strategy"].is_object()) {
                const json &strategy = root["strategy"];
                decision.tone = getStr(strategy, "tone", "friendly");
                const int maxLen = getInt(strategy, "maxLength", 25);
                decision.maxLength = std::clamp(maxLen, 10, 500);
            }

            decision.shouldReply = (decision.action == RouterDecision::Action::REPLY);

            return decision;
        }

        /// @brief 构建 LLM Prompt
        json buildPrompt(const ChatRecordManager &chatRecords, const bool isPrivate) {
            json messages = json::array();

            // System Prompt（数据库存储，管理后台可编辑；私聊使用独立的私聊路由提示词）
            json systemMsg;
            systemMsg["role"] = "system";
            systemMsg["content"] =
              isPrivate ? PromptService::getRouterPrivateSystemPrompt() : PromptService::getRouterSystemPrompt();
            messages.push_back(systemMsg);

            json userMsg;
            userMsg["role"] = "user";
            userMsg["content"] = buildChatContext(chatRecords);
            messages.push_back(userMsg);

            return messages;
        }

        /// @brief LLM 路由与规划（合并判断 + 策略）
        [[nodiscard]] drogon::Task<std::optional<RouterDecision>> llmRouteAndPlan(
          const ChatRecordManager &chatRecords, const bool isPrivate) {
            const auto &config = Config::instance();

            json messages = buildPrompt(chatRecords, isPrivate);

            json body = LlmClient::buildChatRequestBody(config.router, config.routerParams, std::move(messages));

            const auto resp = co_await HttpUtil::send("[Router]", config.router.baseUrl, config.router.path,
              drogon::Post, std::move(body), config.router.apiKey, 90.0, chatRecords.getSessionId());
            if (!resp) {
                co_return std::nullopt;
            }

            const auto respJson = LlmClient::validChatJson(*resp);
            if (!respJson) {
                Logger::session(chatRecords.getSessionId())
                  .error("[Router] LLM请求失败: status={}", static_cast<int>((*resp)->getStatusCode()));
                co_return std::nullopt;
            }

            LlmClient::logUsage(*respJson, config.router.model, "router", chatRecords.getSessionId());

            const std::string content =
              jsonToString(atOrNull(atOrNull((*respJson)["choices"][0], "message"), "content"));
            Logger::session(chatRecords.getSessionId()).debug("[Router] LLM响应: {}", content);

            co_return parseResponse(content, chatRecords.getSessionId());
        }

        /// @brief 构造硬规则决策结果
        RouterDecision makeDecision(
          const RouterDecision::Action action, std::string reason, int maxLength = 25, bool priority = false) {
            RouterDecision decision;
            decision.action = action;
            decision.shouldReply = action == RouterDecision::Action::REPLY;
            decision.reason = std::move(reason);
            decision.maxLength = maxLength;
            decision.isPriority = priority;
            return decision;
        }
    } // namespace

    drogon::Task<RouterDecision> route(const ChatRecordManager &chatRecords, QQMessage message) {
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

        Logger::session(chatRecords.getSessionId())
          .info("[Router] 决策: {} ({})", llmDecision->action, llmDecision->reason);

        co_return llmDecision.value();
    }
} // namespace insoulforge
