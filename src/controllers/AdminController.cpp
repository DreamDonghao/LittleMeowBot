#include <controllers/AdminController.hpp>
#include <model/QQMessage.hpp>
#include <agent/AgentSystem.hpp>
#include <agent/AgentToolManager.hpp>
#include <service/ToolRegistry.hpp>
#include <service/OneBotClient.hpp>
#include <spdlog/spdlog.h>
#include <config/Config.hpp>
#include <util/HttpUtil.hpp>
#include <util/HttpTrace.hpp>
#include <util/Logger.hpp>
#include <util/CommonUtil.hpp>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <storage/AdminStore.hpp>
#include <storage/AffinityStore.hpp>
#include <storage/ChatRecordStore.hpp>
#include <storage/ConfigStore.hpp>
#include <storage/MemoryStore.hpp>
#include <storage/PromptStore.hpp>
#include <storage/SessionStore.hpp>
#include <storage/ToolStore.hpp>
#include <storage/UsageStore.hpp>

using namespace insoulforge;
using namespace drogon;

namespace {
    // 进程启动时间（文件作用域 static，程序启动时初始化）
    const auto g_processStartTime = std::chrono::system_clock::now();

    uint64_t parseQueryUInt64(const HttpRequestPtr &req, const std::string &name, uint64_t fallback = 0) {
        const auto value = req->getParameter(name);
        return value.empty() ? fallback : parseUInt64(value, fallback);
    }
}

// ==================== LLM配置 ====================

Task<> AdminController::getLLMConfigs(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
    const auto configs = ConfigStore::instance().getAllLLMConfigs();
    callback(HttpResponse::newHttpJsonResponse(configs));
    co_return;
}

