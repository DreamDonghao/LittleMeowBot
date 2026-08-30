/// @file ToolStore.cpp
/// @brief 自定义工具存储 - 实现
/// @author donghao
/// @date 2026-08-30

#include <storage/ToolStore.hpp>
#include <storage/Database.hpp>
#include <storage/Statement.hpp>
#include <spdlog/spdlog.h>
#include <fmt/core.h>

namespace insoulforge {
    ToolStore &ToolStore::instance() {
        static ToolStore store;
        return store;
    }

    std::vector<ToolStore::CustomTool> ToolStore::loadCustomTools(const bool onlyEnabled) const {
        auto &db = Database::instance();
        std::shared_lock lock(db.mutex());
        std::vector<CustomTool> tools;
        Statement stmt(db.handle(),
                       fmt::format(
                           "SELECT id, name, description, parameters, executor_type, executor_config, script_content, readme, enabled "
                           "FROM custom_tools{} ORDER BY id", onlyEnabled ? " WHERE enabled = 1" : ""));
        while (stmt.step()) {
            CustomTool tool;
            tool.id = stmt.getInt(0);
            tool.name = stmt.getText(1);
            tool.description = stmt.getText(2);
            tool.parameters = stmt.getText(3);
            tool.executorType = stmt.getText(4);
            tool.executorConfig = stmt.getText(5);
            tool.scriptContent = stmt.getText(6);
            tool.readme = stmt.getText(7);
            tool.enabled = stmt.getInt(8) == 1;
            tools.push_back(std::move(tool));
        }
        return tools;
    }

    std::vector<ToolStore::CustomTool> ToolStore::getCustomTools() const {
        return loadCustomTools(false);
    }

    std::vector<ToolStore::CustomTool> ToolStore::getEnabledCustomTools() const {
        return loadCustomTools(true);
    }

    int ToolStore::addCustomTool(const CustomTool &tool) const {
        auto &db = Database::instance();
        std::unique_lock lock(db.mutex());
        Statement stmt(db.handle(),
                       "INSERT INTO custom_tools (name, description, parameters, executor_type, executor_config, script_content, readme, enabled) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
        stmt.bind(1, tool.name);
        stmt.bind(2, tool.description);
        stmt.bind(3, tool.parameters);
        stmt.bind(4, tool.executorType);
        stmt.bind(5, tool.executorConfig);
        stmt.bind(6, tool.scriptContent);
        stmt.bind(7, tool.readme);
        stmt.bind(8, tool.enabled ? 1 : 0);
        stmt.exec();
        spdlog::info("已添加自定义工具: {}", tool.name);
        return static_cast<int>(Statement::lastInsertRowId(db.handle()));
    }

    void ToolStore::updateCustomTool(const CustomTool &tool) const {
        auto &db = Database::instance();
        std::unique_lock lock(db.mutex());
        Statement stmt(db.handle(),
                       "UPDATE custom_tools SET name=?, description=?, parameters=?, executor_type=?, executor_config=?, script_content=?, readme=?, enabled=? "
                       "WHERE id=?");
        stmt.bind(1, tool.name);
        stmt.bind(2, tool.description);
        stmt.bind(3, tool.parameters);
        stmt.bind(4, tool.executorType);
        stmt.bind(5, tool.executorConfig);
        stmt.bind(6, tool.scriptContent);
        stmt.bind(7, tool.readme);
        stmt.bind(8, tool.enabled ? 1 : 0);
        stmt.bind(9, tool.id);
        stmt.exec();
        spdlog::info("已更新自定义工具: {}", tool.name);
    }

    void ToolStore::deleteCustomTool(const int id) const {
        auto &db = Database::instance();
        std::unique_lock lock(db.mutex());
        Statement stmt(db.handle(), "DELETE FROM custom_tools WHERE id=?");
        stmt.bind(1, id);
        stmt.exec();
        spdlog::info("已删除自定义工具 ID: {}", id);
    }

    void ToolStore::toggleCustomTool(const int id) const {
        auto &db = Database::instance();
        std::unique_lock lock(db.mutex());
        Statement stmt(db.handle(), "UPDATE custom_tools SET enabled = NOT enabled WHERE id=?");
        stmt.bind(1, id);
        stmt.exec();
    }

    bool ToolStore::hasCustomTool(const std::string &name) const {
        auto &db = Database::instance();
        std::shared_lock lock(db.mutex());
        Statement stmt(db.handle(), "SELECT 1 FROM custom_tools WHERE name=?");
        stmt.bind(1, name);
        return stmt.step();
    }

    std::string ToolStore::getCustomToolPython() const {
        auto &db = Database::instance();
        std::shared_lock lock(db.mutex());
        Statement stmt(db.handle(), "SELECT value FROM settings WHERE key='custom_tool_python'");
        if (stmt.step()) {
            return stmt.getText(0);
        }
        return "python3"; // 默认值
    }

    void ToolStore::setCustomToolPython(const std::string &pythonPath) const {
        auto &db = Database::instance();
        std::unique_lock lock(db.mutex());
        Statement stmt(db.handle(),
                       "INSERT OR REPLACE INTO settings (key, value) VALUES ('custom_tool_python', ?)");
        stmt.bind(1, pythonPath);
        stmt.exec();
        spdlog::info("自定义工具Python路径已设置: {}", pythonPath);
    }
} // namespace insoulforge