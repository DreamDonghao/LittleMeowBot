/// @file Database.cpp
/// @brief SQLite 数据库管理 - 实现
/// @author donghao
/// @date 2026-04-02

#include <storage/Database.hpp>
#include <spdlog/spdlog.h>
#include <array>
#include <algorithm>
#include <storage/Statement.hpp>
namespace LittleMeowBot {
    Database& Database::instance(){
        static Database db;
        return db;
    }

    Database::~Database(){
        close();
    }

    void Database::initialize(const std::string& dbPath){
        std::unique_lock lock(m_mutex);

        // 创建数据目录
        std::filesystem::path p(dbPath);
        if (p.has_parent_path() && !std::filesystem::exists(p.parent_path())) {
            std::filesystem::create_directories(p.parent_path());
        }

        // 打开数据库
        if (sqlite3_open(dbPath.c_str(), &m_db) != SQLITE_OK) {
            spdlog::error("无法打开数据库: {}", sqlite3_errmsg(m_db));
            return;
        }

        spdlog::info("数据库已打开: {}", dbPath);
        createTables();
        spdlog::info("数据库初始化完成");
    }

    void Database::close(){
        std::unique_lock lock(m_mutex);
        if (m_db) {
            sqlite3_close(m_db);
            m_db = nullptr;
            spdlog::info("数据库已关闭");
        }
    }

    // ============================================================
    //                      群组配置操作
    // ============================================================

    GroupConfig Database::getGroupConfig(uint64_t groupId) const{
        std::shared_lock lock(m_mutex);
        GroupConfig config;
        Statement stmt(m_db, "SELECT all_mes_count, all_char_count FROM group_config WHERE group_id = ?");
        stmt.bind(1, groupId);
        if (stmt.step()) {
            config.allMesCount = stmt.getInt64(0);
            config.allCharCount = stmt.getInt64(1);
        }
        return config;
    }

    void Database::saveGroupConfig(uint64_t groupId, const GroupConfig& config) const{
        std::unique_lock lock(m_mutex);
        Statement stmt(
            m_db, "INSERT OR REPLACE INTO group_config (group_id, all_mes_count, all_char_count) VALUES (?, ?, ?)");
        stmt.bind(1, groupId);
        stmt.bind(2, config.allMesCount);
        stmt.bind(3, config.allCharCount);
        stmt.exec();
    }

    void Database::incrementMessageCount(uint64_t groupId, size_t charCount) const{
        std::unique_lock lock(m_mutex);

        Statement stmt(
            m_db,
            "UPDATE group_config SET all_mes_count = all_mes_count + 1, all_char_count = all_char_count + ? WHERE group_id = ?");
        stmt.bind(1, charCount);
        stmt.bind(2, groupId);
        stmt.exec();

        if (Statement::changes(m_db) == 0) {
            GroupConfig config{1, charCount};
            saveGroupConfig(groupId, config);
        }
    }

    bool Database::hasGroupConfig(uint64_t groupId) const{
        std::shared_lock lock(m_mutex);
        Statement stmt(m_db, "SELECT 1 FROM group_config WHERE group_id = ?");
        stmt.bind(1, groupId);
        return stmt.step();
    }

    // ============================================================
    //                      聊天记录操作
    // ============================================================

    void Database::addChatRecord(uint64_t groupId, const std::string& role, const std::string& content) const{
        std::unique_lock lock(m_mutex);
        Statement stmt(m_db, "INSERT INTO chat_records (group_id, role, content) VALUES (?, ?, ?)");
        stmt.bind(1, groupId);
        stmt.bind(2, role);
        stmt.bind(3, content);
        stmt.exec();
    }

    std::vector<Json::Value> Database::getChatRecords(uint64_t groupId, int limit) const{
        std::shared_lock lock(m_mutex);
        std::vector<Json::Value> records;

        Statement stmt(m_db, "SELECT role, content FROM chat_records WHERE group_id = ? ORDER BY id DESC LIMIT ?");
        stmt.bind(1, groupId);
        stmt.bind(2, limit);

        while (stmt.step()) {
            Json::Value record;
            record["role"] = stmt.getText(0);
            record["content"] = stmt.getText(1);
            records.push_back(record);
        }

        std::ranges::reverse(records);
        return records;
    }

    std::vector<Json::Value> Database::getChatRecordsWithIds(uint64_t groupId, int limit) const{
        std::shared_lock lock(m_mutex);
        std::vector<Json::Value> records;

        Statement stmt(m_db, "SELECT id, role, content FROM chat_records WHERE group_id = ? ORDER BY id DESC LIMIT ?");
        stmt.bind(1, groupId);
        stmt.bind(2, limit);

        while (stmt.step()) {
            Json::Value record;
            record["id"] = stmt.getInt64(0);
            record["role"] = stmt.getText(1);
            record["content"] = stmt.getText(2);
            records.push_back(record);
        }

        return records;
    }