Task<> AdminController::saveLLMConfig(
    const HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
    const auto json = req->getJsonObject();
    if (!json || !json->isMember("name")) {
        Json::Value err;
        err["error"] = "缺少name字段";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    const std::string name = (*json)["name"].asString();
    ConfigStore::instance().saveLLMConfig(name, *json);

    // 更新内存中的配置
    auto &config = Config::instance();
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
    std::function<void(const HttpResponsePtr &)> callback
) const {
    const auto prompts = PromptStore::instance().getAllPrompts();

    Json::Value result;
    for (const auto &[key, content]: prompts) {
        result[key] = content;
    }
    callback(HttpResponse::newHttpJsonResponse(result));
    co_return;
}

Task<> AdminController::savePrompt(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
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

    // 防护: router 提示词的 JSON 格式示例若含双花括号(fmt 转义残留/旧页面缓存内容),模型会照抄导致解析失败
    if ((key == "router_system" || key == "router_private_system")
        && (content.find("{{") != std::string::npos || content.find("}}") != std::string::npos)) {
        Json::Value err;
        err["success"] = false;
        err["error"] = "提示词包含双花括号{{ }}，JSON 格式示例应为单花括号，请刷新页面后重试";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    PromptStore::instance().setPrompt(key, content, description);
    spdlog::warn("管理后台更新提示词: key={}, 长度={}", key, content.size());

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "提示词已保存";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== 运行日志 ====================

Task<> AdminController::getLogs(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
    LogQuery query;

    // 线上参数沿用 "groupId"（内部语义为 sessionId）
    if (const std::string groupIdParam = req->getParameter("groupId"); !groupIdParam.empty()) {
        if (groupIdParam == "system") {
            query.systemOnly = true;
        } else {
            query.sessionId = parseUInt64(groupIdParam);
        }
    }

    if (const std::string level = req->getParameter("level"); !level.empty() && level != "all") {
        query.level = level;
    }

    query.keyword = req->getParameter("keyword");
    query.afterId = parseQueryUInt64(req, "afterId");
    query.beforeId = tryParseUInt64(req->getParameter("beforeId"));
    query.limit = static_cast<int>(std::clamp<uint64_t>(parseQueryUInt64(req, "limit", 200), 1, 1000));

    const auto result = LogBuffer::instance().query(query);

    Json::Value resp;
    resp["entries"] = Json::arrayValue;
    for (const auto &entry: result.entries) {
        Json::Value item;
        item["id"] = entry.id;
        item["timestamp"] = entry.timestamp;
        item["level"] = entry.level;
        item["message"] = entry.message;
        if (entry.sessionId.has_value()) {
            // 字符串形式：会话 ID 可能带私聊标志位，超过 JS 安全整数范围
            item["groupId"] = std::to_string(*entry.sessionId);
        } else {
            item["groupId"] = Json::nullValue;
        }
        resp["entries"].append(item);
    }
    resp["hasMore"] = result.hasMore;
    resp["nextAfterId"] = result.nextAfterId;
    resp["nextBeforeId"] = result.nextBeforeId;
    resp["oldestId"] = result.oldestId;
    resp["newestId"] = result.newestId;
    resp["size"] = static_cast<Json::UInt64>(LogBuffer::instance().size());
    resp["currentLevel"] = Logger::level();
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== HTTP 请求调试 ====================

Task<> AdminController::getHttpTraces(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
    const auto afterId = parseQueryUInt64(req, "afterId");
    const auto limit = std::clamp<size_t>(parseQueryUInt64(req, "limit", 50), 1, 500);

    Json::Value resp;
    resp["entries"] = Json::arrayValue;
    resp["total"] = static_cast<Json::UInt64>(HttpTrace::instance().size());
    for (auto &entry: HttpTrace::instance().query(afterId, limit)) {
        Json::Value item;
        item["id"] = entry.id;
        item["timestamp"] = entry.timestamp;
        item["tag"] = entry.tag;
        item["method"] = entry.method;
        item["url"] = entry.url;
        item["status"] = entry.status;
        // 字符串形式：会话 ID 可能带私聊标志位，超过 JS 安全整数范围
        if (entry.sessionId.has_value()) {
            item["groupId"] = std::to_string(*entry.sessionId);
        } else {
            item["groupId"] = Json::nullValue;
        }
        item["requestBody"] = entry.requestBody;
        item["responseBody"] = entry.responseBody;
        resp["entries"].append(item);
    }
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::clearHttpTraces(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
    HttpTrace::instance().clear();
    Json::Value resp;
    resp["success"] = true;
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== 用量统计 ====================

Task<> AdminController::getUsage(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
    int days = 30;
    if (const std::string p = req->getParameter("days"); !p.empty()) {
        // 与 stoi 行为一致：跳过前导空白，解析失败时保留默认值
        const auto *begin = p.data();
        const auto *end = p.data() + p.size();
        while (begin < end && (*begin == ' ' || *begin == '\t')) ++begin;
        std::from_chars(begin, end, days);
    }
    days = std::clamp(days, 1, 365);

    Json::Value resp = UsageStore::instance().getUsageSummary(days);
    resp["recent"] = UsageStore::instance().getRecentUsage(50);
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== 运行信息 ====================

Task<> AdminController::getSystemInfo(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
    const auto now = std::chrono::system_clock::now();
    const auto uptimeSeconds = std::chrono::duration_cast<std::chrono::seconds>(now - g_processStartTime).count();
    const auto startEpoch = std::chrono::duration_cast<std::chrono::seconds>(g_processStartTime.time_since_epoch()).
            count();

    Json::Value resp;
    resp["startTime"] = startEpoch;
    resp["uptimeSeconds"] = uptimeSeconds;
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::getBotStatus(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
    Json::Value resp;
    resp["running"] = AgentSystem::instance().isRunning();
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::setBotStatus(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
    const auto json = req->getJsonObject();
    if (!json || !json->isMember("running") || !(*json)["running"].isBool()) {
        Json::Value err;
        err["success"] = false;
        err["error"] = "running字段必须为布尔值";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    const bool running = (*json)["running"].asBool();
    AgentSystem::instance().setRunning(running);
    spdlog::warn("管理后台{}机器人", running ? "打开" : "暂停");

    Json::Value resp;
    resp["success"] = true;
    resp["running"] = running;
    resp["message"] = running ? "机器人已打开" : "机器人已暂停";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== 表情包库（QQ 收藏表情，以实际收藏为基准） ====================

Task<> AdminController::getEmojis(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
    callback(HttpResponse::newHttpJsonResponse(co_await AgentToolManager::fetchFavoriteEmojis()));
    co_return;
}

Task<> AdminController::updateEmojiDesc(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
    auto json = req->getJsonObject();
    if (!json || !json->isMember("res_id") || !json->isMember("desc")) {
        Json::Value err;
        err["error"] = "缺少必要字段: res_id、desc";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    const std::string resId = (*json)["res_id"].asString();
    const std::string desc = (*json)["desc"].asString();
    if (!co_await OneBotClient::setCustomFaceDesc(
            json->isMember("emoji_id") ? (*json)["emoji_id"].asString() : "0",
            resId, json->isMember("md5") ? (*json)["md5"].asString() : "", desc)) {
        Json::Value err;
        err["error"] = "修改表情描述失败，请确认 QQ 客户端在线";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    AgentToolManager::invalidateFavoriteEmojiCache();
    spdlog::info("[Admin] 已修改表情描述: res_id={} desc={}",
                 resId, desc);

    Json::Value respJson2;
    respJson2["success"] = true;
    respJson2["message"] = "描述已修改";
    callback(HttpResponse::newHttpJsonResponse(respJson2));
    co_return;
}

// ==================== 管理员 ====================

Task<> AdminController::getAdmins(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
    const auto admins = AdminStore::instance().getAdmins();

    Json::Value result(Json::arrayValue);
    for (const uint64_t qq: admins) {
        Json::Value admin;
        admin["qq"] = qq;
        result.append(admin);
    }
    callback(HttpResponse::newHttpJsonResponse(result));
    co_return;
}

Task<> AdminController::addAdmin(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
    const auto json = req->getJsonObject();
    if (!json || !json->isMember("qq")) {
        Json::Value err;
        err["error"] = "缺少qq字段";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    const uint64_t qq = (*json)["qq"].asUInt64();
    AdminStore::instance().addAdmin(qq);

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "管理员已添加";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::removeAdmin(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback,
    const std::string &qq
) const {
    const uint64_t qqNum = std::stoull(qq);
    AdminStore::instance().removeAdmin(qqNum);

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "管理员已删除";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== 启用群 ====================

Task<> AdminController::getGroups(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
    auto groups = SessionStore::instance().getAllSessionsWithStatus();

    Json::Value result(Json::arrayValue);
    for (const auto &[sessionId, groupName, enabled, messageCount]: groups) {
        Json::Value group;
        // 会话 ID 可能带私聊标志位（超过 JS Number 安全范围），同步提供字符串形式
        group["groupId"] = sessionId;
        group["groupIdStr"] = std::to_string(sessionId);
        if (QQMessage::isPrivateSession(sessionId)) {
            group["sessionType"] = "private";
            group["userId"] = sessionId & ~QQMessage::kPrivateSessionFlag;
        }
        group["groupName"] = groupName;
        group["enabled"] = enabled;
        group["messageCount"] = messageCount;
        result.append(group);
    }
    callback(HttpResponse::newHttpJsonResponse(result));
    co_return;
}

Task<> AdminController::enableSession(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
    auto json = req->getJsonObject();
    if (!json || (!json->isMember("sessionId") && !json->isMember("userId"))) {
        Json::Value err;
        err["error"] = "缺少groupId或userId字段";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    uint64_t sessionId = 0;
    if (json->isMember("sessionType") && (*json)["sessionType"].asString() == "private") {
        // 私聊会话 ID 带标志位，由后端按 QQ 号构造（前端无法安全表示超出 JS 精度的大数）
        const uint64_t userId = jsonToUInt64((*json)["userId"]);
        if (userId == 0) {
            Json::Value err;
            err["error"] = "QQ号无效";
            callback(HttpResponse::newHttpJsonResponse(err));
            co_return;
        }
        sessionId = userId | QQMessage::kPrivateSessionFlag;
    } else {
        // 前端以字符串传递（避免 JS 大数精度丢失），需安全解析而非 asUInt64()
        sessionId = jsonToUInt64((*json)["groupId"]);
        if (sessionId == 0 || QQMessage::isPrivateSession(sessionId)) {
            Json::Value err;
            err["error"] = "群号无效";
            callback(HttpResponse::newHttpJsonResponse(err));
            co_return;
        }
    }
    SessionStore::instance().enableSession(sessionId);

    // 自动获取会话名称（群聊为群名，私聊为 QQ 昵称）
    std::string groupName = co_await MessageService::fetchAndUpdateSessionName(sessionId);

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = QQMessage::isPrivateSession(sessionId) ? "私聊已启用" : "群已启用";
    resp["groupName"] = groupName;
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::toggleSession(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback,
    const std::string &sessionId
) const {
    const uint64_t gid = std::stoull(sessionId);
    SessionStore::instance().toggleSessionStatus(gid);

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "群状态已切换";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::removeSession(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback,
    const std::string &sessionId
) const {
    const uint64_t gid = std::stoull(sessionId);
    SessionStore::instance().disableSession(gid);

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "群已删除";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::refreshSessionName(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback,
    const std::string &sessionId
) const {
    const uint64_t gid = std::stoull(sessionId);
    const auto groupName = co_await MessageService::fetchAndUpdateSessionName(gid);

    Json::Value resp;
    resp["success"] = true;
    resp["groupName"] = groupName;
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::refreshAllSessionNames(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
    auto groups = SessionStore::instance().getAllSessionsWithStatus();

    for (const auto &[sessionId, groupName, enabled, messageCount]: groups) {
        co_await MessageService::fetchAndUpdateSessionName(sessionId);
    }

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "所有会话名称已刷新";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== 聊天记录 ====================

Task<> AdminController::getChatSessions(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
    auto groups = SessionStore::instance().getSessionsWithChatRecords();

    Json::Value result(Json::arrayValue);
    for (const auto &[sessionId, groupName, messageCount]: groups) {
        Json::Value group;
        group["groupId"] = sessionId;
        group["groupIdStr"] = std::to_string(sessionId);
        if (QQMessage::isPrivateSession(sessionId)) {
            group["sessionType"] = "private";
            group["userId"] = sessionId & ~QQMessage::kPrivateSessionFlag;
        }
        group["groupName"] = groupName;
        group["messageCount"] = messageCount;
        result.append(group);
    }
    callback(HttpResponse::newHttpJsonResponse(result));
    co_return;
}

Task<> AdminController::getChatRecords(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback,
    const std::string &sessionId
) const {
    const uint64_t gid = std::stoull(sessionId);

    // 支持limit参数
    int limit = 50;
    if (const std::string limitParam = req->getParameter("limit"); !limitParam.empty()) {
        limit = std::stoi(limitParam);
    }

    // 返回带ID的记录，支持编辑
    const auto result = ChatRecordStore::instance().getChatRecordsWithIds(gid, limit);

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
    std::function<void(const HttpResponsePtr &)> callback,
    const std::string &recordId
) const {
    const auto json = req->getJsonObject();
    if (!json || !json->isMember("content")) {
        Json::Value err;
        err["error"] = "缺少content字段";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    const int id = std::stoi(recordId);
    const std::string content = (*json)["content"].asString();
    ChatRecordStore::instance().updateChatRecord(id, content);

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "聊天记录已更新";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::deleteChatRecord(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback,
    const std::string &recordId
) const {
    const int id = std::stoi(recordId);
    ChatRecordStore::instance().deleteChatRecord(id);

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "聊天记录已删除";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::clearSessionChatRecords(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback,
    const std::string &sessionId
) const {
    const uint64_t gid = std::stoull(sessionId);
    ChatRecordStore::instance().clearSessionChatRecords(gid);

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "聊天记录已清空";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== 知识库配置 ====================

Task<> AdminController::getKBConfig(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
    const auto config = ConfigStore::instance().getKBConfig();
    callback(HttpResponse::newHttpJsonResponse(config));
    co_return;
}

Task<> AdminController::saveKBConfig(
    const HttpRequestPtr req,
    const std::function<void(const HttpResponsePtr &)> callback
) const {
    const auto json = req->getJsonObject();
    if (!json) {
        Json::Value err;
        err["error"] = "缺少配置数据";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    ConfigStore::instance().saveKBConfig(*json);

    // 更新内存中的配置
    auto &kbConfig = Config::instance().knowledgeBase;
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

Task<> AdminController::getSessionMemory(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback,
    const std::string &sessionId
) const {
    const uint64_t gid = std::stoull(sessionId);
    const std::string memory = MemoryStore::instance().getShortTermMemory(gid);

    Json::Value resp;
    resp["groupId"] = gid;
    resp["memory"] = memory;
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::updateSessionMemory(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback,
    const std::string &sessionId
) const {
    const auto json = req->getJsonObject();
    if (!json || !json->isMember("memory")) {
        Json::Value err;
        err["error"] = "缺少memory字段";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    const uint64_t gid = std::stoull(sessionId);
    const std::string memory = (*json)["memory"].asString();
    MemoryStore::instance().updateShortTermMemory(gid, memory);

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "记忆已更新";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== 好感度 ====================

Task<> AdminController::getSessionAffinity(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback,
    const std::string &sessionId
) const {
    const uint64_t gid = std::stoull(sessionId);
    auto affinityMap = AffinityStore::instance().getAffinityMap(gid);

    std::vector<std::pair<uint64_t, int> > entries(affinityMap.begin(), affinityMap.end());
    std::ranges::sort(entries, [](const auto &a, const auto &b) { return a.second > b.second; });

    Json::Value list(Json::arrayValue);
    for (const auto &[qq, affinity]: entries) {
        Json::Value item;
        // QQ 号以字符串返回（超过 JS Number 安全范围）
        item["qq"] = std::to_string(qq);
        // 昵称优先取运行时映射（含自定义昵称），缺失时回退 OneBot 实时查询
        std::string name(QQMessage::getQQName(qq));
        if (name.empty() || name == "未知") {
            const auto info = co_await OneBotClient::getStrangerInfo(qq, gid);
            if (info.isMember("data") && info["data"].isMember("nickname")) {
                name = info["data"]["nickname"].asString();
            }
        }
        if (!name.empty() && name != "未知") {
            item["name"] = name;
        }
        item["affinity"] = affinity;
        list.append(item);
    }

    Json::Value resp;
    resp["groupIdStr"] = std::to_string(gid);
    resp["affinities"] = list;
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== 记忆配置 ====================

Task<> AdminController::getMemoryConfig(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
    const auto config = ConfigStore::instance().getMemoryConfig();
    callback(HttpResponse::newHttpJsonResponse(config));
    co_return;
}

Task<> AdminController::saveMemoryConfig(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
    const auto json = req->getJsonObject();
    if (!json) {
        Json::Value err;
        err["error"] = "缺少配置数据";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    // 窗口配置校验: 保留条数必须小于触发条数,否则窗口永远滑不动
    if (!json->isMember("windowTriggerCount") || (*json)["windowTriggerCount"].asInt() <= 0) {
        (*json)["windowTriggerCount"] = Config::instance().windowTriggerCount;
    }
    if (!json->isMember("windowKeepCount") || (*json)["windowKeepCount"].asInt() <= 0
        || (*json)["windowKeepCount"].asInt() >= (*json)["windowTriggerCount"].asInt()) {
        (*json)["windowKeepCount"] = (*json)["windowTriggerCount"].asInt() / 2;
    }
    if (!json->isMember("memoryExtractMaxTokens") || (*json)["memoryExtractMaxTokens"].asInt() <= 0) {
        (*json)["memoryExtractMaxTokens"] = Config::instance().memoryExtractMaxTokens;
    }
    // Router 子窗口校验: 保留条数必须小于触发条数
    if (!json->isMember("routerWindowTriggerCount") || (*json)["routerWindowTriggerCount"].asInt() <= 0) {
        (*json)["routerWindowTriggerCount"] = Config::instance().routerWindowTriggerCount;
    }
    if (!json->isMember("routerWindowKeepCount") || (*json)["routerWindowKeepCount"].asInt() <= 0
        || (*json)["routerWindowKeepCount"].asInt() >= (*json)["routerWindowTriggerCount"].asInt()) {
        (*json)["routerWindowKeepCount"] = (*json)["routerWindowTriggerCount"].asInt() / 2;
    }

    ConfigStore::instance().saveMemoryConfig(*json);

    // 更新内存中的配置
    auto &config = Config::instance();
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
    std::function<void(const HttpResponsePtr &)> callback
) const {
    const auto config = ConfigStore::instance().getQQConfig();
    callback(HttpResponse::newHttpJsonResponse(config));
    co_return;
}

Task<> AdminController::saveQQConfig(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
    const auto json = req->getJsonObject();
    if (!json) {
        Json::Value err;
        err["error"] = "缺少配置数据";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    ConfigStore::instance().saveQQConfig(*json);

    // 更新内存中的配置
    auto &config = Config::instance();
    config.accessToken = json->get("accessToken", "").asString();
    config.selfQQNumber = json->get("selfQQNumber", 0).asInt64();
    config.qqHttpHost = json->get("qqHttpHost", "").asString();
    config.botName = json->get("botName", "小喵").asString();

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
    std::function<void(const HttpResponsePtr &)> callback
) const {
    const auto tools = ToolStore::instance().getCustomTools();

    Json::Value result(Json::arrayValue);
    for (const auto &tool: tools) {
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
    std::function<void(const HttpResponsePtr &)> callback
) const {
    const auto json = req->getJsonObject();
    if (!json || !json->isMember("name") || !json->isMember("executorType") || !json->isMember("executorConfig")) {
        Json::Value err;
        err["error"] = "缺少必要字段 (name, executorType, executorConfig)";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    const std::string name = (*json)["name"].asString();

    // 检查是否与内置工具名冲突
    const auto &registry = ToolRegistry::instance();
    if (registry.hasTool(name)) {
        Json::Value err;
        err["error"] = "工具名 '" + name + "' 已存在（内置工具或自定义工具）";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    ToolStore::CustomTool tool;
    tool.name = name;
    tool.description = json->get("description", "").asString();
    tool.parameters = json->get("parameters", "").asString();
    tool.executorType = (*json)["executorType"].asString();
    tool.executorConfig = json->get("executorConfig", "").asString();
    tool.scriptContent = json->get("scriptContent", "").asString();
    tool.readme = json->get("readme", "").asString();
    tool.enabled = json->get("enabled", true).asBool();

    const int id = ToolStore::instance().addCustomTool(tool);

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
    std::function<void(const HttpResponsePtr &)> callback,
    const std::string &id
) const {
    const auto json = req->getJsonObject();
    if (!json || !json->isMember("name") || !json->isMember("executorType") || !json->isMember("executorConfig")) {
        Json::Value err;
        err["error"] = "缺少必要字段 (name, executorType, executorConfig)";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    ToolStore::CustomTool tool;
    tool.id = std::stoi(id);
    tool.name = (*json)["name"].asString();
    tool.description = json->get("description", "").asString();
    tool.parameters = json->get("parameters", "").asString();
    tool.executorType = (*json)["executorType"].asString();
    tool.executorConfig = json->get("executorConfig", "").asString();
    tool.scriptContent = json->get("scriptContent", "").asString();
    tool.readme = json->get("readme", "").asString();
    tool.enabled = json->get("enabled", true).asBool();

    ToolStore::instance().updateCustomTool(tool);

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
    std::function<void(const HttpResponsePtr &)> callback,
    const std::string &id
) const {
    const int toolId = std::stoi(id);
    ToolStore::instance().deleteCustomTool(toolId);

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
    std::function<void(const HttpResponsePtr &)> callback,
    const std::string &id
) const {
    const int toolId = std::stoi(id);
    ToolStore::instance().toggleCustomTool(toolId);

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
    std::function<void(const HttpResponsePtr &)> callback
) const {
    AgentToolManager::registerCustomTools();

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "自定义工具已重新加载";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::testCustomTool(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback
) const {
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
        auto tools = ToolStore::instance().getCustomTools();
        auto it =
                std::ranges::find_if(tools, [toolId](const auto &t) { return t.id == toolId; });
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
    std::function<void(const HttpResponsePtr &)> callback
) const {
    Json::Value resp;
    resp["pythonPath"] = ToolStore::instance().getCustomToolPython();
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::saveCustomToolConfig(
    const HttpRequestPtr req,
    const std::function<void(const HttpResponsePtr &)> callback
) const {
    const auto json = req->getJsonObject();
    if (!json || !json->isMember("pythonPath")) {
        Json::Value err;
        err["success"] = false;
        err["error"] = "缺少 pythonPath 字段";
        callback(HttpResponse::newHttpJsonResponse(err));
        co_return;
    }

    const std::string pythonPath = (*json)["pythonPath"].asString();
    ToolStore::instance().setCustomToolPython(pythonPath);

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "Python解释器路径已保存";
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ============== 自定义工具导入导出 ==============

Task<> AdminController::exportCustomTool(
    HttpRequestPtr req,
    std::function<void(const HttpResponsePtr &)> callback,
    const std::string &id) const {
    int toolId = std::stoi(id);
    auto tools = ToolStore::instance().getCustomTools();

    auto it = std::ranges::find_if(tools,
                                   [toolId](const ToolStore::CustomTool &t) { return t.id == toolId; });

    if (it == tools.end()) {
        Json::Value resp;
        resp["success"] = false;
        resp["error"] = "工具不存在";
        callback(HttpResponse::newHttpJsonResponse(resp));
        co_return;
    }

    const auto &tool = *it;

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
    std::function<void(const HttpResponsePtr &)> callback) const {
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
    if (ToolStore::instance().hasCustomTool(name)) {
        Json::Value resp;
        resp["success"] = false;
        resp["error"] = "工具名已存在：" + name;
        callback(HttpResponse::newHttpJsonResponse(resp));
        co_return;
    }

    // 构建工具对象（强制使用 Python 类型）
    ToolStore::CustomTool tool;
    tool.name = name;
    tool.description = (*json)["description"].asString();
    tool.executorType = "python";

    // 参数处理
    if (json->isMember("parameters")) {
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        tool.parameters = Json::writeString(writer, (*json)["parameters"]);
    } else {
        tool.parameters = R"({"type":"object","properties":{},"required":[]})";
    }

    tool.scriptContent = (*json)["scriptContent"].asString();
    tool.readme = json->get("readme", "").asString();
    tool.enabled = true;

    // 添加到数据库
    int newId = ToolStore::instance().addCustomTool(tool);
    AgentToolManager::registerCustomTools();

    spdlog::info("导入自定义工具: {} (ID: {})", tool.name, newId);

    Json::Value resp;
    resp["success"] = true;
    resp["message"] = "工具已导入";
    resp["id"] = newId;
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}
