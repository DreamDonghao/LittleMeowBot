/// @file AdminControllerCustomTools.cpp
/// @brief 管理后台 REST API 控制器 - LLM 配置 / 提示词 / 自定义工具部分的实现
/// @author donghao
/// @date 2026-09-01

#include <agent/AgentToolManager.hpp>
#include <algorithm>
#include <array>
#include <config/Config.hpp>
#include <controllers/AdminController.hpp>
#include <controllers/AdminResponse.hpp>
#include <ranges>
#include <service/ToolRegistry.hpp>
#include <spdlog/spdlog.h>
#include <storage/ConfigStore.hpp>
#include <storage/PromptStore.hpp>
#include <storage/ToolStore.hpp>
#include <util/CommonUtil.hpp>

using namespace insoulforge;
using namespace drogon;

// ==================== LLM配置 ====================

Task<> AdminController::getLLMConfigs(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    const auto configs = ConfigStore::getAllLLMConfigs();
    callback(HttpResponse::newHttpJsonResponse(configs));
    co_return;
}

Task<> AdminController::saveLLMConfig(
  const HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    const auto json = req->getJsonObject();
    if (!json || !json->isMember("name")) {
        callback(HttpResponse::newHttpJsonResponse(AdminResponse::errorJson("缺少name字段")));
        co_return;
    }

    const std::string name = (*json)["name"].asString();
    ConfigStore::saveLLMConfig(name, *json);

    // 更新内存中的配置
    struct ConfigTarget {
        std::string_view name;
        LLMApiConfig *api;
        LLMModelParams *params; // nullptr 表示该配置没有模型参数
        int defaultMaxTokens;
    };
    auto &config = Config::instance();
    const std::array targets{
      ConfigTarget{.name = "router", .api = &config.router, .params = &config.routerParams, .defaultMaxTokens = 100},
      ConfigTarget{
        .name = "executor", .api = &config.executor, .params = &config.executorParams, .defaultMaxTokens = 100},
      ConfigTarget{.name = "executorThinking",
        .api = &config.executorThinking,
        .params = &config.executorThinkingParams,
        .defaultMaxTokens = 512},
      ConfigTarget{.name = "image", .api = &config.image, .params = nullptr, .defaultMaxTokens = 0},
    };

    if (const auto it = std::ranges::find(targets, name, &ConfigTarget::name); it != targets.end()) {
        auto &api = *it->api;
        api.apiKey = json->get("apiKey", "").asString();
        api.baseUrl = json->get("baseUrl", "").asString();
        api.path = json->get("path", "").asString();
        api.model = json->get("model", "").asString();
        api.reasoningEffort = json->get("reasoningEffort", "").asString();
        if (it->params) {
            it->params->maxTokens = json->get("maxTokens", it->defaultMaxTokens).asInt();
            it->params->temperature = json->get("temperature", 0.7f).asFloat();
            it->params->topP = json->get("topP", 0.9f).asFloat();
        }
    }

    callback(HttpResponse::newHttpJsonResponse(AdminResponse::okJson("LLM配置已保存")));
    co_return;
}

// ==================== 提示词 ====================

Task<> AdminController::getPrompts(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    const auto prompts = PromptStore::getAllPrompts();

    Json::Value result;
    for (const auto &[key, content]: prompts) {
        result[key] = content;
    }
    callback(HttpResponse::newHttpJsonResponse(result));
    co_return;
}

Task<> AdminController::savePrompt(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    auto json = req->getJsonObject();
    if (!json || !json->isMember("key") || !json->isMember("content")) {
        callback(HttpResponse::newHttpJsonResponse(AdminResponse::errorJson("缺少key或content字段")));
        co_return;
    }

    std::string key = (*json)["key"].asString();
    std::string content = (*json)["content"].asString();
    std::string description = json->isMember("description") ? (*json)["description"].asString() : "";

    // 防护: router 提示词的 JSON 格式示例若含双花括号(fmt 转义残留/旧页面缓存内容),模型会照抄导致解析失败
    if ((key == "router_system" || key == "router_private_system") &&
        (content.find("{{") != std::string::npos || content.find("}}") != std::string::npos)) {
        callback(HttpResponse::newHttpJsonResponse(
          AdminResponse::failJson("提示词包含双花括号{{ }}，JSON 格式示例应为单花括号，请刷新页面后重试")));
        co_return;
    }

    PromptStore::setPrompt(key, content, description);
    spdlog::warn("管理后台更新提示词: key={}, 长度={}", key, content.size());

    callback(HttpResponse::newHttpJsonResponse(AdminResponse::okJson("提示词已保存")));
    co_return;
}

// ==================== 自定义工具 ====================

