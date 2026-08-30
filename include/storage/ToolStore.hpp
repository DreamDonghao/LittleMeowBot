/// @file ToolStore.hpp
/// @brief 自定义工具存储
/// @author donghao
/// @date 2026-08-30
/// @details 表：custom_tools（工具定义与脚本）、settings（Python 解释器路径）

#pragma once
#include <string>
#include <vector>

namespace insoulforge {
    /// @brief 自定义工具存储
    class ToolStore {
    public:
        /// @brief 自定义工具结构
        struct CustomTool {
            int id = 0;
            std::string name; // 工具名，如 "search_web"
            std::string description; // 给LLM看的描述
            std::string parameters; // JSON Schema (字符串形式)
            std::string executorType; // "python" | "http"
            std::string executorConfig; // JSON 配置 (http用)
            std::string scriptContent; // Python脚本内容 (python用)
            std::string readme; // Markdown 说明文档（作者、用法、联系方式等）
            bool enabled = true;
        };

        static ToolStore &instance();

        /// @brief 获取所有自定义工具
        std::vector<CustomTool> getCustomTools() const;

        /// @brief 获取启用的自定义工具（供 AgentToolManager 使用）
        std::vector<CustomTool> getEnabledCustomTools() const;

        /// @brief 添加自定义工具
        int addCustomTool(const CustomTool &tool) const;

        /// @brief 更新自定义工具
        void updateCustomTool(const CustomTool &tool) const;

        /// @brief 删除自定义工具
        void deleteCustomTool(int id) const;

        /// @brief 切换自定义工具启用状态
        void toggleCustomTool(int id) const;

        /// @brief 检查工具名是否已存在
        bool hasCustomTool(const std::string &name) const;

        /// @brief 获取自定义工具Python解释器路径
        std::string getCustomToolPython() const;

        /// @brief 设置自定义工具Python解释器路径
        void setCustomToolPython(const std::string &pythonPath) const;

    private:
        ToolStore() = default;

        /// @brief 从数据库加载自定义工具（onlyEnabled 时仅加载启用的）
        std::vector<CustomTool> loadCustomTools(bool onlyEnabled) const;
    };
} // namespace insoulforge