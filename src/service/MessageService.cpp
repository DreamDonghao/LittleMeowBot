/// @file MessageService.cpp
/// @brief QQ 消息服务 - 实现

#include <service/MessageService.hpp>
#include <util/tool.h>
#include <spdlog/spdlog.h>
#include <fmt/core.h>
#include <regex>
#include <algorithm>
#include <config/Config.hpp>
#include <service/WebSocketManager.hpp>

namespace LittleMeowBot {
    MessageService& MessageService::instance(){
        static MessageService service;
        return service;
    }

    std::string MessageService::convertAtToCQCode(const std::string& text){
        std::string result = text;

        // 格式 @[...数字...] → 提取数字转为 [CQ:at,qq=数字]
        const std::regex atPattern(R"(@\[.*?(\d{5,11}).*?\])");
        result = std::regex_replace(result, atPattern, "[CQ:at,qq=$1]");

        // 2. 模糊格式 @昵称 → 查找昵称映射
        auto nameToQQ = QQMessage::getNameToQQMap();

        // 按昵称长度降序排序，避免短昵称先匹配
        std::vector<std::pair<std::string, uint64_t>> sortedNames(nameToQQ.begin(), nameToQQ.end());
        std::sort(sortedNames.begin(), sortedNames.end(),
                  [](const auto& a, const auto& b) { return a.first.length() > b.first.length(); });

        for (const auto& [name, qq] : sortedNames) {
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

    drogon::Task<> MessageService::sendGroupMsg(
        Json::UInt64 groupId,
        const std::string& message,
        const ChatRecordManager& chatRecords) const{
        const auto& config = Config::instance();
        const auto client = drogon::HttpClient::newHttpClient(config.qqHttpHost);

        // 转换 @[QQ:xxx] 为 CQ 码
        std::string processedMessage = convertAtToCQCode(message);

        Json::Value body;
        body["group_id"] = groupId;
        body["message"] = processedMessage;
        body["auto_escape"] = false;

        const auto req = drogon::HttpRequest::newHttpJsonRequest(body);
        req->setMethod(drogon::Post);
        req->setPath("/send_group_msg");
        req->addHeader("Authorization", "Bearer " + config.accessToken);

        const auto resp = co_await client->sendRequestCoro(req);
        const auto requestJson = resp->getJsonObject();

        if (resp->getStatusCode() != drogon::k200OK || !requestJson) {
            LOG_ERROR << "发送消息错误: status=" << resp->getStatusCode();
            co_return;
        }

        std::string status = requestJson->get("status", "").asString();
        if (status != "ok") {
            LOG_ERROR << "发送消息错误: status=" << status << ", retcode=" << requestJson->get("retcode", -1).asInt();
            co_return;
        }

        uint64_t messageId = (*requestJson)["data"]["message_id"].asUInt64();

        // 获取当前时间
        std::string timeStr = currentDateTime();

        // 构造JSON格式的消息（用于缓存）
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

        QQMessage::addMessageCache(messageId, formattedMsg);

        // 更新聊天记录（保存JSON格式）
        chatRecords.addAssistantRecord(formattedMsg);

        // WebSocket推送（推送原始文本）
        WebSocketManager::instance().pushMessage(groupId, "assistant", processedMessage);

        spdlog::info("成功发送群消息 {} -> {} (message_id={})", groupId, processedMessage, messageId);
    }

    drogon::Task<bool> MessageService::setGroupBan(
        Json::UInt64 groupId,
        Json::UInt64 userId,
        Json::UInt64 duration) const{
        const auto& config = Config::instance();
        const auto client = drogon::HttpClient::newHttpClient(config.qqHttpHost);

        Json::Value body;
        body["group_id"] = groupId;
        body["user_id"] = userId;
        body["duration"] = duration;

        const auto req = drogon::HttpRequest::newHttpJsonRequest(body);
        req->setMethod(drogon::Post);
        req->setPath("/set_group_ban");
        req->addHeader("Authorization", "Bearer " + config.accessToken);

        const auto resp = co_await client->sendRequestCoro(req);
        const auto requestJson = resp->getJsonObject();

        if (resp->getStatusCode() != drogon::k200OK || !requestJson) {
            spdlog::error("禁言请求失败: http_status={}", resp->getStatusCode());
            co_return false;
        }

        // 打印完整响应便于调试
        spdlog::info("禁言API响应: {}", resp->getBody());

        std::string status = requestJson->get("status", "").asString();
        int retcode = requestJson->get("retcode", -1).asInt();

        // status=ok 表示操作成功
        if (status == "ok") {
            spdlog::info("禁言成功: 群{} 用户{} 时长{}秒", groupId, userId, duration);
            co_return true;
        }

        spdlog::error("禁言失败: status={}, retcode={}", status, retcode);
        co_return false;
    }

    drogon::Task<Json::Value> MessageService::getGroupInfo(Json::UInt64 groupId) const{
        const auto& config = Config::instance();
        const auto client = drogon::HttpClient::newHttpClient(config.qqHttpHost);

        const auto req = drogon::HttpRequest::newHttpRequest();
        req->setMethod(drogon::Get);
        req->setPath(fmt::format("/get_group_info?group_id={}", groupId));
        req->addHeader("Authorization", "Bearer " + config.accessToken);

        const auto resp = co_await client->sendRequestCoro(req);

        Json::Value result;
        if (resp->getStatusCode() == drogon::k200OK) {
            result = *resp->getJsonObject();
        }

        co_return result;
    }

    drogon::Task<std::string> MessageService::fetchAndUpdateGroupName(Json::UInt64 groupId) const{
        auto result = co_await getGroupInfo(groupId);

        std::string groupName;
        if (result.isMember("data") && result["data"].isMember("group_name")) {
            groupName = result["data"]["group_name"].asString();
            Database::instance().updateGroupName(groupId, groupName);
        }

        co_return groupName;
    }

    drogon::Task<bool> MessageService::setGroupPoke(Json::UInt64 groupId, Json::UInt64 userId) const{
        const auto& config = Config::instance();
        const auto client = drogon::HttpClient::newHttpClient(config.qqHttpHost);

        Json::Value body;
        body["group_id"] = groupId;
        body["user_id"] = userId;

        const auto req = drogon::HttpRequest::newHttpJsonRequest(body);
        req->setMethod(drogon::Post);
        req->setPath("/send_poke");
        req->addHeader("Authorization", "Bearer " + config.accessToken);

        const auto resp = co_await client->sendRequestCoro(req);
        const auto requestJson = resp->getJsonObject();

        if (resp->getStatusCode() != drogon::k200OK || !requestJson) {
            spdlog::error("拍一拍请求失败: http_status={}", resp->getStatusCode());
            co_return false;
        }

        std::string status = requestJson->get("status", "").asString();

        // status=ok 表示操作成功
        if (status == "ok") {
            spdlog::info("拍一拍成功: 群{} 用户{}", groupId, userId);
            co_return true;
        }

        spdlog::error("拍一拍失败: status={}, retcode={}", status, requestJson->get("retcode", -1).asInt());
        co_return false;
    }

    drogon::Task<bool> MessageService::deleteMessage(Json::UInt64 messageId) const{
        const auto& config = Config::instance();
        const auto client = drogon::HttpClient::newHttpClient(config.qqHttpHost);

        Json::Value body;
        body["message_id"] = messageId;

        const auto req = drogon::HttpRequest::newHttpJsonRequest(body);
        req->setMethod(drogon::Post);
        req->setPath("/delete_msg");
        req->addHeader("Authorization", "Bearer " + config.accessToken);

        const auto resp = co_await client->sendRequestCoro(req);
        const auto requestJson = resp->getJsonObject();

        if (resp->getStatusCode() != drogon::k200OK || !requestJson) {
            spdlog::error("撤回消息请求失败: http_status={}", resp->getStatusCode());
            co_return false;
        }

        std::string status = requestJson->get("status", "").asString();

        // status=ok 表示操作成功
        if (status == "ok") {
            spdlog::info("撤回消息成功: message_id={}", messageId);
            co_return true;
        }

        spdlog::error("撤回消息失败: status={}, retcode={}", status, requestJson->get("retcode", -1).asInt());
        co_return false;
    }
}