    std::string Database::getChatRecordsText(uint64_t groupId, int limit, const std::string& botName) const{
        const auto records = getChatRecords(groupId, limit);
        std::string text = "[";
        bool first = true;
        for (const auto& record : records) {
            if (!first) {
                text += ",";
            }
            first = false;
            // 消息内容已经是JSON格式
            text += record["content"].asString();
        }
        text += "]";
        return text;
    }

    size_t Database::getChatRecordCount(uint64_t groupId) const{
        std::shared_lock lock(m_mutex);
        Statement stmt(m_db, "SELECT COUNT(*) FROM chat_records WHERE group_id = ?");
        stmt.bind(1, groupId);
        return stmt.step() ? stmt.getInt64(0) : 0;
    }

    void Database::clearOldRecords(uint64_t groupId, int keepLast) const{
        std::unique_lock lock(m_mutex);
        Statement stmt(
            m_db,
            "DELETE FROM chat_records WHERE group_id = ? AND id NOT IN (SELECT id FROM chat_records WHERE group_id = ? ORDER BY id DESC LIMIT ?)");
        stmt.bind(1, groupId);
        stmt.bind(2, groupId);
        stmt.bind(3, keepLast);
        stmt.exec();
    }

    // ============================================================
    //                      长期记忆操作
    // ============================================================

    std::string Database::getLongTermMemory(uint64_t groupId){
        std::shared_lock lock(m_mutex);
        Statement stmt(m_db, "SELECT memory_content FROM long_term_memory WHERE group_id = ?");
        stmt.bind(1, groupId);
        return stmt.step() ? stmt.getText(0) : "";
    }

    void Database::updateLongTermMemory(uint64_t groupId, const std::string& memory) const{
        std::unique_lock lock(m_mutex);
        Statement stmt(m_db, "INSERT OR REPLACE INTO long_term_memory (group_id, memory_content) VALUES (?, ?)");
        stmt.bind(1, groupId);
        stmt.bind(2, memory);
        stmt.exec();
    }

    // ============================================================
    //                      消息缓存操作
    // ============================================================

    void Database::cacheMessage(uint64_t messageId, const std::string& formattedText){
        std::unique_lock lock(m_mutex);
        Statement stmt(m_db, "INSERT OR REPLACE INTO message_cache (message_id, formatted_text) VALUES (?, ?)");
        stmt.bind(1, messageId);
        stmt.bind(2, formattedText);
        stmt.exec();
    }

    std::optional<std::string> Database::getCachedMessage(uint64_t messageId){
        std::shared_lock lock(m_mutex);
        Statement stmt(m_db, "SELECT formatted_text FROM message_cache WHERE message_id = ?");
        stmt.bind(1, messageId);
        if (stmt.step()) {
            return stmt.getText(0);
        }
        return std::nullopt;
    }

    // ============================================================
    //                      提示词操作
    // ============================================================

    std::string Database::getPrompt(const std::string& key, const std::string& defaultValue){
        std::shared_lock lock(m_mutex);
        Statement stmt(m_db, "SELECT prompt_content FROM prompts WHERE prompt_key = ?");
        stmt.bind(1, key);
        return stmt.step() ? stmt.getText(0) : defaultValue;
    }

    void Database::setPrompt(const std::string& key, const std::string& content, const std::string& description){
        std::unique_lock lock(m_mutex);
        Statement stmt(
            m_db, "INSERT OR REPLACE INTO prompts (prompt_key, prompt_content, description) VALUES (?, ?, ?)");
        stmt.bind(1, key);
        stmt.bind(2, content);
        stmt.bind(3, description);
        stmt.exec();
    }

    bool Database::hasPrompt(const std::string& key){
        std::shared_lock lock(m_mutex);
        Statement stmt(m_db, "SELECT 1 FROM prompts WHERE prompt_key = ?");
        stmt.bind(1, key);
        return stmt.step();
    }

    std::unordered_map<std::string, std::string> Database::getAllPrompts(){
        std::shared_lock lock(m_mutex);
        std::unordered_map<std::string, std::string> prompts;

        Statement stmt(m_db, "SELECT prompt_key, prompt_content FROM prompts");
        while (stmt.step()) {
            prompts[stmt.getText(0)] = stmt.getText(1);
        }
        return prompts;
    }

    // ============================================================
    //                      启用群聊操作
    // ============================================================

