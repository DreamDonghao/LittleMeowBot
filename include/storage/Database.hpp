/// @file Database.hpp
/// @brief SQLite 数据库管理 - 持久化存储层
/// @author donghao
/// @date 2026-04-02
/// @details 提供完整的数据库管理功能：
///          - 群组配置：启用群列表、群名称、消息统计
///          - 聊天记录：消息缓存、历史记录查询
///          - 记忆系统：长期记忆存储
///          - 配置管理：LLM配置、知识库配置、QQ Bot配置
///          - 自定义工具：工具注册、脚本存储
///          使用读写锁保证线程安全，支持运行时迁移

#pragma once
#include <sqlite3.h>
#include <json/json.h>
#include <shared_mutex>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <filesystem>

namespace LittleMeowBot {
    /// @brief 群组配置结构
    struct GroupConfig{
        uint64_t allMesCount = 0;
        uint64_t allCharCount = 0;
    };

    /// @brief SQLite 数据库管理类
    class Database{
    public:
        static Database& instance();

        /// @brief 初始化数据库
        void initialize(const std::string& dbPath);

        /// @brief 关闭数据库
        void close();

        // ============================================================
        //                      群组配置操作
        // ============================================================

        GroupConfig getGroupConfig(uint64_t groupId) const;
        void saveGroupConfig(uint64_t groupId, const GroupConfig& config) const;
        void incrementMessageCount(uint64_t groupId, size_t charCount) const;
        bool hasGroupConfig(uint64_t groupId) const;

        // ============================================================
        //                      聊天记录操作
        // ============================================================

        void addChatRecord(uint64_t groupId, const std::string& role, const std::string& content) const;
        std::vector<Json::Value> getChatRecords(uint64_t groupId, int limit = 50) const;
        std::vector<Json::Value> getChatRecordsWithIds(uint64_t groupId, int limit = 50) const;
        std::string getChatRecordsText(uint64_t groupId, int limit = 12, const std::string& botName = "小喵") const;
        size_t getChatRecordCount(uint64_t groupId) const;
        void clearOldRecords(uint64_t groupId, int keepLast = 12) const;

        // ============================================================
        //                      长期记忆操作
        // ============================================================

        std::string getLongTermMemory(uint64_t groupId);
        void updateLongTermMemory(uint64_t groupId, const std::string& memory) const;

        // ============================================================
        //                      消息缓存操作
        // ============================================================

        void cacheMessage(uint64_t messageId, const std::string& formattedText);
        std::optional<std::string> getCachedMessage(uint64_t messageId);

        // ============================================================
        //                      提示词操作
        // ============================================================

        std::string getPrompt(const std::string& key, const std::string& defaultValue = "");
        void setPrompt(const std::string& key, const std::string& content, const std::string& description = "");
        bool hasPrompt(const std::string& key);
        std::unordered_map<std::string, std::string> getAllPrompts();

        // ============================================================
        //                      启用群聊操作
        // ============================================================

        bool isGroupEnabled(uint64_t groupId) const;
        void enableGroup(uint64_t groupId) const;
        void disableGroup(uint64_t groupId) const;
        std::vector<uint64_t> getEnabledGroups() const;
        std::vector<std::pair<uint64_t, std::string>> getEnabledGroupsWithNames() const;

        /// @brief 获取所有有聊天记录的群（用于聊天记录页面）
        std::vector<std::tuple<uint64_t, std::string, int>> getGroupsWithChatRecords() const;

        /// @brief 获取所有群（包括已禁用的）
        std::vector<std::tuple<uint64_t, std::string, bool, int>> getAllGroupsWithStatus() const;

        /// @brief 切换群启用状态
        void toggleGroupStatus(uint64_t groupId) const;

        /// @brief 更新聊天记录内容
        void updateChatRecord(int recordId, const std::string& content) const;

        /// @brief 删除聊天记录
        void deleteChatRecord(int recordId) const;

        /// @brief 清空群的所有聊天记录
        void clearGroupChatRecords(uint64_t groupId) const;

        void updateGroupName(uint64_t groupId, const std::string& name) const;
        std::string getGroupName(uint64_t groupId);

        // ============================================================
        //                      管理员操作
        // ============================================================

        bool isAdmin(uint64_t qqNumber);
        void addAdmin(uint64_t qqNumber);
        void removeAdmin(uint64_t qqNumber) const;
        std::vector<uint64_t> getAdmins() const;

        // ============================================================
        //                      表情库操作
        // ============================================================

        std::string getEmoji(const std::string& name);
        void addEmoji(const std::string& name, const std::string& path, const std::string& description = "") const;
        void removeEmoji(const std::string& name);
        std::unordered_map<std::string, std::string> getAllEmojis() const;

        // ============================================================
        //                      LLM 配置操作
        // ============================================================

        Json::Value getLLMConfig(const std::string& name) const;
        void saveLLMConfig(const std::string& name, const Json::Value& config) const;
        Json::Value getAllLLMConfigs();

        // ============================================================
        //                      知识库配置操作
        // ============================================================

        Json::Value getKBConfig() const;
        void saveKBConfig(const Json::Value& config) const;
        bool hasKBConfig() const;

        // ============================================================
        //                      QQ Bot 配置操作
        // ============================================================

        Json::Value getQQConfig() const;
        void saveQQConfig(const Json::Value& config) const;
        bool hasQQConfig() const;

        // ============================================================
        //                      记忆配置操作
        // ============================================================

        Json::Value getMemoryConfig() const;
        void saveMemoryConfig(const Json::Value& config) const;
        bool hasMemoryConfig() const;

        // ============================================================
        //                      自定义工具操作
        // ============================================================

        /// @brief 自定义工具结构
        struct CustomTool{
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

        /// @brief 获取所有自定义工具
        std::vector<CustomTool> getCustomTools() const;

        /// @brief 获取启用的自定义工具（供 AgentToolManager 使用）
        std::vector<CustomTool> getEnabledCustomTools() const;

        /// @brief 添加自定义工具
        int addCustomTool(const CustomTool& tool) const;

        /// @brief 更新自定义工具
        void updateCustomTool(const CustomTool& tool) const;

        /// @brief 删除自定义工具
        void deleteCustomTool(int id) const;

        /// @brief 切换自定义工具启用状态
        void toggleCustomTool(int id) const;

        /// @brief 检查工具名是否已存在
        bool hasCustomTool(const std::string& name) const;

        // ============================================================
        //                      自定义工具配置
        // ============================================================

        /// @brief 获取自定义工具Python解释器路径
        std::string getCustomToolPython() const;

        /// @brief 设置自定义工具Python解释器路径
        void setCustomToolPython(const std::string& pythonPath) const;

    private:
        Database() = default;
        ~Database();

        sqlite3* m_db = nullptr;
        mutable std::shared_mutex m_mutex;

        void createTables();
        void migrateDatabase() const;
        void initDefaultLLMConfigs() const;
        void initDefaultKBConfig() const;
        void initDefaultMemoryConfig() const;
        void initDefaultQQConfig() const;
    };
} // namespace LittleMeowBot