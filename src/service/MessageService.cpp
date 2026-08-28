/// @file MessageService.cpp
/// @brief QQ 消息服务 - 实现

#include <service/MessageService.hpp>
#include <util/tool.h>
#include <util/HttpUtil.hpp>
#include <spdlog/spdlog.h>
#include <fmt/core.h>
#include <regex>
#include <algorithm>
#include <config/Config.hpp>
#include <service/WebSocketManager.hpp>
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
        std::vector<std::pair<std::string, uint64_t> > sortedNames(nameToQQ.begin(), nameToQQ.end());
        std::sort(sortedNames.begin(), sortedNames.end(),
                  [](const auto &a, const auto &b) { return a.first.length() > b.first.length(); });

        for (const auto &[name, qq]: sortedNames) {
            std::string atPattern = "@" + name;
            size_t pos = 0;
            while ((pos = result.find(atPattern, pos)) != std::string::npos) {
                size_t endPos = pos + atPattern.length();
                bool isComplete = (endPos >= result.length() ||
                                   (!std::isalnum(static_cast<unsigned char>(result[endPos])) &&
                                    result[endPos] != '_' && result[endPos] != '-'));

                if (isComplete) {
                    // 检查是否已经是CQ码的一部分（避免重复转换）
                    if (pos >= 4 && result.substr(pos - 4, 4) == "qq=") {
                        pos = endPos;
                        continue;
                    }
                    std::string cqCode = fmt::format("[CQ:at,qq={}]", qq);
                    result.replace(pos, atPattern.length(), cqCode);
                    pos += cqCode.length();
                } else {
                    pos = endPos;
                }
            }
        }

        return result;
    }

namespace {
    /// @brief 调用 OneBot 发送消息 API 并记录聊天记录、推送 WebSocket（群聊/私聊共用）
    /// @param apiPath OneBot API 路径（/send_group_msg 或 /send_private_msg）
    /// @param body 请求体（含目标字段与 message）
    /// @param chatRecords 聊天记录管理器
    /// @param sessionId 会话 ID（用于日志）
    /// @param channelName 日志中的渠道名（"群消息"/"私聊消息"）
    drogon::Task<> sendOneBotMessage(const std::string &apiPath, const Json::Value &body,
                                     const ChatRecordManager &chatRecords, const uint64_t sessionId,
                                     std::string_view channelName) {
        const auto &config = Config::instance();

        // 转换 @[QQ:xxx] 为 CQ 码
        std::string processedMessage = MessageService::convertAtToCQCode(body["message"].asString());

        Json::Value requestBody = body;
        requestBody["message"] = processedMessage;
        requestBody["auto_escape"] = false;

        const auto resp = co_await HttpUtil::send("[Msg]", config.qqHttpHost, apiPath,
                                                  drogon::Post, requestBody, config.accessToken, 30.0, sessionId);
        if (!resp) {
            co_return;
        }
        const auto requestJson = (*resp)->getJsonObject();

        if ((*resp)->getStatusCode() != drogon::k200OK || !requestJson) {
            Logger::session(sessionId).error("[Msg] 发送消息错误: status={}",
                                           static_cast<int>((*resp)->getStatusCode()));
            co_return;
        }

        std::string status = requestJson->get("status", "").asString();
        if (status != "ok") {
            Logger::session(sessionId).error("发送消息错误: status={}, retcode={}, msgLen={}, preview={}",
                                           status, requestJson->get("retcode", -1).asInt(),
                                           processedMessage.size(), processedMessage.substr(0, 200));
            co_return;
        }

        uint64_t messageId = jsonToUInt64((*requestJson)["data"]["message_id"]);

        // 获取当前时间
        std::string timeStr = currentDateTime();

        // 构造JSON格式的消息
        Json::Value msgJson;
        msgJson["time"] = timeStr;
        msgJson["sender"]["name"] = config.botName + "(我)";
        msgJson["sender"]["qq"] = "self";
        msgJson["message_id"] = std::to_string(messageId);
        msgJson["text"] = processedMessage;
        msgJson["reply_to"] = Json::nullValue;

        Json::StreamWriterBuilder writerBuilder;
        writerBuilder["indentation"] = "";
        writerBuilder["emitUTF8"] = true;
        std::string formattedMsg = Json::writeString(writerBuilder, msgJson);

        // 更新聊天记录（保存JSON格式）
        chatRecords.addAssistantRecord(formattedMsg);

        // WebSocket推送（推送原始文本）
        WebSocketManager::instance().pushMessage(sessionId, "assistant", processedMessage);

        Logger::session(sessionId).info("成功发送{}: {} (message_id={})",
                                      channelName, processedMessage, messageId);
    }
}

