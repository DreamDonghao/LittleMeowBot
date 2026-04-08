/// @file ToolRegistry.cpp
/// @brief 工具注册中心 - 实现
/// @author donghao
/// @date 2026-03-28

#include <service/ToolRegistry.hpp>
#include <spdlog/spdlog.h>
#include <fmt/core.h>
#include <ranges>
#include <storage/Database.hpp>

namespace LittleMeowBot {
    ToolContext& currentToolContext(){
        thread_local ToolContext ctx;
        return ctx;
    }

    ToolRegistry& ToolRegistry::instance(){
        static ToolRegistry registry;
        return registry;
    }

    void ToolRegistry::registerTool(const Tool& tool, ToolCategory category){
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
        spdlog::info("工具注册成功: {} (分类: {})",
                     tool.name, categoryToString(category));
    }

    Json::Value ToolRegistry::getToolsByCategory(ToolCategory category) const{
        Json::Value tools;

        for (const auto& toolMap = getToolMap(category);
             const auto& tool : toolMap | std::views::values) {
            Json::Value toolDef;
            toolDef["type"] = "function";
            toolDef["function"]["name"] = tool.name;
            toolDef["function"]["description"] = tool.description;
            toolDef["function"]["parameters"] = tool.parameters;
            tools.append(toolDef);
        }
        return tools;
    }

    Json::Value ToolRegistry::getAllTools() const{
        Json::Value tools;

        // 终端工具
        for (const auto& tool : m_terminalTools | std::views::values) {
            Json::Value toolDef;
            toolDef["type"] = "function";
            toolDef["function"]["name"] = tool.name;
            toolDef["function"]["description"] = tool.description;
            toolDef["function"]["parameters"] = tool.parameters;
            tools.append(toolDef);
        }

        // 信息工具
        for (const auto& tool : m_infoTools | std::views::values) {
            Json::Value toolDef;
            toolDef["type"] = "function";
            toolDef["function"]["name"] = tool.name;
            toolDef["function"]["description"] = tool.description;
            toolDef["function"]["parameters"] = tool.parameters;
            tools.append(toolDef);
        }

        // 动作工具
        for (const auto& tool : m_actionTools | std::views::values) {
            Json::Value toolDef;
            toolDef["type"] = "function";
            toolDef["function"]["name"] = tool.name;
            toolDef["function"]["description"] = tool.description;
            toolDef["function"]["parameters"] = tool.parameters;
            tools.append(toolDef);
        }

        return tools;
    }

    Json::Value ToolRegistry::getExecutorTools() const{
        return getAllTools();
    }

    Json::Value ToolRegistry::getPlannerInfoTools() const{
        Json::Value tools;
        for (const auto& tool : m_infoTools | std::views::values) {
            Json::Value toolDef;
            toolDef["type"] = "function";
            toolDef["function"]["name"] = tool.name;
            toolDef["function"]["description"] = tool.description;
            toolDef["function"]["parameters"] = tool.parameters;
            tools.append(toolDef);
        }
        return tools;
    }

    drogon::Task<std::string> ToolRegistry::executeTool(const std::string& name, const Json::Value& args,
                                                        uint64_t groupId) const{
        // 设置上下文
        auto& ctx = currentToolContext();
        ctx.groupId = groupId;
        if (groupId != 0) {
            ctx.groupName = Database::instance().getGroupName(groupId);
        }

        // 查找所有分类
        if (m_terminalTools.contains(name)) {
            co_return co_await m_terminalTools.at(name).handler(args);
        }
        if (m_infoTools.contains(name)) {
            co_return co_await m_infoTools.at(name).handler(args);
        }
        if (m_actionTools.contains(name)) {
            co_return co_await m_actionTools.at(name).handler(args);
        }
        co_return "工具未找到: " + name;
    }

    bool ToolRegistry::hasTool(const std::string& name) const{
        return m_terminalTools.contains(name) ||
            m_infoTools.contains(name) ||
            m_actionTools.contains(name);
    }

    bool ToolRegistry::isTerminalTool(const std::string& name) const{
        return m_terminalTools.contains(name);
    }

    bool ToolRegistry::isInfoTool(const std::string& name) const{
        return m_infoTools.contains(name);
    }

    void ToolRegistry::unregisterTool(const std::string& name){
        m_terminalTools.erase(name);
        m_infoTools.erase(name);
        m_actionTools.erase(name);
        spdlog::info("工具已注销: {}", name);
    }

    void ToolRegistry::clearAllCustomTools(){
        for (const auto& name : m_customToolNames) {
            unregisterTool(name);
        }
        m_customToolNames.clear();
    }

    void ToolRegistry::recordCustomTool(const std::string& name){
        if (!std::ranges::contains(m_customToolNames, name)) {
            m_customToolNames.push_back(name);
        }
    }

    ToolCategory ToolRegistry::getCategory(const std::string& name) const{
        if (m_terminalTools.contains(name)) return ToolCategory::TERMINAL;
        if (m_infoTools.contains(name)) return ToolCategory::INFORMATION;
        if (m_actionTools.contains(name)) return ToolCategory::ACTION;
        return ToolCategory::ACTION; // 默认
    }

    std::vector<std::string> ToolRegistry::getToolNames() const{
        std::vector<std::string> names;

        for (const auto& name : m_terminalTools | std::views::keys) names.push_back(name);
        for (const auto& name : m_infoTools | std::views::keys) names.push_back(name);
        for (const auto& name : m_actionTools | std::views::keys) names.push_back(name);

        return names;
    }

    std::string ToolRegistry::getToolsDescription() const{
        std::string desc = "可用工具：\n";

        // 终端工具
        desc += "【终端工具】\n";
        for (const auto& [name, tool] : m_terminalTools) {
            desc += fmt::format("- {}: {}\n", name, tool.description);
        }

        // 信息工具
        desc += "【信息工具】\n";
        for (const auto& [name, tool] : m_infoTools) {
            desc += fmt::format("- {}: {}\n", name, tool.description);
        }

        // 动作工具
        desc += "【动作工具】\n";
        for (const auto& [name, tool] : m_actionTools) {
            desc += fmt::format("- {}: {}\n", name, tool.description);
        }

        return desc;
    }

    const std::unordered_map<std::string, Tool>& ToolRegistry::getToolMap(ToolCategory category) const{
        switch (category) {
        case ToolCategory::TERMINAL: return m_terminalTools;
        case ToolCategory::INFORMATION: return m_infoTools;
        case ToolCategory::ACTION: return m_actionTools;
        default: return m_actionTools;
        }
    }

    std::string ToolRegistry::categoryToString(ToolCategory category){
        switch (category) {
        case ToolCategory::TERMINAL: return "TERMINAL";
        case ToolCategory::INFORMATION: return "INFORMATION";
        case ToolCategory::ACTION: return "ACTION";
        default: return "UNKNOWN";
        }
    }
} // namespace LittleMeowBot