    bool Database::isGroupEnabled(uint64_t groupId) const{
        std::shared_lock lock(m_mutex);
        Statement stmt(m_db, "SELECT enabled FROM enabled_groups WHERE group_id = ?");
        stmt.bind(1, groupId);
        return stmt.step() && stmt.getInt(0) == 1;
    }

    void Database::enableGroup(uint64_t groupId) const{
        std::unique_lock lock(m_mutex);
        Statement stmt(m_db, "INSERT OR REPLACE INTO enabled_groups (group_id, enabled) VALUES (?, 1)");
        stmt.bind(1, groupId);
        stmt.exec();
        spdlog::info("已启用群: {}", groupId);
    }

    void Database::disableGroup(uint64_t groupId) const{
        std::unique_lock lock(m_mutex);
        Statement stmt(m_db, "DELETE FROM enabled_groups WHERE group_id = ?");
        stmt.bind(1, groupId);
        stmt.exec();
        spdlog::info("已禁用群: {}", groupId);
    }

    std::vector<uint64_t> Database::getEnabledGroups() const{
        std::shared_lock lock(m_mutex);
        std::vector<uint64_t> groups;
        Statement stmt(m_db, "SELECT group_id FROM enabled_groups WHERE enabled = 1");
        while (stmt.step()) {
            groups.push_back(stmt.getInt64(0));
        }
        return groups;
    }

    std::vector<std::pair<uint64_t, std::string>> Database::getEnabledGroupsWithNames() const{
        std::shared_lock lock(m_mutex);
        std::vector<std::pair<uint64_t, std::string>> groups;
        Statement stmt(m_db, "SELECT group_id, group_name FROM enabled_groups WHERE enabled = 1");
        while (stmt.step()) {
            groups.emplace_back(stmt.getInt64(0), stmt.getText(1));
        }
        return groups;
    }

    std::vector<std::tuple<uint64_t, std::string, int>> Database::getGroupsWithChatRecords() const{
        std::shared_lock lock(m_mutex);
        std::vector<std::tuple<uint64_t, std::string, int>> groups;
        Statement stmt(m_db,
                       "SELECT cr.group_id, COALESCE(eg.group_name, ''), COUNT(*) as cnt "
                       "FROM chat_records cr "
                       "LEFT JOIN enabled_groups eg ON cr.group_id = eg.group_id "
                       "GROUP BY cr.group_id "
                       "ORDER BY cnt DESC");
        while (stmt.step()) {
            groups.emplace_back(stmt.getInt64(0), stmt.getText(1), stmt.getInt(2));
        }
        return groups;
    }

    std::vector<std::tuple<uint64_t, std::string, bool, int>> Database::getAllGroupsWithStatus() const{
        std::shared_lock lock(m_mutex);
        std::vector<std::tuple<uint64_t, std::string, bool, int>> groups;
        Statement stmt(m_db,
                       "SELECT eg.group_id, eg.group_name, eg.enabled, "
                       "(SELECT COUNT(*) FROM chat_records WHERE group_id = eg.group_id) as cnt "
                       "FROM enabled_groups eg "
                       "ORDER BY eg.enabled DESC, cnt DESC");
        while (stmt.step()) {
            groups.emplace_back(stmt.getInt64(0), stmt.getText(1), stmt.getInt(2) == 1, stmt.getInt(3));
        }
        return groups;
    }

    void Database::toggleGroupStatus(uint64_t groupId) const{
        std::unique_lock lock(m_mutex);
        Statement stmt(m_db, "UPDATE enabled_groups SET enabled = NOT enabled WHERE group_id = ?");
        stmt.bind(1, groupId);
        stmt.exec();
    }

    void Database::updateChatRecord(int recordId, const std::string& content) const{
        std::unique_lock lock(m_mutex);
        Statement stmt(m_db, "UPDATE chat_records SET content = ? WHERE id = ?");
        stmt.bind(1, content);
        stmt.bind(2, recordId);
        stmt.exec();
    }

    void Database::deleteChatRecord(int recordId) const{
        std::unique_lock lock(m_mutex);
        Statement stmt(m_db, "DELETE FROM chat_records WHERE id = ?");
        stmt.bind(1, recordId);
        stmt.exec();
    }

    void Database::clearGroupChatRecords(uint64_t groupId) const{
        std::unique_lock lock(m_mutex);
        Statement stmt(m_db, "DELETE FROM chat_records WHERE group_id = ?");
        stmt.bind(1, groupId);
        stmt.exec();
        spdlog::info("已清空群 {} 的聊天记录", groupId);
    }

    void Database::updateGroupName(uint64_t groupId, const std::string& name) const{
        std::unique_lock lock(m_mutex);
        Statement stmt(m_db, "UPDATE enabled_groups SET group_name = ? WHERE group_id = ?");
        stmt.bind(1, name);
        stmt.bind(2, groupId);
        stmt.exec();
        spdlog::info("更新群名称: {} -> {}", groupId, name);
    }