drogon::Task<> MessageService::sendGroupMsg(
    Json::UInt64 groupId,
    const std::string &message,
    const ChatRecordManager &chatRecords) {
    Json::Value body;
    body["group_id"] = groupId;
    body["message"] = message;

    co_await sendOneBotMessage("/send_group_msg", body, chatRecords, groupId, "群消息");
}

drogon::Task<> MessageService::sendPrivateMsg(
    Json::UInt64 userId,
    const std::string &message,
    const ChatRecordManager &chatRecords) {
    Json::Value body;
    body["user_id"] = userId;
    body["message"] = message;

    co_await sendOneBotMessage("/send_private_msg", body, chatRecords, userId | QQMessage::kPrivateSessionFlag,
                               "私聊消息");
}

    drogon::Task<bool> MessageService::setGroupBan(
        Json::UInt64 groupId,
        Json::UInt64 userId,
        Json::UInt64 duration) {
        const auto &config = Config::instance();

        Json::Value body;
        body["group_id"] = groupId;
        body["user_id"] = userId;
        body["duration"] = duration;

        const auto resp = co_await HttpUtil::send("[Ban]", config.qqHttpHost, "/set_group_ban",
                                                  drogon::Post, body, config.accessToken, 30.0, groupId);
        if (!resp) {
            co_return false;
        }
        const auto requestJson = (*resp)->getJsonObject();

        if ((*resp)->getStatusCode() != drogon::k200OK || !requestJson) {
            Logger::session(groupId).error("[Ban] 禁言请求失败: http_status={}",
                                         static_cast<int>((*resp)->getStatusCode()));
            co_return false;
        }

        // 打印完整响应便于调试
        Logger::session(groupId).debug("[Ban] 禁言API响应: {}", (*resp)->getBody());

        std::string status = requestJson->get("status", "").asString();
        int retcode = requestJson->get("retcode", -1).asInt();

        // status=ok 表示操作成功
        if (status == "ok") {
            Logger::session(groupId).info("禁言成功: 用户{} 时长{}秒", userId, duration);
            co_return true;
        }

        Logger::session(groupId).error("禁言失败: status={}, retcode={}", status, retcode);
        co_return false;
    }

    drogon::Task<Json::Value> MessageService::getGroupInfo(Json::UInt64 groupId) {
        const auto &config = Config::instance();

        const auto resp = co_await HttpUtil::send("[GroupInfo]", config.qqHttpHost,
                                                  fmt::format("/get_group_info?group_id={}", groupId),
                                                  drogon::Get, Json::Value(Json::nullValue),
                                                  config.accessToken, 30.0, groupId);
        if (!resp) {
            co_return Json::Value(Json::nullValue);
        }

        Json::Value result;
        if ((*resp)->getStatusCode() == drogon::k200OK) {
            result = *(*resp)->getJsonObject();
        }

        co_return result;
    }

    drogon::Task<std::string> MessageService::fetchAndUpdateSessionName(Json::UInt64 sessionId) {
        std::string name;
        if (QQMessage::isPrivateSession(sessionId)) {
            // 私聊会话取 QQ 昵称，复用 groupName 列存储
            const auto &config = Config::instance();
            const uint64_t userId = sessionId & ~QQMessage::kPrivateSessionFlag;
            const auto resp = co_await HttpUtil::send("[StrangerInfo]", config.qqHttpHost,
                                                      fmt::format("/get_stranger_info?user_id={}", userId),
                                                      drogon::Get, Json::Value(Json::nullValue),
                                                      config.accessToken, 30.0, sessionId);
            if (resp && (*resp)->getStatusCode() == drogon::k200OK) {
                const auto body = (*resp)->getJsonObject();
                if (body && body->isMember("data") && (*body)["data"].isMember("nickname")) {
                    name = (*body)["data"]["nickname"].asString();
                    Database::instance().updateSessionName(sessionId, name);
                }
            }
        } else {
            auto result = co_await getGroupInfo(sessionId);
            if (result.isMember("data") && result["data"].isMember("group_name")) {
                name = result["data"]["group_name"].asString();
                Database::instance().updateSessionName(sessionId, name);
            }
        }

        co_return name;
    }

    drogon::Task<bool> MessageService::setGroupPoke(Json::UInt64 groupId, Json::UInt64 userId) {
        const auto &config = Config::instance();

        Json::Value body;
        body["group_id"] = groupId;
        body["user_id"] = userId;

        const auto resp = co_await HttpUtil::send("[Poke]", config.qqHttpHost, "/send_poke",
                                                  drogon::Post, body, config.accessToken, 30.0, groupId);
        if (!resp) {
            co_return false;
        }
        const auto requestJson = (*resp)->getJsonObject();

        if ((*resp)->getStatusCode() != drogon::k200OK || !requestJson) {
            Logger::session(groupId).error("[Poke] 拍一拍请求失败: http_status={}",
                                         static_cast<int>((*resp)->getStatusCode()));
            co_return false;
        }

        std::string status = requestJson->get("status", "").asString();

        // status=ok 表示操作成功
        if (status == "ok") {
            Logger::session(groupId).info("拍一拍成功: 用户{}", userId);
            co_return true;
        }

        Logger::session(groupId).error("拍一拍失败: status={}, retcode={}",
                                     status, requestJson->get("retcode", -1).asInt());
        co_return false;
    }

    drogon::Task<bool> MessageService::deleteMessage(
        Json::UInt64 messageId, std::optional<uint64_t> groupId) {
        const auto &config = Config::instance();

        Json::Value body;
        body["message_id"] = messageId;

        const auto resp = co_await HttpUtil::send("[Recall]", config.qqHttpHost, "/delete_msg",
                                                  drogon::Post, body, config.accessToken, 30.0, groupId);
        if (!resp) {
            co_return false;
        }
        const auto requestJson = (*resp)->getJsonObject();

        if ((*resp)->getStatusCode() != drogon::k200OK || !requestJson) {
            if (groupId) {
                Logger::session(*groupId).error("[Recall] 撤回消息请求失败: http_status={}",
                                              static_cast<int>((*resp)->getStatusCode()));
            } else {
                spdlog::error("[Recall] 撤回消息请求失败: http_status={}",
                              static_cast<int>((*resp)->getStatusCode()));
            }
            co_return false;
        }

        std::string status = requestJson->get("status", "").asString();

        // status=ok 表示操作成功
        if (status == "ok") {
            if (groupId) {
                Logger::session(*groupId).info("撤回消息成功: message_id={}", messageId);
            } else {
                spdlog::info("撤回消息成功: message_id={}", messageId);
            }
            co_return true;
        }

        if (groupId) {
            Logger::session(*groupId).error("撤回消息失败: status={}, retcode={}",
                                          status, requestJson->get("retcode", -1).asInt());
        } else {
            spdlog::error("撤回消息失败: status={}, retcode={}",
                          status, requestJson->get("retcode", -1).asInt());
        }
        co_return false;
    }
}