Task<> AdminController::getCustomTools(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    const auto tools = ToolStore::getCustomTools();

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

Task<> AdminController::addCustomTool(HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    const auto json = req->getJsonObject();
    if (!json || !json->isMember("name") || !json->isMember("executorType") || !json->isMember("executorConfig")) {
        callback(HttpResponse::newHttpJsonResponse(
          AdminResponse::errorJson("缺少必要字段 (name, executorType, executorConfig)")));
        co_return;
    }

    const std::string name = (*json)["name"].asString();

    // 检查是否与内置工具名冲突
    const auto &registry = ToolRegistry::instance();
    if (registry.hasTool(name)) {
        callback(HttpResponse::newHttpJsonResponse(
          AdminResponse::errorJson("工具名 '" + name + "' 已存在（内置工具或自定义工具）")));
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

    const int id = ToolStore::addCustomTool(tool);

    // 立即注册到 ToolRegistry
    AgentToolManager::registerCustomTools();

    Json::Value resp = AdminResponse::okJson("自定义工具已添加");
    resp["id"] = id;
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::updateCustomTool(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, const std::string &id) const {
    const auto json = req->getJsonObject();
    if (!json || !json->isMember("name") || !json->isMember("executorType") || !json->isMember("executorConfig")) {
        callback(HttpResponse::newHttpJsonResponse(
          AdminResponse::errorJson("缺少必要字段 (name, executorType, executorConfig)")));
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

    ToolStore::updateCustomTool(tool);

    // 重新注册工具
    AgentToolManager::registerCustomTools();

    callback(HttpResponse::newHttpJsonResponse(AdminResponse::okJson("自定义工具已更新")));
    co_return;
}

Task<> AdminController::deleteCustomTool(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, const std::string &id) const {
    const int toolId = std::stoi(id);
    ToolStore::deleteCustomTool(toolId);

    // 重新注册工具（移除已删除的）
    AgentToolManager::registerCustomTools();

    callback(HttpResponse::newHttpJsonResponse(AdminResponse::okJson("自定义工具已删除")));
    co_return;
}

Task<> AdminController::toggleCustomTool(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, const std::string &id) const {
    const int toolId = std::stoi(id);
    ToolStore::toggleCustomTool(toolId);

    // 重新注册工具
    AgentToolManager::registerCustomTools();

    callback(HttpResponse::newHttpJsonResponse(AdminResponse::okJson("工具状态已切换")));
    co_return;
}

Task<> AdminController::reloadCustomTools(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    AgentToolManager::registerCustomTools();

    callback(HttpResponse::newHttpJsonResponse(AdminResponse::okJson("自定义工具已重新加载")));
    co_return;
}

Task<> AdminController::testCustomTool(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    auto json = req->getJsonObject();
    if (!json) {
        callback(HttpResponse::newHttpJsonResponse(AdminResponse::failJson("缺少请求数据")));
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
        auto tools = ToolStore::getCustomTools();
        auto it = std::ranges::find_if(tools, [toolId](const auto &t) { return t.id == toolId; });
        if (it == tools.end()) {
            callback(HttpResponse::newHttpJsonResponse(AdminResponse::failJson("工具不存在")));
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

    Json::Value resp = AdminResponse::okJson();
    resp["result"] = result;
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

// ==================== 自定义工具配置 ====================

Task<> AdminController::getCustomToolConfig(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    Json::Value resp;
    resp["pythonPath"] = ToolStore::getCustomToolPython();
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}

Task<> AdminController::saveCustomToolConfig(
  const HttpRequestPtr req, const std::function<void(const HttpResponsePtr &)> callback) const {
    const auto json = req->getJsonObject();
    if (!json || !json->isMember("pythonPath")) {
        callback(HttpResponse::newHttpJsonResponse(AdminResponse::failJson("缺少 pythonPath 字段")));
        co_return;
    }

    const std::string pythonPath = (*json)["pythonPath"].asString();
    ToolStore::setCustomToolPython(pythonPath);

    callback(HttpResponse::newHttpJsonResponse(AdminResponse::okJson("Python解释器路径已保存")));
    co_return;
}

// ============== 自定义工具导入导出 ==============

Task<> AdminController::exportCustomTool(
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback, const std::string &id) const {
    int toolId = std::stoi(id);
    auto tools = ToolStore::getCustomTools();

    auto it = std::ranges::find_if(tools, [toolId](const ToolStore::CustomTool &t) { return t.id == toolId; });

    if (it == tools.end()) {
        callback(HttpResponse::newHttpJsonResponse(AdminResponse::failJson("工具不存在")));
        co_return;
    }

    const auto &tool = *it;

    // 只支持导出 Python 工具
    if (tool.executorType != "python") {
        callback(HttpResponse::newHttpJsonResponse(AdminResponse::failJson("仅支持导出 Python 类型工具")));
        co_return;
    }

    // 构建导出 JSON（简化格式，不含 executorType）
    Json::Value exportJson;
    exportJson["name"] = tool.name;
    exportJson["description"] = tool.description;

    // 解析参数 JSON
    Json::Value params;
    std::ignore = tryParseJson(tool.parameters, params);
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
  HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) const {
    auto json = req->getJsonObject();
    if (!json) {
        callback(HttpResponse::newHttpJsonResponse(AdminResponse::failJson("无效的 JSON 数据")));
        co_return;
    }

    // 检查必要字段
    if (!json->isMember("name") || !json->isMember("description") || !json->isMember("scriptContent")) {
        callback(
          HttpResponse::newHttpJsonResponse(AdminResponse::failJson("缺少必要字段：name, description, scriptContent")));
        co_return;
    }

    std::string name = (*json)["name"].asString();

    // 检查是否已存在同名工具
    if (ToolStore::hasCustomTool(name)) {
        callback(HttpResponse::newHttpJsonResponse(AdminResponse::failJson("工具名已存在：" + name)));
        co_return;
    }

    // 构建工具对象（强制使用 Python 类型）
    ToolStore::CustomTool tool;
    tool.name = name;
    tool.description = (*json)["description"].asString();
    tool.executorType = "python";

    // 参数处理
    if (json->isMember("parameters")) {
        tool.parameters = dumpJson((*json)["parameters"], false);
    } else {
        tool.parameters = R"({"type":"object","properties":{},"required":[]})";
    }

    tool.scriptContent = (*json)["scriptContent"].asString();
    tool.readme = json->get("readme", "").asString();
    tool.enabled = true;

    // 添加到数据库
    int newId = ToolStore::addCustomTool(tool);
    AgentToolManager::registerCustomTools();

    spdlog::info("导入自定义工具: {} (ID: {})", tool.name, newId);

    Json::Value resp = AdminResponse::okJson("工具已导入");
    resp["id"] = newId;
    callback(HttpResponse::newHttpJsonResponse(resp));
    co_return;
}
