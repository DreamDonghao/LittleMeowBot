#include "ProcessQQMessages.h"
#include <agent/AgentSystem.hpp>
#include <config/Config.hpp>
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

#include "util/Log.hpp"

using namespace LittleMeowBot;
using namespace drogon;

// 全局状态：每个群的新消息计数（用于触发记忆生成）
std::unordered_map<uint64_t, int> g_newQQMesCounts;

Task<> ProcessQQMessages::receiveMessages(
    const HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
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

    auto& config = Config::instance();
    auto& groupConfigMgr = GroupConfigManager::instance();
    auto& memoryService = MemoryService::instance();
    auto& messageService = MessageService::instance();

    QQMessage qqMessage(*json);
    uint64_t groupId = qqMessage.getGroupId(); ///< 当前消息的群号

    // 检查是否是命令消息（@ 且以 / 开头）- 不受群启用状态影响
    if (auto& commandHandler = CommandHandler::instance();
        commandHandler.isCommand(qqMessage)
    ) {
        spdlog::info("收到命令消息: {}", qqMessage.getRawMessage());

        // 确保群配置存在
        if (!groupConfigMgr.contains(groupId)) {
            groupConfigMgr.addConfig(groupId);
        }

        ChatRecordManager chatRecords(groupId);
        co_await qqMessage.formatMessage();

        std::string cmdResponse = co_await commandHandler.handleCommand(qqMessage, chatRecords);
        co_await messageService.sendGroupMsg(groupId, cmdResponse, chatRecords);
        co_return;
    }

    // 只处理启用的群（从数据库读取）
    if (auto& database = Database::instance();
        !database.isGroupEnabled(groupId)
    ) {
        co_return;
    }

    // 确保群配置存在
    if (!groupConfigMgr.contains(groupId)) {
        groupConfigMgr.addConfig(groupId);
    }

    // 创建聊天记录和记忆管理器
    ChatRecordManager chatRecords(groupId);
    MemoryManager memory(groupId);

    // 格式化消息
    co_await qqMessage.formatMessage();
    // 记录新的聊天消息
    if (qqMessage.getSelfQQNumber() == qqMessage.getSenderQQNumber()) {
        chatRecords.addAssistantRecord(qqMessage.getFormatMessage());
        WebSocketManager::instance().pushMessage(groupId, "assistant", qqMessage.getFormatMessage());
    } else {
        chatRecords.addUserRecord(qqMessage.getFormatMessage());
        WebSocketManager::instance().pushMessage(groupId, "user", qqMessage.getFormatMessage());
    }

    // 处理消息回复 - 使用多层代理架构
    auto& agentSystem = AgentSystem::instance();

    // 使用三层代理处理消息

    if (auto result = co_await agentSystem.process(chatRecords, memory, qqMessage);
        result && !result->empty()
    ) {
        spdlog::info("多层代理决定回复");
        co_await messageService.sendGroupMsg(groupId, result.value(), chatRecords);
    } else {
        spdlog::info("多层代理决定不回复");
    }

    // 更新统计
    groupConfigMgr.incrementMessageCount(groupId, qqMessage.getFormatMessage().size());
    auto [allMesCount, allCharCount] = groupConfigMgr.getConfig(groupId);
    Log::info("群聊统计数据 {} :接收总消息数{}条,接收总字符(字节)数{}个", groupId, allMesCount, allCharCount);

    // 记忆生成 - 取模触发
    if (++g_newQQMesCounts[groupId] % config.memoryTriggerCount == 0) {
        spdlog::info("记忆生成开始 {}({})", groupId, chatRecords.getRecordCount());

        // 使用新的短期记忆流程
        co_await memoryService.appendAndMergeMemory(groupId, chatRecords.getRecordsText());
    }
}