    std::string Database::getGroupName(uint64_t groupId){
        std::shared_lock lock(m_mutex);
        Statement stmt(m_db, "SELECT group_name FROM enabled_groups WHERE group_id = ?");
        stmt.bind(1, groupId);
        return stmt.step() ? stmt.getText(0) : "";
    }

    // ============================================================
    //                      管理员操作
    // ============================================================

    bool Database::isAdmin(uint64_t qqNumber){
        std::shared_lock lock(m_mutex);
        Statement stmt(m_db, "SELECT 1 FROM admins WHERE qq_number = ?");
        stmt.bind(1, qqNumber);
        return stmt.step();
    }

    void Database::addAdmin(uint64_t qqNumber){
        std::unique_lock lock(m_mutex);
        Statement stmt(m_db, "INSERT OR IGNORE INTO admins (qq_number) VALUES (?)");
        stmt.bind(1, qqNumber);
        stmt.exec();
        spdlog::info("已添加管理员: {}", qqNumber);
    }

    void Database::removeAdmin(uint64_t qqNumber) const{
        std::unique_lock lock(m_mutex);
        Statement stmt(m_db, "DELETE FROM admins WHERE qq_number = ?");
        stmt.bind(1, qqNumber);
        stmt.exec();
        spdlog::info("已移除管理员: {}", qqNumber);
    }

    std::vector<uint64_t> Database::getAdmins() const{
        std::shared_lock lock(m_mutex);
        std::vector<uint64_t> admins;
        Statement stmt(m_db, "SELECT qq_number FROM admins");
        while (stmt.step()) {
            admins.push_back(stmt.getInt64(0));
        }
        return admins;
    }

    // ============================================================
    //                      表情库操作
    // ============================================================

    std::string Database::getEmoji(const std::string& name){
        std::shared_lock lock(m_mutex);
        Statement stmt(m_db, "SELECT path FROM emojis WHERE name = ?");
        stmt.bind(1, name);
        return stmt.step() ? stmt.getText(0) : "";
    }

    void Database::addEmoji(const std::string& name, const std::string& path, const std::string& description) const{
        std::unique_lock lock(m_mutex);
        Statement stmt(m_db, "INSERT OR REPLACE INTO emojis (name, path, description) VALUES (?, ?, ?)");
        stmt.bind(1, name);
        stmt.bind(2, path);
        stmt.bind(3, description);
        stmt.exec();
        spdlog::info("已添加表情: {} -> {}", name, path);
    }

    void Database::removeEmoji(const std::string& name){
        std::unique_lock lock(m_mutex);
        Statement stmt(m_db, "DELETE FROM emojis WHERE name = ?");
        stmt.bind(1, name);
        stmt.exec();
        spdlog::info("已删除表情: {}", name);
    }

    std::unordered_map<std::string, std::string> Database::getAllEmojis() const{
        std::shared_lock lock(m_mutex);
        std::unordered_map<std::string, std::string> emojis;
        Statement stmt(m_db, "SELECT name, path FROM emojis");
        while (stmt.step()) {
            emojis[stmt.getText(0)] = stmt.getText(1);
        }
        return emojis;
    }

    // ============================================================
    //                      LLM 配置操作
    // ============================================================

    Json::Value Database::getLLMConfig(const std::string& name) const{
        std::shared_lock lock(m_mutex);
        Json::Value config;

        Statement stmt(
            m_db,
            "SELECT api_key, base_url, path, model, max_tokens, temperature, top_p FROM llm_config WHERE name = ?");
        stmt.bind(1, name);

        if (stmt.step()) {
            config["apiKey"] = stmt.getText(0);
            config["baseUrl"] = stmt.getText(1);
            config["path"] = stmt.getText(2);
            config["model"] = stmt.getText(3);
            config["maxTokens"] = stmt.getInt(4);
            config["temperature"] = stmt.getDouble(5);
            config["topP"] = stmt.getDouble(6);
        }
        return config;
    }

