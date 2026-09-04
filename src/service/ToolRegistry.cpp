/// @file ToolRegistry.cpp
/// @brief 工具注册中心 - 实现
/// @author donghao
/// @date 2026-03-28

#include <ranges>
#include <service/ToolRegistry.hpp>
#include <spdlog/spdlog.h>

using insoulforge::json;

namespace {
    /// @brief 构建单个工具的 OpenAI function calling 定义（缺省字段补齐为合法 schema）
    json buildToolDef(const insoulforge::Tool &tool) {
        json toolDef;
        toolDef["type"] = "function";
        toolDef["function"]["name"] = tool.name;
        toolDef["function"]["description"] = tool.description;
        json params = tool.parameters.is_null() ? json::object() : tool.parameters;
        params["type"] = "object";
        if (!params.contains("properties")) {
            params["properties"] = json::object();
        }
        if (!params.contains("required")) {
            params["required"] = json::array();
        }
        toolDef["function"]["parameters"] = params;
        return toolDef;
    }
} // namespace

namespace insoulforge {
    ToolRegistry &ToolRegistry::instance() {
        static ToolRegistry registry;
        return registry;
    }

    void ToolRegistry::registerTool(const Tool &tool, const ToolCategory category) {
        switch (category) {
            case ToolCategory::REPLY:
                m_replyTools[tool.name] = tool;
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

    json ToolRegistry::getAllTools() const {
        json tools = json::array();

        // 分类顺序固定（回复 → 信息 → 动作），类内按名称有序，保证 tools 数组跨重启稳定
        for (const auto *categoryTools: {&m_replyTools, &m_infoTools, &m_actionTools}) {
            for (const auto &tool: *categoryTools | std::views::values) {
                tools.push_back(buildToolDef(tool));
            }
        }

        return tools;
    }

    drogon::Task<std::string> ToolRegistry::executeTool(const std::string name, json args, ToolCallContext ctx) const {
        for (const auto *categoryTools: {&m_replyTools, &m_infoTools, &m_actionTools}) {
            if (const auto it = categoryTools->find(name); it != categoryTools->end()) {
                co_return co_await it->second.handler(std::move(args), std::move(ctx));
            }
        }
        co_return "工具未找到: " + name;
    }

    bool ToolRegistry::hasTool(const std::string &name) const {
        return m_replyTools.contains(name) || m_infoTools.contains(name) || m_actionTools.contains(name);
    }

    void ToolRegistry::unregisterTool(const std::string &name) {
        m_replyTools.erase(name);
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
            case ToolCategory::REPLY:
                return "REPLY";
            case ToolCategory::INFORMATION:
                return "INFORMATION";
            case ToolCategory::ACTION:
                return "ACTION";
            default:
                return "UNKNOWN";
        }
    }
} // namespace insoulforge
