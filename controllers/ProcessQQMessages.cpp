#include "ProcessQQMessages.h"
#include <regex>
#include <agent/AgentSystem.hpp>
#include <handler/CommandHandler.hpp>
#include <model/ChatRecordManager.hpp>
#include <model/GroupConfigManager.hpp>
#include <model/MemoryManager.hpp>
#include <model/QQMessage.hpp>
#include <service/MemoryService.hpp>
#include <service/MessageService.hpp>
#include <service/WebSocketManager.hpp>
#include <storage/Database.hpp>
#include <spdlog/spdlog.h>
#include <util/Logger.hpp>

using namespace LittleMeowBot;
using namespace drogon;

namespace {
    /// @brief 拆分表情包 CQ 码和文字，分开发送（表情包先，文字后）
    /// @param content 代理生成的回复内容
    /// @return {表情包部分, 文字部分}
    [[nodiscard]] std::pair<std::string, std::string> splitCqAndText(const std::string &content) {
        thread_local const std::regex cqPattern(R"(\[CQ:mface,[^\]]*\]|\[CQ:image,[^\]]*sub_type=1[^\]]*\])");

        std::string cqPart;
        for (auto it = std::sregex_iterator(content.begin(), content.end(), cqPattern); it != std::sregex_iterator();
             ++it) {
            cqPart += it->str();
        }
        std::string textPart = std::regex_replace(content, cqPattern, "");
        if (const size_t b = textPart.find_first_not_of(" \t\n\r"); b != std::string::npos) {
            textPart = textPart.substr(b, textPart.find_last_not_of(" \t\n\r") - b + 1);
        } else {
            textPart.clear();
        }
        return {cqPart, textPart};
    }
} // namespace

Task<> ProcessQQMessages::receiveMessages(const HttpRequestPtr req,
                                          std::function<void(const HttpResponsePtr &)> callback) {
    const auto json = req->getJsonObject();
    if (!json || !json->isObject()) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        resp->setBody("Invalid JSON or not an object");
        callback(resp);
        co_return;
    }
    // 检查 post_type 字段
    if (!json->isMember("post_type") || (*json)["post_type"].asString() != "message") {
        Json::Value resp;
        resp["status"] = "ok";
        callback(HttpResponse::newHttpJsonResponse(resp));
        co_return;
    }
    // 返回响应
    Json::Value respJson;
    respJson["status"] = "ok";
    callback(HttpResponse::newHttpJsonResponse(respJson));

    if (!AgentSystem::instance().isRunning()) {
        co_return;
    }

    QQMessage qqMessage(*json);
    uint64_t groupId = qqMessage.getGroupId(); ///< 当前消息的群号
    // 确保群配置存在
    if (!GroupConfigManager::contains(groupId)) {
        GroupConfigManager::addConfig(groupId);
    }
    // 格式化消息
    co_await qqMessage.formatMessage();

    // 检查是否是命令消息（@ 且以 / 开头）- 不受群启用状态影响
    const auto log = Logger::group(groupId);

    if (isCommand(qqMessage)) {
        log.info("收到命令消息: {}", qqMessage.getRawMessage());

        ChatRecordManager chatRecords(groupId);

        std::string cmdResponse = co_await handleCommand(qqMessage, chatRecords);
        co_await MessageService::sendGroupMsg(groupId, cmdResponse, chatRecords);
        co_return;
    }

    // 只处理启用的群（从数据库读取）
    if (auto &database = Database::instance(); !database.isGroupEnabled(groupId)) {
        co_return;
    }

    // 创建聊天记录和记忆管理器
    ChatRecordManager chatRecords(groupId);
    MemoryManager memory(groupId);

    auto &webSocketManager = WebSocketManager::instance();

    // 记录新的聊天消息
    if (qqMessage.getSelfQQNumber() == qqMessage.getSenderQQNumber()) {
        chatRecords.addAssistantRecord(qqMessage.getFormatMessage());
        webSocketManager.pushMessage(groupId, "assistant", qqMessage.getFormatMessage());
    } else {
        chatRecords.addUserRecord(qqMessage.getFormatMessage());
        webSocketManager.pushMessage(groupId, "user", qqMessage.getFormatMessage());
    }

    // 处理消息回复 - 使用多层代理架构
    auto &agentSystem = AgentSystem::instance();

    // 使用二层代理处理消息（顶层兜底，任何异常不让其逃逸到框架层）
    try {
        if (auto result = co_await agentSystem.process(chatRecords, memory, qqMessage);
            result && !result->empty() && agentSystem.isRunning()) {
            log.info("多层代理决定回复");

            // 拆分表情包和文字，分开发送（表情包先，文字后）
            const auto [cqPart, textPart] = splitCqAndText(result.value());

            if (!cqPart.empty()) {
                co_await MessageService::sendGroupMsg(groupId, cqPart, chatRecords);
            }
            if (!textPart.empty()) {
                co_await MessageService::sendGroupMsg(groupId, textPart, chatRecords);
            }
        } else {
            log.info("多层代理决定不回复");
        }
    } catch (const std::exception &e) {
        log.error("消息处理异常: {}", e.what());
    }

    // 更新统计
    GroupConfigManager::incrementMessageCount(groupId, qqMessage.getFormatMessage().size());
    auto [allMesCount, allCharCount] = GroupConfigManager::getConfig(groupId);
    log.info("群聊统计数据: 接收总消息数{}条,接收总字符(字节)数{}个", allMesCount, allCharCount);

    // 记忆提取与窗口滑动 - 窗口超限时触发（失败自愈：下条消息重试）
    try {
        co_await MemoryService::appendAndMergeMemory(groupId);
    } catch (const std::exception &e) {
        log.error("记忆提取异常: {}", e.what());
    }
}
