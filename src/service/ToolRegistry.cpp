/// @file ToolRegistry.cpp
/// @brief 工具注册中心 - 实现
/// @author donghao
/// @date 2026-03-28

#include <ranges>
#include <service/ToolRegistry.hpp>
#include <spdlog/spdlog.h>
#include <storage/SessionStore.hpp>

namespace {
    /// @brief 构建单个工具的 OpenAI function calling 定义（缺省字段补齐为合法 schema）
    Json::Value buildToolDef(const insoulforge::Tool &tool) {
        Json::Value toolDef;
        toolDef["type"] = "function";
        toolDef["function"]["name"] = tool.name;
        toolDef["function"]["description"] = tool.description;
        Json::Value params = tool.parameters.isNull() ? Json::Value(Json::objectValue) : tool.parameters;
        params["type"] = "object";
        if (!params.isMember("properties")) {
            params["properties"] = Json::Value(Json::objectValue);
        }
        if (!params.isMember("required")) {
            params["required"] = Json::Value(Json::arrayValue);
        }
        toolDef["function"]["parameters"] = params;
        return toolDef;
    }
} // namespace

namespace insoulforge {
    ToolContext &currentToolContext() {
        thread_local ToolContext ctx;
        return ctx;
    }

    ToolRegistry &ToolRegistry::instance() {
        static ToolRegistry registry;
        return registry;
    }

    void ToolRegistry::registerTool(const Tool &tool, ToolCategory category) {
        switch (category) {
            case ToolCategory::TERMINAL:
                m_terminalTools[tool.name] = tool;
                break;
            case ToolCategory::INFORMATION:
                m_infoTools[tool.name] = tool;
                break;
            case ToolCategory::ACTION:
                m_actionTools[tool.name] = tool;
                break;
        }
        spdlog::info("工具注册成功: {} (分类: {})", tool.name, categoryToString(category));
    }

    Json::Value ToolRegistry::getAllTools() const {
        Json::Value tools;

        // 分类顺序固定（终端 → 信息 → 动作），类内按名称有序，保证 tools 数组跨重启稳定
        for (const auto *categoryTools: {&m_terminalTools, &m_infoTools, &m_actionTools}) {
            for (const auto &tool: *categoryTools | std::views::values) {
                tools.append(buildToolDef(tool));
            }
        }

        return tools;
    }

    drogon::Task<std::string> ToolRegistry::executeTool(
      const std::string &name, const Json::Value &args, uint64_t sessionId) const {
        // 设置上下文
        auto &ctx = currentToolContext();
        ctx.sessionId = sessionId;
        if (sessionId != 0) {
            ctx.groupName = SessionStore::getSessionName(sessionId);
        }

        for (const auto *categoryTools: {&m_terminalTools, &m_infoTools, &m_actionTools}) {
            if (const auto it = categoryTools->find(name); it != categoryTools->end()) {
                co_return co_await it->second.handler(args);
            }
        }
        co_return "工具未找到: " + name;
    }

    bool ToolRegistry::hasTool(const std::string &name) const {
        return m_terminalTools.contains(name) || m_infoTools.contains(name) || m_actionTools.contains(name);
    }

    void ToolRegistry::unregisterTool(const std::string &name) {
        m_terminalTools.erase(name);
        m_infoTools.erase(name);
        m_actionTools.erase(name);
        spdlog::info("工具已注销: {}", name);
    }

    void ToolRegistry::clearAllCustomTools() {
        for (const auto &name: m_customToolNames) {
            unregisterTool(name);
        }
        m_customToolNames.clear();
    }

    void ToolRegistry::recordCustomTool(const std::string &name) {
        if (!std::ranges::contains(m_customToolNames, name)) {
            m_customToolNames.push_back(name);
        }
    }

    std::string ToolRegistry::categoryToString(ToolCategory category) {
        switch (category) {
            case ToolCategory::TERMINAL:
                return "TERMINAL";
            case ToolCategory::INFORMATION:
                return "INFORMATION";
            case ToolCategory::ACTION:
                return "ACTION";
            default:
                return "UNKNOWN";
        }
    }
} // namespace insoulforge
