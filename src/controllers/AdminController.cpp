#include <agent/AgentSystem.hpp>
#include <agent/AgentToolManager.hpp>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <config/Config.hpp>
#include <controllers/AdminController.hpp>
#include <controllers/AdminResponse.hpp>
#include <model/QQMessage.hpp>
#include <service/OneBotClient.hpp>
#include <service/TaskScheduler.hpp>
#include <spdlog/spdlog.h>
#include <storage/AdminStore.hpp>
#include <storage/AffinityStore.hpp>
#include <storage/ChatRecordStore.hpp>
#include <storage/ConfigStore.hpp>
#include <storage/LongTermMemoryStore.hpp>
#include <storage/MemoryStore.hpp>
#include <storage/SessionStore.hpp>
#include <storage/TaskStore.hpp>
#include <storage/UsageStore.hpp>
#include <util/CommonUtil.hpp>
#include <util/HttpTrace.hpp>
#include <util/HttpUtil.hpp>
#include <util/Logger.hpp>

using namespace insoulforge;
using namespace drogon;

namespace {
    // 进程启动时间（文件作用域 static，程序启动时初始化）
    const auto g_processStartTime = std::chrono::system_clock::now();

    uint64_t parseQueryUInt64(const HttpRequestPtr &req, const std::string &name, uint64_t fallback = 0) {
        const auto value = req->getParameter(name);
        return value.empty() ? fallback : parseUInt64(value, fallback);
    }

    /// @brief 会话列表项的公共头部字段（会话 ID 数值+字符串形式，私聊附带类型与 QQ 号）
    Json::Value sessionItemHeader(const uint64_t sessionId) {
        Json::Value item;
        // 会话 ID 可能带私聊标志位（超过 JS Number 安全范围），同步提供字符串形式
        item["groupId"] = sessionId;
        item["groupIdStr"] = std::to_string(sessionId);
        if (QQMessage::isPrivateSession(sessionId)) {
            item["sessionType"] = "private";
            item["userId"] = sessionId & ~QQMessage::kPrivateSessionFlag;
        }
        return item;
    }
} // namespace

// ==================== 运行日志 ====================

