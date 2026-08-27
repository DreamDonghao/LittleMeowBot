/// @file Database.hpp
/// @brief SQLite 数据库管理 - 持久化存储层
/// @author donghao
/// @date 2026-04-02
/// @details 提供完整的数据库管理功能：
///          - 群组配置：启用群列表、群名称、消息统计
///          - 聊天记录：历史记录查询
///          - 记忆系统：短期记忆存储
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

namespace LittleMeowBot {
    /// @brief 群组配置结构
    struct SessionConfig {
        uint64_t allMesCount = 0;
        uint64_t allCharCount = 0;
    };

    /// @brief SQLite 数据库管理类
    class Database {
    public:
        static Database &instance();

        /// @brief 初始化数据库
        void initialize(const std::string &dbPath);

        /// @brief 关闭数据库
        void close();

        // ============================================================
        //                      群组配置操作
        // ============================================================

        SessionConfig getSessionConfig(uint64_t sessionId) const;

        void saveSessionConfig(uint64_t sessionId, const SessionConfig &config) const;

        void incrementMessageCount(uint64_t sessionId, size_t charCount) const;

        bool hasSessionConfig(uint64_t sessionId) const;

        // ============================================================
        //                      聊天记录操作
        // ============================================================

        void addChatRecord(uint64_t sessionId, const std::string &role, const std::string &content) const;

        std::vector<Json::Value> getChatRecords(uint64_t sessionId, int limit = 50) const;

        std::vector<Json::Value> getChatRecordsWithIds(uint64_t sessionId, int limit = 50) const;

        /// @brief 获取水位线之后的最新记录（旧→新），limit<=0 表示不限
        std::vector<Json::Value> getChatRecordsSince(uint64_t sessionId, uint64_t watermarkId, int limit = 0) const;

        /// @brief 统计水位线之后的记录条数
        size_t getChatRecordCountSince(uint64_t sessionId, uint64_t watermarkId) const;

        // ============================================================
        //                      长期记忆操作
        // ============================================================

        std::string getShortTermMemory(uint64_t sessionId) const;

        void updateShortTermMemory(uint64_t sessionId, const std::string &memory) const;

        /// @brief 获取群记忆水位线（最后已提取的聊天记录 id，无记录时为 0）
        uint64_t getMemoryWatermark(uint64_t sessionId) const;

        /// @brief 原子更新记忆与水位线（单条 upsert 语句，崩溃安全）
        void updateShortTermMemoryWithWatermark(uint64_t sessionId, const std::string &memory,
                                                uint64_t watermarkId) const;

        // ============================================================
        //                      提示词操作
        // ============================================================

        std::string getPrompt(const std::string &key, const std::string &defaultValue = "") const;

        void setPrompt(const std::string &key, const std::string &content, const std::string &description = "");

        bool hasPrompt(const std::string &key) const;

        std::unordered_map<std::string, std::string> getAllPrompts() const;

        // ============================================================
        //                      启用群聊操作
        // ============================================================

        bool isSessionEnabled(uint64_t sessionId) const;

        void enableSession(uint64_t sessionId) const;

        void disableSession(uint64_t sessionId) const;

        std::vector<uint64_t> getEnabledGroups() const;

        /// @brief 获取所有有聊天记录的群（用于聊天记录页面）
        std::vector<std::tuple<uint64_t, std::string, int> > getSessionsWithChatRecords() const;

        /// @brief 获取所有群（包括已禁用的）
        std::vector<std::tuple<uint64_t, std::string, bool, int> > getAllSessionsWithStatus() const;

        /// @brief 切换群启用状态
        void toggleSessionStatus(uint64_t sessionId) const;

        /// @brief 更新聊天记录内容
        void updateChatRecord(int recordId, const std::string &content) const;

        /// @brief 删除聊天记录
        void deleteChatRecord(int recordId) const;

        /// @brief 清空群的所有聊天记录
        void clearSessionChatRecords(uint64_t sessionId) const;

        void updateSessionName(uint64_t sessionId, const std::string &name) const;

        std::string getSessionName(uint64_t sessionId) const;

        // ============================================================
        //                      管理员操作
        // ============================================================

        bool isAdmin(uint64_t qqNumber) const;

        void addAdmin(uint64_t qqNumber) const;

        void removeAdmin(uint64_t qqNumber) const;

        std::vector<uint64_t> getAdmins() const;


        // ============================================================
        //                      LLM 配置操作
        // ============================================================

        Json::Value getLLMConfig(const std::string &name) const;

        void saveLLMConfig(const std::string &name, const Json::Value &config) const;

        Json::Value getAllLLMConfigs();

        // ============================================================
        //                      知识库配置操作
        // ============================================================

        Json::Value getKBConfig() const;

        void saveKBConfig(const Json::Value &config) const;

        // ============================================================
        //                      QQ Bot 配置操作
        // ============================================================

        Json::Value getQQConfig() const;

        void saveQQConfig(const Json::Value &config) const;

        // ============================================================
        //                      记忆配置操作
        // ============================================================

        Json::Value getMemoryConfig() const;

        void saveMemoryConfig(const Json::Value &config) const;

        // ============================================================
        //                      用量统计操作
        // ============================================================

        /// @brief 记录一次 LLM 调用用量
        void addUsageRecord(const std::string &role, const std::string &model, int promptTokens,
                            int completionTokens, int totalTokens, int cachedTokens) const;

        /// @brief 获取最近 N 天用量汇总（按角色、按天聚合）
        Json::Value getUsageSummary(int days) const;

        /// @brief 获取最近调用明细
        Json::Value getRecentUsage(int limit) const;

        // ============================================================
        //                      自定义工具操作
        // ============================================================

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

        // ============================================================
        //                      自定义工具配置
        // ============================================================

        /// @brief 获取自定义工具Python解释器路径
        std::string getCustomToolPython() const;

        /// @brief 设置自定义工具Python解释器路径
        void setCustomToolPython(const std::string &pythonPath) const;

    private:
        Database() = default;

        ~Database();

        sqlite3 *m_db = nullptr;
        mutable std::shared_mutex m_mutex;

        void createTables();

        void migrateDatabase() const;

        /// @brief 从数据库加载自定义工具（onlyEnabled 时仅加载启用的）
        std::vector<CustomTool> loadCustomTools(bool onlyEnabled) const;

        void initDefaultLLMConfigs() const;

        void initDefaultKBConfig() const;

        void initDefaultMemoryConfig() const;

        void initDefaultQQConfig() const;
    };
} // namespace LittleMeowBot