    void Database::saveLLMConfig(const std::string& name, const Json::Value& config) const{
        std::unique_lock lock(m_mutex);

        Statement stmt(
            m_db,
            "INSERT OR REPLACE INTO llm_config (name, api_key, base_url, path, model, max_tokens, temperature, top_p) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
        stmt.bind(1, name);
        stmt.bind(2, config["apiKey"].asString());
        stmt.bind(3, config["baseUrl"].asString());
        stmt.bind(4, config["path"].asString());
        stmt.bind(5, config["model"].asString());
        stmt.bind(6, config["maxTokens"].asInt());
        stmt.bind(7, config["temperature"].asFloat());
        stmt.bind(8, config["topP"].asFloat());
        stmt.exec();
    }

    Json::Value Database::getAllLLMConfigs(){
        std::shared_lock lock(m_mutex);
        Json::Value configs;

        Statement stmt(
            m_db, "SELECT name, api_key, base_url, path, model, max_tokens, temperature, top_p FROM llm_config");
        while (stmt.step()) {
            Json::Value cfg;
            cfg["apiKey"] = stmt.getText(1);
            cfg["baseUrl"] = stmt.getText(2);
            cfg["path"] = stmt.getText(3);
            cfg["model"] = stmt.getText(4);
            cfg["maxTokens"] = stmt.getInt(5);
            cfg["temperature"] = stmt.getDouble(6);
            cfg["topP"] = stmt.getDouble(7);
            configs[stmt.getText(0)] = cfg;
        }
        return configs;
    }

    // ============================================================
    //                      知识库配置操作
    // ============================================================

    Json::Value Database::getKBConfig() const{
        std::shared_lock lock(m_mutex);
        Json::Value config;

        Statement stmt(
            m_db,
            "SELECT api_key, base_url, knowledge_dataset_id, memory_dataset_id, memory_document_id FROM kb_config WHERE id = 1");
        if (stmt.step()) {
            config["apiKey"] = stmt.getText(0);
            config["baseUrl"] = stmt.getText(1);
            config["knowledgeDatasetId"] = stmt.getText(2);
            config["memoryDatasetId"] = stmt.getText(3);
            config["memoryDocumentId"] = stmt.getText(4);
        }
        return config;
    }

    void Database::saveKBConfig(const Json::Value& config) const{
        std::unique_lock lock(m_mutex);

        Statement stmt(
            m_db,
            "INSERT OR REPLACE INTO kb_config (id, api_key, base_url, knowledge_dataset_id, memory_dataset_id, memory_document_id) VALUES (1, ?, ?, ?, ?, ?)");
        stmt.bind(1, config["apiKey"].asString());
        stmt.bind(2, config["baseUrl"].asString());
        stmt.bind(3, config["knowledgeDatasetId"].asString());
        stmt.bind(4, config["memoryDatasetId"].asString());
        stmt.bind(5, config.isMember("memoryDocumentId") ? config["memoryDocumentId"].asString() : "");
        stmt.exec();
        spdlog::info("知识库配置已保存");
    }

    bool Database::hasKBConfig() const{
        std::shared_lock lock(m_mutex);
        Statement stmt(m_db, "SELECT 1 FROM kb_config WHERE id = 1");
        return stmt.step();
    }

    // ============================================================
    //                      QQ Bot 配置操作
    // ============================================================

    Json::Value Database::getQQConfig() const{
        std::shared_lock lock(m_mutex);
        Json::Value config;

        Statement stmt(
            m_db,
            "SELECT access_token, self_qq_number, qq_http_host, bot_name FROM qq_config WHERE id = 1");
        if (stmt.step()) {
            config["accessToken"] = stmt.getText(0);
            config["selfQQNumber"] = stmt.getInt64(1);
            config["qqHttpHost"] = stmt.getText(2);
            config["botName"] = stmt.getText(3);
        }
        return config;
    }

    void Database::saveQQConfig(const Json::Value& config) const{
        std::unique_lock lock(m_mutex);

        Statement stmt(
            m_db,
            "INSERT OR REPLACE INTO qq_config (id, access_token, self_qq_number, qq_http_host, bot_name) VALUES (1, ?, ?, ?, ?)");
        stmt.bind(1, config["accessToken"].asString());
        stmt.bind(2, config["selfQQNumber"].asInt64());
        stmt.bind(3, config["qqHttpHost"].asString());
        stmt.bind(4, config.isMember("botName") ? config["botName"].asString() : "小喵");
        stmt.exec();
        spdlog::info("QQ Bot 配置已保存");
    }

    bool Database::hasQQConfig() const{
        std::shared_lock lock(m_mutex);
        Statement stmt(m_db, "SELECT 1 FROM qq_config WHERE id = 1");
        return stmt.step();
    }

    // ============================================================
    //                      记忆配置操作
    // ============================================================

    Json::Value Database::getMemoryConfig() const{
        std::shared_lock lock(m_mutex);
        Json::Value config;

        Statement stmt(
            m_db,
            "SELECT memory_trigger_count, memory_chat_record_limit, short_term_memory_max, short_term_memory_limit, memory_migrate_count FROM memory_config WHERE id = 1");
        if (stmt.step()) {
            config["memoryTriggerCount"] = stmt.getInt(0);
            config["memoryChatRecordLimit"] = stmt.getInt(1);
            config["shortTermMemoryMax"] = stmt.getInt(2);
            config["shortTermMemoryLimit"] = stmt.getInt(3);
            config["memoryMigrateCount"] = stmt.getInt(4);
        }
        return config;
    }

    void Database::saveMemoryConfig(const Json::Value& config) const{
        std::unique_lock lock(m_mutex);

        Statement stmt(
            m_db,
            "INSERT OR REPLACE INTO memory_config (id, memory_trigger_count, memory_chat_record_limit, short_term_memory_max, short_term_memory_limit, memory_migrate_count) VALUES (1, ?, ?, ?, ?, ?)");
        stmt.bind(1, config["memoryTriggerCount"].asInt());
        stmt.bind(2, config["memoryChatRecordLimit"].asInt());
        stmt.bind(3, config["shortTermMemoryMax"].asInt());
        stmt.bind(4, config["shortTermMemoryLimit"].asInt());
        stmt.bind(5, config["memoryMigrateCount"].asInt());
        stmt.exec();
        spdlog::info("记忆配置已保存");
    }

    bool Database::hasMemoryConfig() const{
        std::shared_lock lock(m_mutex);
        Statement stmt(m_db, "SELECT 1 FROM memory_config WHERE id = 1");
        return stmt.step();
    }

    // ============================================================
    //                      自定义工具操作
    // ============================================================

    std::vector<Database::CustomTool> Database::getCustomTools() const{
        std::shared_lock lock(m_mutex);
        std::vector<CustomTool> tools;
        Statement stmt(m_db,
                       "SELECT id, name, description, parameters, executor_type, executor_config, script_content, readme, enabled "
                       "FROM custom_tools ORDER BY id");
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
            tools.push_back(tool);
        }
        return tools;
    }

