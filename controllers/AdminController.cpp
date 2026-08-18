#include "AdminController.h"
#include <model/QQMessage.hpp>
#include <agent/AgentToolManager.hpp>
#include <service/ToolRegistry.hpp>
#include <spdlog/spdlog.h>
#include <config/Config.hpp>
#include <algorithm>
#include <charconv>

using namespace LittleMeowBot;
using namespace drogon;

// ==================== LLM配置 ====================

Task<> AdminController::getLLMConfigs(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    auto configs = Database::instance().getAllLLMConfigs();
    callback(HttpResponse::newHttpJsonResponse(configs));
    co_return;
}

Task<> AdminController::saveLLMConfig(
    const HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    auto json = req->getJsonObject();
    if (!json || !json->isMember("name")) {
        Json::Value err;
        err["error"] = "缺少name字段";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    std::string name = (*json)["name"].asString();
    Database::instance().saveLLMConfig(name, *json);

    // 更新内存中的配置
    auto& config = Config::instance();
    if (name == "router") {
        config.router.apiKey = json->get("apiKey", "").asString();
        config.router.baseUrl = json->get("baseUrl", "").asString();
        config.router.path = json->get("path", "").asString();
        config.router.model = json->get("model", "").asString();
        config.router.reasoningEffort = json->get("reasoningEffort", "").asString();
        config.routerParams.maxTokens = json->get("maxTokens", 100).asInt();
        config.routerParams.temperature = json->get("temperature", 0.7f).asFloat();
        config.routerParams.topP = json->get("topP", 0.9f).asFloat();
    } else if (name == "executor") {
        config.executor.apiKey = json->get("apiKey", "").asString();
        config.executor.baseUrl = json->get("baseUrl", "").asString();
        config.executor.path = json->get("path", "").asString();
        config.executor.model = json->get("model", "").asString();
        config.executor.reasoningEffort = json->get("reasoningEffort", "").asString();
        config.executorParams.maxTokens = json->get("maxTokens", 100).asInt();
        config.executorParams.temperature = json->get("temperature", 0.7f).asFloat();
        config.executorParams.topP = json->get("topP", 0.9f).asFloat();
    } else if (name == "executorThinking") {
        config.executorThinking.apiKey = json->get("apiKey", "").asString();
        config.executorThinking.baseUrl = json->get("baseUrl", "").asString();
        config.executorThinking.path = json->get("path", "").asString();
        config.executorThinking.model = json->get("model", "").asString();
        config.executorThinking.reasoningEffort = json->get("reasoningEffort", "").asString();
        config.executorThinkingParams.maxTokens = json->get("maxTokens", 512).asInt();
        config.executorThinkingParams.temperature = json->get("temperature", 0.7f).asFloat();
        config.executorThinkingParams.topP = json->get("topP", 0.9f).asFloat();
    } else if (name == "image") {
        config.image.apiKey = json->get("apiKey", "").asString();
        config.image.baseUrl = json->get("baseUrl", "").asString();
        config.image.path = json->get("path", "").asString();
        config.image.model = json->get("model", "").asString();
        config.image.reasoningEffort = json->get("reasoningEffort", "").asString();
    }

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "LLM配置已保存";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== 提示词 ====================

Task<> AdminController::getPrompts(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    auto prompts = Database::instance().getAllPrompts();

    Json::Value result;
    for (const auto& [key, content] : prompts) {
        result[key] = content;
    }
    callback(HttpResponse::newHttpJsonResponse(result));
    co_return;
}

Task<> AdminController::savePrompt(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    auto json = req->getJsonObject();
    if (!json || !json->isMember("key") || !json->isMember("content")) {
        Json::Value err;
        err["error"] = "缺少key或content字段";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    std::string key = (*json)["key"].asString();
    std::string content = (*json)["content"].asString();
    std::string description = json->isMember("description") ? (*json)["description"].asString() : "";

    // 防护: router_system 的 JSON 格式示例若含双花括号(fmt 转义残留/旧页面缓存内容),模型会照抄导致解析失败
    if (key == "router_system" && (content.find("{{") != std::string::npos
        || content.find("}}") != std::string::npos)) {
        Json::Value err;
        err["success"] = false;
        err["error"] = "提示词包含双花括号{{ }}，JSON 格式示例应为单花括号，请刷新页面后重试";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    Database::instance().setPrompt(key, content, description);
    spdlog::warn("管理后台更新提示词: key={}, 长度={}", key, content.size());

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "提示词已保存";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== 用量统计 ====================

Task<> AdminController::getUsage(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    int days = 30;
    if (const std::string p = req->getParameter("days"); !p.empty()) {
        // 与 stoi 行为一致：跳过前导空白，解析失败时保留默认值
        const auto* begin = p.data();
        const auto* end = p.data() + p.size();
        while (begin < end && (*begin == ' ' || *begin == '\t')) ++begin;
        std::from_chars(begin, end, days);
    }
    days = std::clamp(days, 1, 365);

    Json::Value resp = Database::instance().getUsageSummary(days);
    resp["recent"] = Database::instance().getRecentUsage(50);
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== 表情包库（QQ 收藏表情，以实际收藏为基准） ====================

Task<> AdminController::getEmojis(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    callback(HttpResponse::newHttpJsonResponse(co_await AgentToolManager::fetchFavoriteEmojis()));
    co_return;
}

Task<> AdminController::updateEmojiDesc(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    auto json = req->getJsonObject();
    if (!json || !json->isMember("res_id") || !json->isMember("desc")) {
        Json::Value err;
        err["error"] = "缺少必要字段: res_id、desc";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    const auto& config = Config::instance();
    const auto client = drogon::HttpClient::newHttpClient(config.qqHttpHost);

    Json::Value body;
    body["emoji_id"] = json->isMember("emoji_id") ? (*json)["emoji_id"].asString() : "0";
    body["res_id"] = (*json)["res_id"].asString();
    body["md5"] = json->isMember("md5") ? (*json)["md5"].asString() : "";
    body["desc"] = (*json)["desc"].asString();

    const auto httpReq = drogon::HttpRequest::newHttpJsonRequest(body);
    httpReq->setMethod(drogon::Post);
    httpReq->setPath("/set_custom_face_desc");
    httpReq->addHeader("Authorization", "Bearer " + config.accessToken);

    drogon::HttpResponsePtr resp;
    try {
        resp = co_await client->sendRequestCoro(httpReq, 30.0);
    } catch (const std::exception& e) {
        spdlog::error("[Admin] 修改表情描述网络异常: {}", e.what());
        Json::Value err;
        err["error"] = "修改表情描述失败，请确认 QQ 客户端在线";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }
    const auto respJson = resp->getJsonObject();

    if (resp->getStatusCode() != drogon::k200OK || !respJson
        || respJson->get("status", "failed").asString() != "ok") {
        spdlog::error("[Admin] 修改表情描述失败: status={}",
                      static_cast<int>(resp->getStatusCode()));
        Json::Value err;
        err["error"] = "修改表情描述失败，请确认 QQ 客户端在线";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    AgentToolManager::invalidateFavoriteEmojiCache();
    spdlog::info("[Admin] 已修改表情描述: res_id={} desc={}",
                 body["res_id"].asString(), body["desc"].asString());

    Json::Value respJson2;
    respJson2["success"] = true;
    respJson2["message"] = "描述已修改";
    callback(HttpResponse::newHttpJsonResponse(respJson2));
    co_return;
}

// ==================== 管理员 ====================

Task<> AdminController::getAdmins(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    auto admins = Database::instance().getAdmins();

    Json::Value result(Json::arrayValue);
    for (uint64_t qq : admins) {
        Json::Value admin;
        admin["qq"] = static_cast<Json::UInt64>(qq);
        result.append(admin);
    }
    callback(HttpResponse::newHttpJsonResponse(result));
    co_return;
}

Task<> AdminController::addAdmin(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    auto json = req->getJsonObject();
    if (!json || !json->isMember("qq")) {
        Json::Value err;
        err["error"] = "缺少qq字段";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    uint64_t qq = (*json)["qq"].asUInt64();
    Database::instance().addAdmin(qq);

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "管理员已添加";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::removeAdmin(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback,
    const std::string& qq
) const{
    uint64_t qqNum = std::stoull(qq);
    Database::instance().removeAdmin(qqNum);

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "管理员已删除";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== 启用群 ====================

Task<> AdminController::getGroups(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    auto groups = Database::instance().getAllGroupsWithStatus();

    Json::Value result(Json::arrayValue);
    for (const auto& [groupId, groupName, enabled, messageCount] : groups) {
        Json::Value group;
        group["groupId"] = static_cast<Json::UInt64>(groupId);
        group["groupName"] = groupName;
        group["enabled"] = enabled;
        group["messageCount"] = messageCount;
        result.append(group);
    }
    callback(HttpResponse::newHttpJsonResponse(result));
    co_return;
}

Task<> AdminController::enableGroup(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    auto json = req->getJsonObject();
    if (!json || !json->isMember("groupId")) {
        Json::Value err;
        err["error"] = "缺少groupId字段";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    const uint64_t groupId = (*json)["groupId"].asUInt64();
    Database::instance().enableGroup(groupId);

    // 自动获取群名称
    auto groupName = co_await MessageService::fetchAndUpdateGroupName(groupId);

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "群已启用";
    resp["groupName"] = groupName;
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::toggleGroup(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback,
    const std::string& groupId
) const{
    const uint64_t gid = std::stoull(groupId);
    Database::instance().toggleGroupStatus(gid);

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "群状态已切换";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::removeGroup(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback,
    const std::string& groupId
) const{
    uint64_t gid = std::stoull(groupId);
    Database::instance().disableGroup(gid);

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "群已删除";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::refreshGroupName(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback,
    const std::string& groupId
) const{
    uint64_t gid = std::stoull(groupId);
    auto groupName = co_await MessageService::fetchAndUpdateGroupName(gid);

    Json::Value resp;
    resp["success"] = true;
    resp["groupName"] = groupName;
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::refreshAllGroupNames(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    auto groups = Database::instance().getAllGroupsWithStatus();

    for (const auto& [groupId, groupName, enabled, messageCount] : groups) {
        co_await MessageService::fetchAndUpdateGroupName(groupId);
    }

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "所有群名称已刷新";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== 聊天记录 ====================

Task<> AdminController::getChatGroups(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    auto groups = Database::instance().getGroupsWithChatRecords();

    Json::Value result(Json::arrayValue);
    for (const auto& [groupId, groupName, messageCount] : groups) {
        Json::Value group;
        group["groupId"] = static_cast<Json::UInt64>(groupId);
        group["groupName"] = groupName;
        group["messageCount"] = messageCount;
        result.append(group);
    }
    callback(HttpResponse::newHttpJsonResponse(result));
    co_return;
}

Task<> AdminController::getChatRecords(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback,
    const std::string& groupId
) const{
    uint64_t gid = std::stoull(groupId);

    // 支持limit参数
    int limit = 50;
    if (req->getParameter("limit") != "") {
        limit = std::stoi(req->getParameter("limit"));
    }

    // 返回带ID的记录，支持编辑
    auto result = Database::instance().getChatRecordsWithIds(gid, limit);

    // 反转顺序，最新的在底部
    Json::Value reversed(Json::arrayValue);
    for (Json::ArrayIndex i = result.size(); i > 0; --i) {
        reversed.append(result[i - 1]);
    }

    callback(HttpResponse::newHttpJsonResponse(reversed));
    co_return;
}

Task<> AdminController::updateChatRecord(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback,
    const std::string& recordId
) const{
    auto json = req->getJsonObject();
    if (!json || !json->isMember("content")) {
        Json::Value err;
        err["error"] = "缺少content字段";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    int id = std::stoi(recordId);
    std::string content = (*json)["content"].asString();
    Database::instance().updateChatRecord(id, content);

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "聊天记录已更新";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::deleteChatRecord(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback,
    const std::string& recordId
) const{
    int id = std::stoi(recordId);
    Database::instance().deleteChatRecord(id);

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "聊天记录已删除";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::clearGroupChatRecords(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback,
    const std::string& groupId
) const{
    uint64_t gid = std::stoull(groupId);
    Database::instance().clearGroupChatRecords(gid);

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "聊天记录已清空";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== 知识库配置 ====================

Task<> AdminController::getKBConfig(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    auto config = Database::instance().getKBConfig();
    callback(HttpResponse::newHttpJsonResponse(config));
    co_return;
}

Task<> AdminController::saveKBConfig(
    const HttpRequestPtr req,
    const std::function<void(const HttpResponsePtr&)> callback
) const{
    const auto json = req->getJsonObject();
    if (!json) {
        Json::Value err;
        err["error"] = "缺少配置数据";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    Database::instance().saveKBConfig(*json);

    // 更新内存中的配置
    auto& kbConfig = Config::instance().knowledgeBase;
    kbConfig.enabled = json->get("enabled", true).asBool();
    kbConfig.apiKey = json->get("apiKey", "").asString();
    kbConfig.baseUrl = json->get("baseUrl", "").asString();
    kbConfig.knowledgeDatasetId = json->get("knowledgeDatasetId", "").asString();
    kbConfig.memoryDatasetId = json->get("memoryDatasetId", "").asString();
    kbConfig.memoryDocumentId = json->get("memoryDocumentId", "").asString();

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "知识库配置已保存";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== 群记忆 ====================

Task<> AdminController::getGroupMemory(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback,
    const std::string& groupId
) const{
    const uint64_t gid = std::stoull(groupId);
    const std::string memory = Database::instance().getShortTermMemory(gid);

    Json::Value resp;
    resp["groupId"] = static_cast<Json::UInt64>(gid);
    resp["memory"] = memory;
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::updateGroupMemory(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback,
    const std::string& groupId
) const{
    auto json = req->getJsonObject();
    if (!json || !json->isMember("memory")) {
        Json::Value err;
        err["error"] = "缺少memory字段";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    uint64_t gid = std::stoull(groupId);
    std::string memory = (*json)["memory"].asString();
    Database::instance().updateShortTermMemory(gid, memory);

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "记忆已更新";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== 记忆配置 ====================

Task<> AdminController::getMemoryConfig(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    auto config = Database::instance().getMemoryConfig();
    callback(HttpResponse::newHttpJsonResponse(config));
    co_return;
}

Task<> AdminController::saveMemoryConfig(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    auto json = req->getJsonObject();
    if (!json) {
        Json::Value err;
        err["error"] = "缺少配置数据";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    // 窗口配置校验: 保留条数必须小于触发条数,否则窗口永远滑不动
    if (!(*json).isMember("windowTriggerCount") || (*json)["windowTriggerCount"].asInt() <= 0) {
        (*json)["windowTriggerCount"] = Config::instance().windowTriggerCount;
    }
    if (!(*json).isMember("windowKeepCount") || (*json)["windowKeepCount"].asInt() <= 0
        || (*json)["windowKeepCount"].asInt() >= (*json)["windowTriggerCount"].asInt()) {
        (*json)["windowKeepCount"] = (*json)["windowTriggerCount"].asInt() / 2;
    }
    if (!(*json).isMember("memoryExtractMaxTokens") || (*json)["memoryExtractMaxTokens"].asInt() <= 0) {
        (*json)["memoryExtractMaxTokens"] = Config::instance().memoryExtractMaxTokens;
    }
    // Router 子窗口校验: 保留条数必须小于触发条数
    if (!(*json).isMember("routerWindowTriggerCount") || (*json)["routerWindowTriggerCount"].asInt() <= 0) {
        (*json)["routerWindowTriggerCount"] = Config::instance().routerWindowTriggerCount;
    }
    if (!(*json).isMember("routerWindowKeepCount") || (*json)["routerWindowKeepCount"].asInt() <= 0
        || (*json)["routerWindowKeepCount"].asInt() >= (*json)["routerWindowTriggerCount"].asInt()) {
        (*json)["routerWindowKeepCount"] = (*json)["routerWindowTriggerCount"].asInt() / 2;
    }

    Database::instance().saveMemoryConfig(*json);

    // 更新内存中的配置
    auto& config = Config::instance();
    config.windowTriggerCount = (*json)["windowTriggerCount"].asInt();
    config.windowKeepCount = (*json)["windowKeepCount"].asInt();
    config.memoryExtractMaxTokens = (*json)["memoryExtractMaxTokens"].asInt();
    config.routerWindowTriggerCount = (*json)["routerWindowTriggerCount"].asInt();
    config.routerWindowKeepCount = (*json)["routerWindowKeepCount"].asInt();
    config.shortTermMemoryMax = (*json)["shortTermMemoryMax"].asInt();
    config.memoryMigrateCount = (*json)["memoryMigrateCount"].asInt();

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "记忆配置已保存";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== QQ Bot 配置 ====================

Task<> AdminController::getQQConfig(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    auto config = Database::instance().getQQConfig();
    callback(HttpResponse::newHttpJsonResponse(config));
    co_return;
}

Task<> AdminController::saveQQConfig(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    auto json = req->getJsonObject();
    if (!json) {
        Json::Value err;
        err["error"] = "缺少配置数据";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    Database::instance().saveQQConfig(*json);

    // 更新内存中的配置
    auto& config = Config::instance();
    config.accessToken = (*json).get("accessToken", "").asString();
    config.selfQQNumber = (*json).get("selfQQNumber", 0).asInt64();
    config.qqHttpHost = (*json).get("qqHttpHost", "").asString();
    config.botName = (*json).get("botName", "小喵").asString();

    // 更新 QQMessage 的自定义名称
    QQMessage::setCustomQQName(config.selfQQNumber, config.botName + "(我)");

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "QQ Bot 配置已保存";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== 自定义工具 ====================

Task<> AdminController::getCustomTools(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    auto tools = Database::instance().getCustomTools();

    Json::Value result(Json::arrayValue);
    for (const auto& tool : tools) {
        Json::Value item;
        item["id"] = tool.id;
        item["name"] = tool.name;
        item["description"] = tool.description;
        item["parameters"] = tool.parameters;
        item["executorType"] = tool.executorType;
        item["executorConfig"] = tool.executorConfig;
        item["scriptContent"] = tool.scriptContent;
        item["readme"] = tool.readme;
        item["enabled"] = tool.enabled;
        result.append(item);
    }
    callback(HttpResponse::newHttpJsonResponse(result));
    co_return;
}

Task<> AdminController::addCustomTool(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    auto json = req->getJsonObject();
    if (!json || !json->isMember("name") || !json->isMember("executorType") || !json->isMember("executorConfig")) {
        Json::Value err;
        err["error"] = "缺少必要字段 (name, executorType, executorConfig)";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    std::string name = (*json)["name"].asString();

    // 检查是否与内置工具名冲突
    auto& registry = ToolRegistry::instance();
    if (registry.hasTool(name)) {
        Json::Value err;
        err["error"] = "工具名 '" + name + "' 已存在（内置工具或自定义工具）";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    Database::CustomTool tool;
    tool.name = name;
    tool.description = json->get("description", "").asString();
    tool.parameters = json->get("parameters", "").asString();
    tool.executorType = (*json)["executorType"].asString();
    tool.executorConfig = json->get("executorConfig", "").asString();
    tool.scriptContent = json->get("scriptContent", "").asString();
    tool.readme = json->get("readme", "").asString();
    tool.enabled = json->get("enabled", true).asBool();

    int id = Database::instance().addCustomTool(tool);

    // 立即注册到 ToolRegistry
    AgentToolManager::registerCustomTools();

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "自定义工具已添加";
    resp["id"] = id;
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::updateCustomTool(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback,
    const std::string& id
) const{
    auto json = req->getJsonObject();
    if (!json || !json->isMember("name") || !json->isMember("executorType") || !json->isMember("executorConfig")) {
        Json::Value err;
        err["error"] = "缺少必要字段 (name, executorType, executorConfig)";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    Database::CustomTool tool;
    tool.id = std::stoi(id);
    tool.name = (*json)["name"].asString();
    tool.description = json->get("description", "").asString();
    tool.parameters = json->get("parameters", "").asString();
    tool.executorType = (*json)["executorType"].asString();
    tool.executorConfig = json->get("executorConfig", "").asString();
    tool.scriptContent = json->get("scriptContent", "").asString();
    tool.readme = json->get("readme", "").asString();
    tool.enabled = json->get("enabled", true).asBool();

    Database::instance().updateCustomTool(tool);

    // 重新注册工具
    AgentToolManager::registerCustomTools();

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "自定义工具已更新";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::deleteCustomTool(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback,
    const std::string& id
) const{
    const int toolId = std::stoi(id);
    Database::instance().deleteCustomTool(toolId);

    // 重新注册工具（移除已删除的）
    AgentToolManager::registerCustomTools();

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "自定义工具已删除";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::toggleCustomTool(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback,
    const std::string& id
) const{
    int toolId = std::stoi(id);
    Database::instance().toggleCustomTool(toolId);

    // 重新注册工具
    AgentToolManager::registerCustomTools();

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "工具状态已切换";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::reloadCustomTools(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    AgentToolManager::registerCustomTools();

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "自定义工具已重新加载";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::testCustomTool(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    auto json = req->getJsonObject();
    if (!json) {
        Json::Value err;
        err["success"] = false;
        err["error"] = "缺少请求数据";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    // 支持两种方式：
    // 1. 传入 toolId 测试已保存的工具
    // 2. 传入工具定义直接测试（未保存）
    std::string executorType;
    std::string executorConfig;
    std::string scriptContent;
    Json::Value testArgs;

    if (json->isMember("toolId")) {
        // 从数据库加载工具
        int toolId = (*json)["toolId"].asInt();
        auto tools = Database::instance().getCustomTools();
        auto it =
            std::ranges::find_if(tools, [toolId](const auto& t) { return t.id == toolId; });
        if (it == tools.end()) {
            Json::Value err;
            err["success"] = false;
            err["error"] = "工具不存在";
            callback(HttpResponse::newHttpJsonResponse(err));
            co_return;
        }
        executorType = it->executorType;
        executorConfig = it->executorConfig;
        scriptContent = it->scriptContent;
        testArgs = json->isMember("args") ? (*json)["args"] : Json::Value();
    } else {
        // 直接使用传入的定义
        executorType = json->get("executorType", "python").asString();
        executorConfig = json->get("executorConfig", "").asString();
        scriptContent = json->get("scriptContent", "").asString();
        testArgs = json->isMember("args") ? (*json)["args"] : Json::Value();
    }

    std::string result;
    if (executorType == "python") {
        result = co_await AgentToolManager::executePythonTool(scriptContent, testArgs);
    } else if (executorType == "http") {
        result = co_await AgentToolManager::executeHttpTool(executorConfig, testArgs);
    } else {
        result = "未知的执行类型";
    }

    Json::Value resp;
    resp["success"] = true;
    resp["result"] = result;
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== 自定义工具配置 ====================

Task<> AdminController::getCustomToolConfig(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback
) const{
    Json::Value resp;
    resp["pythonPath"] = Database::instance().getCustomToolPython();
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::saveCustomToolConfig(
    const HttpRequestPtr req,
    const std::function<void(const HttpResponsePtr&)> callback
) const{
    const auto json = req->getJsonObject();
    if (!json || !json->isMember("pythonPath")) {
        Json::Value err;
        err["success"] = false;
        err["error"] = "缺少 pythonPath 字段";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    std::string pythonPath = (*json)["pythonPath"].asString();
    Database::instance().setCustomToolPython(pythonPath);

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "Python解释器路径已保存";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ============== 自定义工具导入导出 ==============

Task<> AdminController::exportCustomTool(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback,
    const std::string& id) const{

    int toolId = std::stoi(id);
    auto tools = Database::instance().getCustomTools();

    auto it = std::find_if(tools.begin(), tools.end(),
        [toolId](const Database::CustomTool& t) { return t.id == toolId; });

    if (it == tools.end()) {
        Json::Value resp;
        resp["success"] = false;
        resp["error"] = "工具不存在";
        callback(HttpResponse::newHttpJsonResponse(resp));
        co_return;
    }

    const auto& tool = *it;

    // 只支持导出 Python 工具
    if (tool.executorType != "python") {
        Json::Value resp;
        resp["success"] = false;
        resp["error"] = "仅支持导出 Python 类型工具";
        callback(HttpResponse::newHttpJsonResponse(resp));
        co_return;
    }

    // 构建导出 JSON（简化格式，不含 executorType）
    Json::Value exportJson;
    exportJson["name"] = tool.name;
    exportJson["description"] = tool.description;

    // 解析参数 JSON
    Json::Value params;
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::string errors;
    reader->parse(tool.parameters.c_str(), tool.parameters.c_str() + tool.parameters.size(), &params, &errors);
    exportJson["parameters"] = params;

    exportJson["scriptContent"] = tool.scriptContent;
    if (!tool.readme.empty()) {
        exportJson["readme"] = tool.readme;
    }
    exportJson["version"] = "1.0";

    // 返回 JSON 文件
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    std::string content = Json::writeString(writer, exportJson);

    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    resp->addHeader("Content-Disposition", "attachment; filename=\"" + tool.name + ".json\"");
    resp->setBody(content);
    callback(resp);
    co_return;
}

Task<> AdminController::importCustomTool(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback) const{

    auto json = req->getJsonObject();
    if (!json) {
        Json::Value resp;
        resp["success"] = false;
        resp["error"] = "无效的 JSON 数据";
        callback(HttpResponse::newHttpJsonResponse(resp));
        co_return;
    }

    // 检查必要字段
    if (!json->isMember("name") || !json->isMember("description") || !json->isMember("scriptContent")) {
        Json::Value resp;
        resp["success"] = false;
        resp["error"] = "缺少必要字段：name, description, scriptContent";
        callback(HttpResponse::newHttpJsonResponse(resp));
        co_return;
    }

    std::string name = (*json)["name"].asString();

    // 检查是否已存在同名工具
    if (Database::instance().hasCustomTool(name)) {
        Json::Value resp;
        resp["success"] = false;
        resp["error"] = "工具名已存在：" + name;
        callback(HttpResponse::newHttpJsonResponse(resp));
        co_return;
    }

    // 构建工具对象（强制使用 Python 类型）
    Database::CustomTool tool;
    tool.name = name;
    tool.description = (*json)["description"].asString();
    tool.executorType = "python";

    // 参数处理
    if (json->isMember("parameters")) {
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        tool.parameters = Json::writeString(writer, (*json)["parameters"]);
    } else {
        tool.parameters = "{\"type\":\"object\",\"properties\":{},\"required\":[]}";
    }

    tool.scriptContent = (*json)["scriptContent"].asString();
    tool.readme = json->get("readme", "").asString();
    tool.enabled = true;

    // 添加到数据库
    int newId = Database::instance().addCustomTool(tool);
    AgentToolManager::registerCustomTools();

    spdlog::info("导入自定义工具: {} (ID: {})", tool.name, newId);

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "工具已导入";
    resp["id"] = newId;
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}
