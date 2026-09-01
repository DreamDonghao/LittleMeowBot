/// @file MessageService.cpp
/// @brief QQ 消息服务 - 实现

#include <algorithm>
#include <config/Config.hpp>
#include <fmt/core.h>
#include <regex>
#include <service/MessageService.hpp>
#include <service/OneBotClient.hpp>
#include <service/WebSocketManager.hpp>
#include <storage/SessionStore.hpp>
#include <util/CommonUtil.hpp>
#include <util/Logger.hpp>

namespace insoulforge {
    std::string MessageService::convertAtToCQCode(const std::string &text) {
        std::string result = text;

        // 格式 @[...数字...] → 提取数字转为 [CQ:at,qq=数字]
        const std::regex atPattern(R"(@\[.*?(\d{5,11}).*?\])");
        result = std::regex_replace(result, atPattern, "[CQ:at,qq=$1]");

        // 2. 模糊格式 @昵称 → 查找昵称映射
        auto nameToQQ = QQMessage::getNameToQQMap();

        // 按昵称长度降序排序，避免短昵称先匹配
        std::vector<std::pair<std::string, uint64_t>> sortedNames(nameToQQ.begin(), nameToQQ.end());
        std::ranges::sort(
          sortedNames, [](const auto &a, const auto &b) { return a.first.length() > b.first.length(); });

        for (const auto &[name, qq]: sortedNames) {
            const std::string mention = "@" + name;
            size_t pos = 0;
            while ((pos = result.find(mention, pos)) != std::string::npos) {
                size_t endPos = pos + mention.length();
                bool isComplete =
                  endPos >= result.length() || (!std::isalnum(static_cast<unsigned char>(result[endPos])) &&
                                                 result[endPos] != '_' && result[endPos] != '-');

                if (isComplete) {
                    // 检查是否已经是CQ码的一部分（避免重复转换）
                    if (pos >= 4 && result.substr(pos - 4, 4) == "qq=") {
                        pos = endPos;
                        continue;
                    }
                    std::string cqCode = fmt::format("[CQ:at,qq={}]", qq);
                    result.replace(pos, mention.length(), cqCode);
                    pos += cqCode.length();
                } else {
                    pos = endPos;
                }
            }
        }

        return result;
    }

    namespace {
        /// @brief 发送消息后记录聊天记录、推送 WebSocket（群聊/私聊共用）
        /// @param sendTask 发送协程（OneBotClient）
        /// @param processedMessage 已完成 CQ 码转换的消息内容
        /// @param sessionId 会话 ID
        /// @param channelName 日志中的渠道名（"群消息"/"私聊消息"）
        drogon::Task<> afterSendMessage(drogon::Task<std::optional<uint64_t>> sendTask,
          const ChatRecordManager &chatRecords, const std::string &processedMessage, const uint64_t sessionId,
          std::string_view channelName) {
            const auto messageId = co_await std::move(sendTask);
            if (!messageId) {
                co_return;
            }

            const auto &config = Config::instance();

            // 获取当前时间
            const std::string timeStr = currentDateTime();

            // 构造JSON格式的消息
            Json::Value msgJson;
            msgJson["time"] = timeStr;
            msgJson["sender"]["name"] = config.botName + "(我)";
            msgJson["sender"]["qq"] = "self";
            msgJson["message_id"] = std::to_string(*messageId);
            msgJson["text"] = processedMessage;
            msgJson["reply_to"] = Json::nullValue;

            const std::string formattedMsg = dumpJson(msgJson);

            // 更新聊天记录（保存JSON格式）
            chatRecords.addAssistantRecord(formattedMsg);

            // WebSocket推送（推送原始文本）
            WebSocketManager::instance().pushMessage(sessionId, "assistant", processedMessage);

            Logger::session(sessionId).info(
              "成功发送{}: {} (message_id={})", channelName, processedMessage, *messageId);
        }
    } // namespace

    drogon::Task<> MessageService::sendGroupMsg(
      Json::UInt64 groupId, const std::string &message, const ChatRecordManager &chatRecords) {
        // 转换 @[QQ:xxx] 为 CQ 码
        const std::string processedMessage = convertAtToCQCode(message);
        co_await afterSendMessage(
          OneBotClient::sendGroupMsg(groupId, processedMessage), chatRecords, processedMessage, groupId, "群消息");
    }

    drogon::Task<> MessageService::sendPrivateMsg(
      Json::UInt64 userId, const std::string &message, const ChatRecordManager &chatRecords) {
        const std::string processedMessage = convertAtToCQCode(message);
        co_await afterSendMessage(
          OneBotClient::sendPrivateMsg(userId, processedMessage, userId | QQMessage::kPrivateSessionFlag), chatRecords,
          processedMessage, userId | QQMessage::kPrivateSessionFlag, "私聊消息");
    }

    drogon::Task<std::string> MessageService::fetchAndUpdateSessionName(Json::UInt64 sessionId) {
        std::string name;
        if (QQMessage::isPrivateSession(sessionId)) {
            // 私聊会话取 QQ 昵称，复用 groupName 列存储
            const uint64_t userId = sessionId & ~QQMessage::kPrivateSessionFlag;
            const auto resp = co_await OneBotClient::getStrangerInfo(userId, sessionId);
            if (resp.isMember("data") && resp["data"].isMember("nickname")) {
                name = resp["data"]["nickname"].asString();
                SessionStore::updateSessionName(sessionId, name);
            }
        } else {
            const auto result = co_await OneBotClient::getGroupInfo(sessionId);
            if (result.isMember("data") && result["data"].isMember("group_name")) {
                name = result["data"]["group_name"].asString();
                SessionStore::updateSessionName(sessionId, name);
            }
        }

        co_return name;
    }
} // namespace insoulforge