    std::vector<Database::CustomTool> Database::getEnabledCustomTools() const{
        std::shared_lock lock(m_mutex);
        std::vector<CustomTool> tools;
        Statement stmt(m_db,
                       "SELECT id, name, description, parameters, executor_type, executor_config, script_content, readme "
                       "FROM custom_tools WHERE enabled = 1 ORDER BY id");
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
            tool.enabled = true;
            tools.push_back(tool);
        }
        return tools;
    }

    int Database::addCustomTool(const CustomTool& tool) const{
        std::unique_lock lock(m_mutex);
        Statement stmt(m_db,
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
        return sqlite3_last_insert_rowid(m_db);
    }

    void Database::updateCustomTool(const CustomTool& tool) const{
        std::unique_lock lock(m_mutex);
        Statement stmt(m_db,
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

    void Database::deleteCustomTool(int id) const{
        std::unique_lock lock(m_mutex);
        Statement stmt(m_db, "DELETE FROM custom_tools WHERE id=?");
        stmt.bind(1, id);
        stmt.exec();
        spdlog::info("已删除自定义工具 ID: {}", id);
    }

    void Database::toggleCustomTool(int id) const{
        std::unique_lock lock(m_mutex);
        Statement stmt(m_db, "UPDATE custom_tools SET enabled = NOT enabled WHERE id=?");
        stmt.bind(1, id);
        stmt.exec();
    }

    bool Database::hasCustomTool(const std::string& name) const{
        std::shared_lock lock(m_mutex);
        Statement stmt(m_db, "SELECT 1 FROM custom_tools WHERE name=?");
        stmt.bind(1, name);
        return stmt.step();
    }

    // ============================================================
    //                      自定义工具配置
    // ============================================================

    std::string Database::getCustomToolPython() const{
        std::shared_lock lock(m_mutex);
        Statement stmt(m_db, "SELECT value FROM settings WHERE key='custom_tool_python'");
        if (stmt.step()) {
            return stmt.getText(0);
        }
        return "python3"; // 默认值
    }

    void Database::setCustomToolPython(const std::string& pythonPath) const{
        std::unique_lock lock(m_mutex);
        Statement stmt(m_db,
                       "INSERT OR REPLACE INTO settings (key, value) VALUES ('custom_tool_python', ?)");
        stmt.bind(1, pythonPath);
        stmt.exec();
        spdlog::info("自定义工具Python路径已设置: {}", pythonPath);
    }

    // ============================================================
    //                      私有方法
    // ============================================================

    void Database::createTables(){
        constexpr std::array tables = {
            R"(CREATE TABLE IF NOT EXISTS group_config (
        group_id INTEGER PRIMARY KEY,
        all_mes_count INTEGER DEFAULT 0,
        all_char_count INTEGER DEFAULT 0
    ))",
            R"(CREATE TABLE IF NOT EXISTS chat_records (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        group_id INTEGER NOT NULL,
        role TEXT NOT NULL CHECK(role IN ('user', 'assistant')),
        content TEXT NOT NULL,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    ))",
            R"(CREATE TABLE IF NOT EXISTS long_term_memory (
        group_id INTEGER PRIMARY KEY,
        memory_content TEXT,
        updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    ))",
            R"(CREATE TABLE IF NOT EXISTS message_cache (
        message_id INTEGER PRIMARY KEY,
        formatted_text TEXT NOT NULL,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    ))",
            R"(CREATE TABLE IF NOT EXISTS prompts (
        prompt_key TEXT PRIMARY KEY,
        prompt_content TEXT NOT NULL,
        description TEXT,
        updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    ))",
            R"(CREATE TABLE IF NOT EXISTS enabled_groups (
        group_id INTEGER PRIMARY KEY,
        group_name TEXT,
        enabled INTEGER DEFAULT 1,
        added_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    ))",
            R"(CREATE TABLE IF NOT EXISTS admins (
        qq_number INTEGER PRIMARY KEY,
        added_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    ))",
            R"(CREATE TABLE IF NOT EXISTS emojis (
        name TEXT PRIMARY KEY,
        path TEXT NOT NULL,
        description TEXT,
        added_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    ))",
            R"(CREATE TABLE IF NOT EXISTS llm_config (
        name TEXT PRIMARY KEY,
        api_key TEXT,
        base_url TEXT,
        path TEXT,
        model TEXT,
        max_tokens INTEGER DEFAULT 1024,
        temperature REAL DEFAULT 0.7,
        top_p REAL DEFAULT 0.9
    ))",
            R"(CREATE TABLE IF NOT EXISTS kb_config (
        id INTEGER PRIMARY KEY CHECK (id = 1),
        api_key TEXT,
        base_url TEXT,
        knowledge_dataset_id TEXT,
        memory_dataset_id TEXT,
        memory_document_id TEXT
    ))",
            R"(CREATE TABLE IF NOT EXISTS memory_config (
        id INTEGER PRIMARY KEY CHECK (id = 1),
        memory_trigger_count INTEGER DEFAULT 16,
        memory_chat_record_limit INTEGER DEFAULT 18,
        short_term_memory_max INTEGER DEFAULT 15,
        short_term_memory_limit INTEGER DEFAULT 20,
        memory_migrate_count INTEGER DEFAULT 5
    ))",
            R"(CREATE TABLE IF NOT EXISTS qq_config (
        id INTEGER PRIMARY KEY CHECK (id = 1),
        access_token TEXT,
        self_qq_number INTEGER,
        qq_http_host TEXT,
        bot_name TEXT DEFAULT '小喵'
    ))",
            R"(CREATE TABLE IF NOT EXISTS custom_tools (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        name TEXT UNIQUE NOT NULL,
        description TEXT NOT NULL,
        parameters TEXT,
        executor_type TEXT NOT NULL CHECK(executor_type IN ('python', 'http')),
        executor_config TEXT,
        script_content TEXT,
        enabled INTEGER DEFAULT 1,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    ))",
            R"(CREATE TABLE IF NOT EXISTS settings (
        key TEXT PRIMARY KEY,
        value TEXT
    ))"
        };

        for (const auto* sql : tables) {
            auto* errMsg = static_cast<char*>(nullptr);
            if (sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
                spdlog::error("创建表失败: {}", errMsg);
                sqlite3_free(errMsg);
            }
        }

        // 创建索引
        constexpr std::array indexes = {
            "CREATE INDEX IF NOT EXISTS idx_chat_records_group ON chat_records(group_id)",
            "CREATE INDEX IF NOT EXISTS idx_chat_records_time ON chat_records(group_id, created_at DESC)"
        };
        for (const auto* sql : indexes) {
            sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);
        }

        // 数据库迁移
        migrateDatabase();

        initDefaultLLMConfigs();
        initDefaultKBConfig();
        initDefaultMemoryConfig();
        initDefaultQQConfig();
    }

    void Database::migrateDatabase() const{
        // 检查 custom_tools 表是否有 script_content 列
        bool hasScriptContent = false;
        bool hasReadme = false;
        auto callback = [](void* data, int argc, char** argv, char** colNames) -> int {
            for (int i = 0; i < argc; i++) {
                if (argv[i] && std::string(argv[i]) == "script_content") {
                    *static_cast<bool*>(data) = true;
                    break;
                }
            }
            return 0;
        };
        auto callbackReadme = [](void* data, int argc, char** argv, char** colNames) -> int {
            for (int i = 0; i < argc; i++) {
                if (argv[i] && std::string(argv[i]) == "readme") {
                    *static_cast<bool*>(data) = true;
                    break;
                }
            }
            return 0;
        };
        sqlite3_exec(m_db, "PRAGMA table_info(custom_tools)", callback, &hasScriptContent, nullptr);
        sqlite3_exec(m_db, "PRAGMA table_info(custom_tools)", callbackReadme, &hasReadme, nullptr);

        if (!hasScriptContent) {
            spdlog::info("数据库迁移: 添加 script_content 列");
            sqlite3_exec(m_db, "ALTER TABLE custom_tools ADD COLUMN script_content TEXT", nullptr, nullptr, nullptr);
        }
        if (!hasReadme) {
            spdlog::info("数据库迁移: 添加 readme 列");
            sqlite3_exec(m_db, "ALTER TABLE custom_tools ADD COLUMN readme TEXT", nullptr, nullptr, nullptr);
        }
    }

    void Database::initDefaultLLMConfigs() const{
        struct DefaultConfig{
            const char *name, *apiKey, *baseUrl, *path, *model;
            int maxTokens;
            float temperature, topP;
        };

        constexpr DefaultConfig defaults[] = {
            {"router", "", "http://127.0.0.1:3001", "/v1/chat/completions", "deepseek-chat", 100, 0.3f, 0.9f},
            {"planner", "", "http://127.0.0.1:3001", "/v1/chat/completions", "deepseek-chat", 300, 0.5f, 0.9f},
            {"executor", "", "http://127.0.0.1:3001", "/v1/chat/completions", "deepseek-chat", 150, 0.7f, 0.9f},
            {"executorThinking", "", "http://127.0.0.1:3001", "/v1/chat/completions", "deepseek-reasoner", 512, 0.7f, 0.9f},
            {"memory", "", "https://api.edgefn.net", "/v1/chat/completions", "DeepSeek-V3", 2048, 0.7f, 0.9f},
            {
                "image", "", "https://dashscope.aliyuncs.com", "/compatible-mode/v1/chat/completions",
                "qwen-vl-plus", 1024, 0.7f, 0.9f
            }
        };

        for (const auto& [name, apiKey, baseUrl, path, model, maxTokens, temperature, topP] : defaults) {
            // 检查该配置是否已存在
            Statement checkStmt(m_db, "SELECT COUNT(*) FROM llm_config WHERE name = ?");
            checkStmt.bind(1, name);
            if (checkStmt.step() && checkStmt.getInt(0) > 0) {
                continue; // 已存在，跳过
            }

            Statement stmt(
                m_db,
                "INSERT INTO llm_config (name, api_key, base_url, path, model, max_tokens, temperature, top_p) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
            stmt.bind(1, name);
            stmt.bind(2, apiKey);
            stmt.bind(3, baseUrl);
            stmt.bind(4, path);
            stmt.bind(5, model);
            stmt.bind(6, maxTokens);
            stmt.bind(7, temperature);
            stmt.bind(8, topP);
            stmt.exec();
            spdlog::info("已初始化默认 LLM 配置: {}", name);
        }
    }

    void Database::initDefaultKBConfig() const{
        if (Statement checkStmt(m_db, "SELECT COUNT(*) FROM kb_config");
            checkStmt.step() && checkStmt.getInt(0) > 0) {
            return;
        }

        Statement stmt(
            m_db,
            "INSERT INTO kb_config (id, api_key, base_url, knowledge_dataset_id, memory_dataset_id) VALUES (1, '', '', '', '')");
        stmt.exec();
        spdlog::info("已初始化默认知识库配置");
    }

    void Database::initDefaultMemoryConfig() const{
        if (Statement checkStmt(m_db, "SELECT COUNT(*) FROM memory_config");
            checkStmt.step() && checkStmt.getInt(0) > 0) {
            return;
        }

        Statement stmt(
            m_db,
            "INSERT INTO memory_config (id, memory_trigger_count, memory_chat_record_limit, short_term_memory_max, short_term_memory_limit, memory_migrate_count) VALUES (1, 16, 18, 15, 20, 5)");
        stmt.exec();
        spdlog::info("已初始化默认记忆配置");
    }

    void Database::initDefaultQQConfig() const{
        if (Statement checkStmt(m_db, "SELECT COUNT(*) FROM qq_config");
            checkStmt.step() && checkStmt.getInt(0) > 0) {
            return;
        }

        Statement stmt(
            m_db,
            "INSERT INTO qq_config (id, access_token, self_qq_number, qq_http_host, bot_name) VALUES (1, '', 0, 'http://127.0.0.1:3000', '小喵')");
        stmt.exec();
        spdlog::info("已初始化默认 QQ Bot 配置");
    }
} // namespace LittleMeowBot