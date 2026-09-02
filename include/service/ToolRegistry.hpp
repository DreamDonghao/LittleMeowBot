/// @file ToolRegistry.hpp
/// @brief 工具注册中心 - Agent 工具的分类管理与执行
/// @author donghao
/// @date 2026-03-28
/// @details 提供工具的注册、管理和执行功能：
///          - 分类管理：TERMINAL（终端工具）、INFORMATION（信息工具）、ACTION（动作工具）
///          - 工具执行：支持异步执行和上下文传递
///          - 工具定义生成：生成 OpenAI 兼容的工具定义 JSON

#pragma once

#include <drogon/utils/coroutine.h>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <util/JsonUtil.hpp>

namespace insoulforge {
    /// @brief 工具执行上下文（线程局部存储）
    struct ToolContext {
        uint64_t sessionId = 0;
        std::string groupName;
    };

    /// @brief 获取当前工具执行上下文
    ToolContext &currentToolContext();

    /// @brief 异步工具处理器
    // 注意：args 必须以指针传递。drogon Task 协程的 json 引用参数会被 clang 用来
    // 初始化 promise 的 optional<string>（json 隐式转 string），对象参数会在协程
    // 启动时抛 type_error 导致 unwind 崩溃
    using ToolHandler = std::function<drogon::Task<std::string>(const json *args)>;

    struct Tool {
        std::string name;
        std::string description;
        json parameters; // JSON Schema
        ToolHandler handler;
    };

    /// @brief 工具分类
    enum class ToolCategory {
        TERMINAL, // 终端工具：reply, no_reply, reply_with_quote（结束处理）
        INFORMATION, // 信息工具：recall_memory, get_group_name, list_stickers（获取数据）
        ACTION // 动作工具：send_face, send_sticker, ban_user, send_poke 等（执行操作）
    };

    /// @brief 工具注册中心，分类管理工具
    class ToolRegistry {
    public:
        static ToolRegistry &instance();

        /// @brief 注册工具到指定分类
        void registerTool(const Tool &tool, ToolCategory category);

        /// @brief 获取所有工具定义
        [[nodiscard]] json getAllTools() const;

        /// @brief 执行工具（异步）
        [[nodiscard]] drogon::Task<std::string> executeTool(
          const std::string &name, const json &args, uint64_t sessionId = 0) const;

        /// @brief 检查工具是否存在
        [[nodiscard]] bool hasTool(const std::string &name) const;

        /// @brief 注销工具
        void unregisterTool(const std::string &name);

        /// @brief 清除所有已注册的自定义工具
        void clearAllCustomTools();

        /// @brief 记录自定义工具名称（注册时调用）
        void recordCustomTool(const std::string &name);

    private:
        ToolRegistry() = default;

        // 有序容器保证 tools 数组顺序跨重启稳定，避免破坏 provider 的 prompt cache
        std::map<std::string, Tool> m_terminalTools;
        std::map<std::string, Tool> m_infoTools;
        std::map<std::string, Tool> m_actionTools;
        std::vector<std::string> m_customToolNames; // 记录已注册的自定义工具名称

        static std::string categoryToString(ToolCategory category);
    };
} // namespace insoulforge