Task<> AdminController::getLogs(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
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

Task<> AdminController::getHttpTraces(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
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
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    HttpTrace::instance().clear();
    callback(HttpResponse::newHttpJsonResponse(AdminResponse::okJson()));
    co_return;
}

// ==================== 用量统计 ====================

Task<> AdminController::getUsage(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    int days = 30;
    if (const std::string p = req->getParameter("days"); !p.empty()) {
        // 与 stoi 行为一致：跳过前导空白，解析失败时保留默认值
        const auto *begin = p.data();
        const auto *end = p.data() + p.size();
        while (begin < end && (*begin == ' ' || *begin == '\t'))
            ++begin;
        std::from_chars(begin, end, days);
    }
    days = std::clamp(days, 1, 365);

    Json::Value resp = UsageStore::getUsageSummary(days);
    resp["recent"] = UsageStore::getRecentUsage(50);
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== 运行信息 ====================

Task<> AdminController::getSystemInfo(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    const auto now = std::chrono::system_clock::now();
    const auto uptimeSeconds = std::chrono::duration_cast<std::chrono::seconds>(now - g_processStartTime).count();
    const auto startEpoch =
      std::chrono::duration_cast<std::chrono::seconds>(g_processStartTime.time_since_epoch()).count();

    Json::Value resp;
    resp["startTime"] = startEpoch;
    resp["uptimeSeconds"] = uptimeSeconds;
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::getBotStatus(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    Json::Value resp;
    resp["running"] = AgentSystem::instance().isRunning();
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::setBotStatus(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    const auto json = req->getJsonObject();
    if (!json || !json->isMember("running") || !(*json)["running"].isBool()) {
        callback(HttpResponse::newHttpJsonResponse(AdminResponse::failJson("running字段必须为布尔值")));
        co_return;
    }

    const bool running = (*json)["running"].asBool();
    AgentSystem::instance().setRunning(running);
    spdlog::warn("管理后台{}机器人", running ? "打开" : "暂停");

    Json::Value resp = AdminResponse::okJson(running ? "机器人已打开" : "机器人已暂停");
    resp["running"] = running;
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== 表情包库（QQ 收藏表情，以实际收藏为基准） ====================

Task<> AdminController::getEmojis(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    callback(HttpResponse::newHttpJsonResponse(co_await AgentToolManager::fetchFavoriteEmojis()));
    co_return;
}

Task<> AdminController::updateEmojiDesc(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    auto json = req->getJsonObject();
    if (!json || !json->isMember("res_id") || !json->isMember("desc")) {
        callback(HttpResponse::newHttpJsonResponse(AdminResponse::errorJson("缺少必要字段: res_id、desc")));
        co_return;
    }

    const std::string resId = (*json)["res_id"].asString();
    const std::string desc = (*json)["desc"].asString();
    if (!co_await OneBotClient::setCustomFaceDesc(json->isMember("emoji_id") ? (*json)["emoji_id"].asString() : "0",
          resId, json->isMember("md5") ? (*json)["md5"].asString() : "", desc)) {
        callback(HttpResponse::newHttpJsonResponse(AdminResponse::errorJson("修改表情描述失败，请确认 QQ 客户端在线")));
        co_return;
    }

    AgentToolManager::invalidateFavoriteEmojiCache();
    spdlog::info("[Admin] 已修改表情描述: res_id={} desc={}", resId, desc);

    callback(HttpResponse::newHttpJsonResponse(AdminResponse::okJson("描述已修改")));
    co_return;
}

// ==================== 管理员 ====================

Task<> AdminController::getAdmins(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    const auto admins = AdminStore::getAdmins();

    Json::Value result(Json::arrayValue);
    for (const uint64_t qq: admins) {
        Json::Value admin;
        admin["qq"] = qq;
        result.append(admin);
    }
    callback(HttpResponse::newHttpJsonResponse(result));
    co_return;
}

Task<> AdminController::addAdmin(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    const auto json = req->getJsonObject();
    if (!json || !json->isMember("qq")) {
        callback(HttpResponse::newHttpJsonResponse(AdminResponse::errorJson("缺少qq字段")));
        co_return;
    }

    const uint64_t qq = (*json)["qq"].asUInt64();
    AdminStore::addAdmin(qq);

    callback(HttpResponse::newHttpJsonResponse(AdminResponse::okJson("管理员已添加")));
    co_return;
}

Task<> AdminController::removeAdmin(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, const std::string &qq) const {
    const uint64_t qqNum = std::stoull(qq);
    AdminStore::removeAdmin(qqNum);

    callback(HttpResponse::newHttpJsonResponse(AdminResponse::okJson("管理员已删除")));
    co_return;
}

// ==================== 启用群 ====================

Task<> AdminController::getGroups(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    auto groups = SessionStore::getAllSessionsWithStatus();

    Json::Value result(Json::arrayValue);
    for (const auto &[sessionId, groupName, enabled, messageCount]: groups) {
        Json::Value group = sessionItemHeader(sessionId);
        group["groupName"] = groupName;
        group["enabled"] = enabled;
        group["messageCount"] = messageCount;
        result.append(group);
    }
    callback(HttpResponse::newHttpJsonResponse(result));
    co_return;
}

Task<> AdminController::enableSession(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    auto json = req->getJsonObject();
    if (!json || (!json->isMember("sessionId") && !json->isMember("userId"))) {
        callback(HttpResponse::newHttpJsonResponse(AdminResponse::errorJson("缺少groupId或userId字段")));
        co_return;
    }

    uint64_t sessionId = 0;
    if (json->isMember("sessionType") && (*json)["sessionType"].asString() == "private") {
        // 私聊会话 ID 带标志位，由后端按 QQ 号构造（前端无法安全表示超出 JS 精度的大数）
        const uint64_t userId = jsonToUInt64((*json)["userId"]);
        if (userId == 0) {
            callback(HttpResponse::newHttpJsonResponse(AdminResponse::errorJson("QQ号无效")));
            co_return;
        }
        sessionId = userId | QQMessage::kPrivateSessionFlag;
    } else {
        // 前端以字符串传递（避免 JS 大数精度丢失），需安全解析而非 asUInt64()
        sessionId = jsonToUInt64((*json)["groupId"]);
        if (sessionId == 0 || QQMessage::isPrivateSession(sessionId)) {
            callback(HttpResponse::newHttpJsonResponse(AdminResponse::errorJson("群号无效")));
            co_return;
        }
    }
    SessionStore::enableSession(sessionId);

    // 自动获取会话名称（群聊为群名，私聊为 QQ 昵称）
    std::string groupName = co_await MessageService::fetchAndUpdateSessionName(sessionId);

    Json::Value resp = AdminResponse::okJson(QQMessage::isPrivateSession(sessionId) ? "私聊已启用" : "群已启用");
    resp["groupName"] = groupName;
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::toggleSession(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, const std::string &sessionId) const {
    const uint64_t gid = std::stoull(sessionId);
    SessionStore::toggleSessionStatus(gid);

    callback(HttpResponse::newHttpJsonResponse(AdminResponse::okJson("群状态已切换")));
    co_return;
}

Task<> AdminController::removeSession(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, const std::string &sessionId) const {
    const uint64_t gid = std::stoull(sessionId);
    SessionStore::disableSession(gid);

    callback(HttpResponse::newHttpJsonResponse(AdminResponse::okJson("群已删除")));
    co_return;
}

Task<> AdminController::refreshSessionName(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, const std::string &sessionId) const {
    const uint64_t gid = std::stoull(sessionId);
    const auto groupName = co_await MessageService::fetchAndUpdateSessionName(gid);

    Json::Value resp = AdminResponse::okJson();
    resp["groupName"] = groupName;
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::refreshAllSessionNames(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    auto groups = SessionStore::getAllSessionsWithStatus();

    for (const auto &[sessionId, groupName, enabled, messageCount]: groups) {
        co_await MessageService::fetchAndUpdateSessionName(sessionId);
    }

    callback(HttpResponse::newHttpJsonResponse(AdminResponse::okJson("所有会话名称已刷新")));
    co_return;
}

// ==================== 聊天记录 ====================

Task<> AdminController::getChatSessions(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    auto groups = SessionStore::getSessionsWithChatRecords();

    Json::Value result(Json::arrayValue);
    for (const auto &[sessionId, groupName, messageCount]: groups) {
        Json::Value group = sessionItemHeader(sessionId);
        group["groupName"] = groupName;
        group["messageCount"] = messageCount;
        result.append(group);
    }
    callback(HttpResponse::newHttpJsonResponse(result));
    co_return;
}

Task<> AdminController::getChatRecords(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, const std::string &sessionId) const {
    const uint64_t gid = std::stoull(sessionId);

    // 支持limit参数
    int limit = 50;
    if (const std::string limitParam = req->getParameter("limit"); !limitParam.empty()) {
        limit = std::stoi(limitParam);
    }

    // 返回带ID的记录，支持编辑
    const auto result = ChatRecordStore::getChatRecordsWithIds(gid, limit);

    // 反转顺序，最新的在底部
    Json::Value reversed(Json::arrayValue);
    for (Json::ArrayIndex i = result.size(); i > 0; --i) {
        reversed.append(result[i - 1]);
    }

    callback(HttpResponse::newHttpJsonResponse(reversed));
    co_return;
}

Task<> AdminController::updateChatRecord(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, const std::string &recordId) const {
    const auto json = req->getJsonObject();
    if (!json || !json->isMember("content")) {
        callback(HttpResponse::newHttpJsonResponse(AdminResponse::errorJson("缺少content字段")));
        co_return;
    }

    const int id = std::stoi(recordId);
    const std::string content = (*json)["content"].asString();
    ChatRecordStore::updateChatRecord(id, content);

    callback(HttpResponse::newHttpJsonResponse(AdminResponse::okJson("聊天记录已更新")));
    co_return;
}

Task<> AdminController::deleteChatRecord(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, const std::string &recordId) const {
    const int id = std::stoi(recordId);
    ChatRecordStore::deleteChatRecord(id);

    callback(HttpResponse::newHttpJsonResponse(AdminResponse::okJson("聊天记录已删除")));
    co_return;
}

Task<> AdminController::clearSessionChatRecords(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, const std::string &sessionId) const {
    const uint64_t gid = std::stoull(sessionId);
    ChatRecordStore::clearSessionChatRecords(gid);

    callback(HttpResponse::newHttpJsonResponse(AdminResponse::okJson("聊天记录已清空")));
    co_return;
}

// ==================== 长期记忆 ====================

Task<> AdminController::getLongTermMemories(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    uint64_t sessionId = 0;
    if (const std::string sessionParam = req->getParameter("sessionId"); !sessionParam.empty()) {
        sessionId = std::stoull(sessionParam);
    }

    int limit = 20;
    if (const std::string limitParam = req->getParameter("limit"); !limitParam.empty()) {
        limit = std::clamp(std::stoi(limitParam), 1, 100);
    }

    int offset = 0;
    if (const std::string offsetParam = req->getParameter("offset"); !offsetParam.empty()) {
        offset = std::max(0, std::stoi(offsetParam));
    }

    Json::Value items(Json::arrayValue);
    for (const auto &[id, groupId, content, createdAt]: LongTermMemoryStore::listMemories(sessionId, limit, offset)) {
        Json::Value item;
        item["id"] = id;
        // 会话 ID（私聊带标志位）可能超出 JS 安全整数范围，统一以字符串输出
        item["groupId"] = std::to_string(groupId);
        item["content"] = content;
        item["createdAt"] = createdAt;
        items.append(item);
    }

    Json::Value resp;
    resp["items"] = items;
    resp["total"] = static_cast<Json::Int64>(LongTermMemoryStore::countMemories(sessionId));
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::deleteLongTermMemory(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, const std::string &id) const {
    if (!LongTermMemoryStore::deleteMemory(std::stoll(id))) {
        callback(HttpResponse::newHttpJsonResponse(AdminResponse::errorJson("记忆不存在或已被删除")));
        co_return;
    }

    callback(HttpResponse::newHttpJsonResponse(AdminResponse::okJson("长期记忆已删除")));
    co_return;
}


// ==================== 群记忆 ====================

Task<> AdminController::getSessionMemory(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, const std::string &sessionId) const {
    const uint64_t gid = std::stoull(sessionId);
    const std::string memory = MemoryStore::getShortTermMemory(gid);

    Json::Value resp;
    resp["groupId"] = gid;
    resp["memory"] = memory;
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::updateSessionMemory(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, const std::string &sessionId) const {
    const auto json = req->getJsonObject();
    if (!json || !json->isMember("memory")) {
        callback(HttpResponse::newHttpJsonResponse(AdminResponse::errorJson("缺少memory字段")));
        co_return;
    }

    const uint64_t gid = std::stoull(sessionId);
    const std::string memory = (*json)["memory"].asString();
    MemoryStore::updateShortTermMemory(gid, memory);

    callback(HttpResponse::newHttpJsonResponse(AdminResponse::okJson("记忆已更新")));
    co_return;
}

// ==================== 好感度 ====================

Task<> AdminController::getSessionAffinity(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, const std::string &sessionId) const {
    const uint64_t gid = std::stoull(sessionId);
    auto affinityMap = AffinityStore::getAffinityMap(gid);

    std::vector<std::pair<uint64_t, int>> entries(affinityMap.begin(), affinityMap.end());
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

// ==================== 定时任务 ====================

Task<> AdminController::getScheduledTasks(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, const std::string &sessionId) const {
    const uint64_t sid = parseUInt64(sessionId);
    if (sid == 0) {
        callback(HttpResponse::newHttpJsonResponse(AdminResponse::errorJson("无效的会话 ID")));
        co_return;
    }

    const auto [sessionType, targetId] = QQMessage::parseSessionTarget(sid);
    Json::Value list(Json::arrayValue);
    for (const auto &task: TaskStore::getPendingScheduledTasksByTarget(sessionType, targetId)) {
        Json::Value item;
        item["id"] = task.id;
        item["remindTime"] = task.remindTime;
        item["content"] = task.content;
        item["daily"] = task.isDaily;
        list.append(item);
    }

    Json::Value resp;
    resp["tasks"] = list;
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::cancelScheduledTask(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, const std::string &id) const {
    const uint64_t taskId = parseUInt64(id);
    if (taskId == 0) {
        callback(HttpResponse::newHttpJsonResponse(AdminResponse::failJson("无效的任务 ID")));
        co_return;
    }

    if (TaskScheduler::instance().cancel(static_cast<int64_t>(taskId))) {
        Json::Value resp = AdminResponse::okJson(fmt::format("定时任务 #{} 已取消", taskId));
        spdlog::info("[Admin] 已取消定时任务 #{}", taskId);
        callback(HttpResponse::newHttpJsonResponse(resp));
    } else {
        callback(HttpResponse::newHttpJsonResponse(AdminResponse::failJson("任务不存在或已触发/已取消")));
    }
    co_return;
}

// ==================== 记忆配置 ====================

Task<> AdminController::getMemoryConfig(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    const auto config = ConfigStore::getMemoryConfig();
    callback(HttpResponse::newHttpJsonResponse(config));
    co_return;
}

Task<> AdminController::saveMemoryConfig(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    const auto json = req->getJsonObject();
    if (!json) {
        callback(HttpResponse::newHttpJsonResponse(AdminResponse::errorJson("缺少配置数据")));
        co_return;
    }

    // 窗口配置校验: 保留条数必须小于触发条数,否则窗口永远滑不动
    if (!json->isMember("windowTriggerCount") || (*json)["windowTriggerCount"].asInt() <= 0) {
        (*json)["windowTriggerCount"] = Config::instance().windowTriggerCount;
    }
    if (!json->isMember("windowKeepCount") || (*json)["windowKeepCount"].asInt() <= 0 ||
        (*json)["windowKeepCount"].asInt() >= (*json)["windowTriggerCount"].asInt()) {
        (*json)["windowKeepCount"] = (*json)["windowTriggerCount"].asInt() / 2;
    }
    if (!json->isMember("memoryExtractMaxTokens") || (*json)["memoryExtractMaxTokens"].asInt() <= 0) {
        (*json)["memoryExtractMaxTokens"] = Config::instance().memoryExtractMaxTokens;
    }
    // Router 子窗口校验: 保留条数必须小于触发条数
    if (!json->isMember("routerWindowTriggerCount") || (*json)["routerWindowTriggerCount"].asInt() <= 0) {
        (*json)["routerWindowTriggerCount"] = Config::instance().routerWindowTriggerCount;
    }
    if (!json->isMember("routerWindowKeepCount") || (*json)["routerWindowKeepCount"].asInt() <= 0 ||
        (*json)["routerWindowKeepCount"].asInt() >= (*json)["routerWindowTriggerCount"].asInt()) {
        (*json)["routerWindowKeepCount"] = (*json)["routerWindowTriggerCount"].asInt() / 2;
    }
    // 召回阈值: 必须在 (0,1) 开区间内
    if (!json->isMember("longTermRecallThreshold") || (*json)["longTermRecallThreshold"].asDouble() <= 0.0 ||
        (*json)["longTermRecallThreshold"].asDouble() >= 1.0) {
        (*json)["longTermRecallThreshold"] = Config::instance().longTermRecallThreshold;
    }
    // 注入阈值: 必须在 (0,1) 开区间内
    if (!json->isMember("longTermInjectThreshold") || (*json)["longTermInjectThreshold"].asDouble() <= 0.0 ||
        (*json)["longTermInjectThreshold"].asDouble() >= 1.0) {
        (*json)["longTermInjectThreshold"] = Config::instance().longTermInjectThreshold;
    }

    ConfigStore::saveMemoryConfig(*json);

    // 更新内存中的配置
    auto &config = Config::instance();
    config.windowTriggerCount = (*json)["windowTriggerCount"].asInt();
    config.windowKeepCount = (*json)["windowKeepCount"].asInt();
    config.memoryExtractMaxTokens = (*json)["memoryExtractMaxTokens"].asInt();
    config.routerWindowTriggerCount = (*json)["routerWindowTriggerCount"].asInt();
    config.routerWindowKeepCount = (*json)["routerWindowKeepCount"].asInt();
    config.shortTermMemoryMax = (*json)["shortTermMemoryMax"].asInt();
    config.longTermRecallThreshold = (*json)["longTermRecallThreshold"].asDouble();
    config.longTermInjectThreshold = (*json)["longTermInjectThreshold"].asDouble();

    callback(HttpResponse::newHttpJsonResponse(AdminResponse::okJson("记忆配置已保存")));
    co_return;
}

// ==================== QQ Bot 配置 ====================

Task<> AdminController::getQQConfig(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    const auto config = ConfigStore::getQQConfig();
    callback(HttpResponse::newHttpJsonResponse(config));
    co_return;
}

Task<> AdminController::saveQQConfig(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    const auto json = req->getJsonObject();
    if (!json) {
        callback(HttpResponse::newHttpJsonResponse(AdminResponse::errorJson("缺少配置数据")));
        co_return;
    }

    ConfigStore::saveQQConfig(*json);

    // 更新内存中的配置
    auto &config = Config::instance();
    config.accessToken = json->get("accessToken", "").asString();
    config.selfQQNumber = json->get("selfQQNumber", 0).asInt64();
    config.qqHttpHost = json->get("qqHttpHost", "").asString();
    config.botName = json->get("botName", "小喵").asString();

    // 更新 QQMessage 的自定义名称
    QQMessage::setCustomQQName(config.selfQQNumber, config.botName + "(我)");

    callback(HttpResponse::newHttpJsonResponse(AdminResponse::okJson("QQ Bot 配置已保存")));
    co_return;
}
