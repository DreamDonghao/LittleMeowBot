/// @file ToolRegistry.hpp
/// @brief 工具注册中心 - Agent 工具的分类管理与执行
/// @author donghao
/// @date 2026-03-28
/// @details 提供工具的注册、管理和执行功能：
///          - 分类管理：TERMINAL（终端工具）、INFORMATION（信息工具）、ACTION（动作工具）
///          - 工具执行：支持异步执行和上下文传递
///          - 工具定义生成：生成 OpenAI 兼容的工具定义 JSON

#pragma once

#include <json/value.h>
#include <drogon/utils/coroutine.h>
#include <unordered_map>
#include <string>
#include <functional>
#include <vector>

namespace LittleMeowBot {
    /// @brief 工具执行上下文（线程局部存储）
    struct ToolContext{
        uint64_t groupId = 0;
        std::string groupName;
    };

    /// @brief 获取当前工具执行上下文
    ToolContext& currentToolContext();

    /// @brief 异步工具处理器
    using ToolHandler = std::function<drogon::Task<std::string>(const Json::Value & args)>;

    struct Tool{
        std::string name;
        std::string description;
        Json::Value parameters; // JSON Schema
        ToolHandler handler;
    };

    /// @brief 工具分类
    enum class ToolCategory{
        TERMINAL, // 终端工具：reply, no_reply（结束处理）
        INFORMATION, // 信息工具：get_weather, search_web（获取数据）
        ACTION // 动作工具：random（执行动作）
    };

    /// @brief 工具注册中心，分类管理工具
    class ToolRegistry{
    public:
        static ToolRegistry& instance();

        /// @brief 注册工具到指定分类
        void registerTool(const Tool& tool, ToolCategory category);

        /// @brief 获取指定分类的工具定义（用于 API 请求）
        Json::Value getToolsByCategory(ToolCategory category) const;

        /// @brief 获取所有工具定义
        Json::Value getAllTools() const;

        /// @brief 获取 Executor 使用的工具（终端 + 信息 + 动作）
        Json::Value getExecutorTools() const;

        /// @brief 获取 Planner 可用的工具（仅信息工具，用于规划）
        Json::Value getPlannerInfoTools() const;

        /// @brief 执行工具（异步）
        drogon::Task<std::string> executeTool(const std::string& name, const Json::Value& args,
                                              uint64_t groupId = 0) const;

        /// @brief 检查工具是否存在
        bool hasTool(const std::string& name) const;

        /// @brief 检查是否是终端工具
        bool isTerminalTool(const std::string& name) const;

        /// @brief 检查是否是信息工具
        bool isInfoTool(const std::string& name) const;

        /// @brief 注销工具
        void unregisterTool(const std::string& name);

        /// @brief 清除所有已注册的自定义工具
        void clearAllCustomTools();

        /// @brief 记录自定义工具名称（注册时调用）
        void recordCustomTool(const std::string& name);

        /// @brief 获取工具分类
        ToolCategory getCategory(const std::string& name) const;

        /// @brief 获取所有工具名称列表
        std::vector<std::string> getToolNames() const;

        /// @brief 生成工具说明文本（用于 Executor Prompt）
        std::string getToolsDescription() const;

    private:
        ToolRegistry() = default;

        std::unordered_map<std::string, Tool> m_terminalTools;
        std::unordered_map<std::string, Tool> m_infoTools;
        std::unordered_map<std::string, Tool> m_actionTools;
        std::vector<std::string> m_customToolNames; // 记录已注册的自定义工具名称

        const std::unordered_map<std::string, Tool>& getToolMap(ToolCategory category) const;
        static std::string categoryToString(ToolCategory category);
    };
} // namespace LittleMeowBot
