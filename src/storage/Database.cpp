/// @file Database.cpp
/// @brief SQLite 数据库管理 - 实现
/// @author donghao
/// @date 2026-04-02

#include <storage/Database.hpp>
#include <spdlog/spdlog.h>
#include <fmt/core.h>
#include <array>
#include <algorithm>
#include <concepts>
#include <filesystem>
#include <utility>
namespace LittleMeowBot {
    namespace {
        /// @brief 数据库错误异常
        class DbError : public std::runtime_error{
        public:
            explicit DbError(std::string_view msg) : std::runtime_error(std::string(msg)){}
        };

        /// @brief SQLite Statement RAII 封装，自动管理 sqlite3_stmt 生命周期
        class Statement{
        public:
            Statement(sqlite3* db, std::string_view sql) : m_db(db){
                if (sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &m_stmt, nullptr) != SQLITE_OK) {
                    std::string err = sqlite3_errmsg(db);
                    spdlog::error("SQL 准备失败: {} - {}", sql, err);
                    throw DbError(err);
                }
            }

            ~Statement(){
                if (m_stmt) sqlite3_finalize(m_stmt);
            }

            Statement(const Statement&) = delete;
            Statement& operator=(const Statement&) = delete;

            Statement(Statement&& other) noexcept
                : m_db(std::exchange(other.m_db, nullptr))
                  , m_stmt(std::exchange(other.m_stmt, nullptr)){}

            Statement& operator=(Statement&& other) noexcept{
                if (this != &other) {
                    if (m_stmt) sqlite3_finalize(m_stmt);
                    m_db = std::exchange(other.m_db, nullptr);
                    m_stmt = std::exchange(other.m_stmt, nullptr);
                }
                return *this;
            }

            void bind(int idx, std::integral auto v) noexcept{
                sqlite3_bind_int64(m_stmt, idx, static_cast<int64_t>(v));
            }

            void bind(int idx, std::floating_point auto v) noexcept{
                sqlite3_bind_double(m_stmt, idx, static_cast<double>(v));
            }

            void bind(const int idx, const std::string& v) const noexcept{
                sqlite3_bind_text(m_stmt, idx, v.c_str(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
            }

            void bind(const int idx, const std::string_view v) const noexcept{
                sqlite3_bind_text(m_stmt, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
            }

            void bind(const int idx, const char* v) const noexcept{
                sqlite3_bind_text(m_stmt, idx, v, -1, SQLITE_TRANSIENT);
            }

            void bind(const int idx, const std::vector<uint8_t>& data) const noexcept{
                if (data.empty()) {
                    sqlite3_bind_null(m_stmt, idx);
                } else {
                    sqlite3_bind_blob(m_stmt, idx, data.data(), static_cast<int>(data.size()), SQLITE_TRANSIENT);
                }
            }

            void bindNull(const int idx) const noexcept{
                sqlite3_bind_null(m_stmt, idx);
            }

            [[nodiscard]] bool step() noexcept{
                int rc = sqlite3_step(m_stmt);
                if (rc == SQLITE_ROW) return true;
                if (rc == SQLITE_DONE) return false;
                spdlog::error("SQL 执行失败: {}", sqlite3_errmsg(m_db));
                return false;
            }

            void exec() noexcept{
                sqlite3_step(m_stmt);
            }

            void reset() noexcept{
                sqlite3_reset(m_stmt);
                sqlite3_clear_bindings(m_stmt);
            }

            [[nodiscard]] int64_t getInt64(int col) const noexcept{
                return sqlite3_column_int64(m_stmt, col);
            }

            [[nodiscard]] int getInt(int col) const noexcept{
                return sqlite3_column_int(m_stmt, col);
            }

            [[nodiscard]] double getDouble(int col) const noexcept{
                return sqlite3_column_double(m_stmt, col);
            }

            [[nodiscard]] std::string getText(int col) const noexcept{
                const auto* p = sqlite3_column_text(m_stmt, col);
                return p ? reinterpret_cast<const char*>(p) : "";
            }

            [[nodiscard]] bool isNull(int col) const noexcept{
                return sqlite3_column_type(m_stmt, col) == SQLITE_NULL;
            }

            [[nodiscard]] std::vector<uint8_t> getBlob(int col) const noexcept{
                const auto* p = static_cast<const uint8_t*>(sqlite3_column_blob(m_stmt, col));
                int size = sqlite3_column_bytes(m_stmt, col);
                if (!p || size <= 0) return {};
                return {p, p + size};
            }

            [[nodiscard]] static int64_t lastInsertRowId(sqlite3* db) noexcept{
                return sqlite3_last_insert_rowid(db);
            }

            [[nodiscard]] static int changes(sqlite3* db) noexcept{
                return sqlite3_changes(db);
            }

        private:
            sqlite3* m_db;
            sqlite3_stmt* m_stmt = nullptr;
        };
    }

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
        const std::filesystem::path p(dbPath);
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

    std::vector<Json::Value> Database::getChatRecordsSince(
        uint64_t groupId, uint64_t watermarkId, int limit) const{
        std::shared_lock lock(m_mutex);
        std::vector<Json::Value> records;

        Statement stmt(
            m_db,
            "SELECT id, role, content FROM chat_records WHERE group_id = ? AND id > ? ORDER BY id DESC LIMIT ?");
        stmt.bind(1, groupId);
        stmt.bind(2, watermarkId);
        stmt.bind(3, limit <= 0 ? INT64_MAX : static_cast<int64_t>(limit));

        while (stmt.step()) {
            Json::Value record;
            record["id"] = static_cast<Json::UInt64>(stmt.getInt64(0));
            record["role"] = stmt.getText(1);
            record["content"] = stmt.getText(2);
            records.push_back(std::move(record));
        }

        // DESC 查询结果反转为旧→新
        std::reverse(records.begin(), records.end());
        return records;
    }

    size_t Database::getChatRecordCountSince(uint64_t groupId, uint64_t watermarkId) const{
        std::shared_lock lock(m_mutex);
        Statement stmt(m_db, "SELECT COUNT(*) FROM chat_records WHERE group_id = ? AND id > ?");
        stmt.bind(1, groupId);
        stmt.bind(2, watermarkId);
        return stmt.step() ? stmt.getInt64(0) : 0;
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

    std::string Database::getShortTermMemory(uint64_t groupId){
        std::shared_lock lock(m_mutex);
        Statement stmt(m_db, "SELECT memory_content FROM short_term_memory WHERE group_id = ?");
        stmt.bind(1, groupId);
        return stmt.step() ? stmt.getText(0) : "";
    }

    // upsert 而非 REPLACE：手动编辑记忆(后台)时不能重置水位线
    void Database::updateShortTermMemory(uint64_t groupId, const std::string& memory) const{
        std::unique_lock lock(m_mutex);
        Statement stmt(
            m_db,
            "INSERT INTO short_term_memory (group_id, memory_content) VALUES (?, ?) "
            "ON CONFLICT(group_id) DO UPDATE SET memory_content = excluded.memory_content, updated_at = CURRENT_TIMESTAMP");
        stmt.bind(1, groupId);
        stmt.bind(2, memory);
        stmt.exec();
    }

    uint64_t Database::getMemoryWatermark(uint64_t groupId){
        std::shared_lock lock(m_mutex);
        Statement stmt(m_db, "SELECT watermark_id FROM short_term_memory WHERE group_id = ?");
        stmt.bind(1, groupId);
        return stmt.step() ? static_cast<uint64_t>(stmt.getInt64(0)) : 0;
    }

    void Database::updateShortTermMemoryWithWatermark(
        uint64_t groupId, const std::string& memory, uint64_t watermarkId) const{
        std::unique_lock lock(m_mutex);
        // 单条 upsert 语句天然原子：记忆与水位线要么一起生效要么都不生效
        Statement stmt(
            m_db,
            "INSERT INTO short_term_memory (group_id, memory_content, watermark_id) VALUES (?, ?, ?) "
            "ON CONFLICT(group_id) DO UPDATE SET memory_content = excluded.memory_content, "
            "watermark_id = excluded.watermark_id, updated_at = CURRENT_TIMESTAMP");
        stmt.bind(1, groupId);
        stmt.bind(2, memory);
        stmt.bind(3, watermarkId);
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


    // ============================================================
    //                      LLM 配置操作
    // ============================================================

    Json::Value Database::getLLMConfig(const std::string& name) const{
        std::shared_lock lock(m_mutex);
        Json::Value config;

        Statement stmt(
            m_db,
            "SELECT api_key, base_url, path, model, max_tokens, temperature, top_p, reasoning_effort FROM llm_config WHERE name = ?");
        stmt.bind(1, name);

        if (stmt.step()) {
            config["apiKey"] = stmt.getText(0);
            config["baseUrl"] = stmt.getText(1);
            config["path"] = stmt.getText(2);
            config["model"] = stmt.getText(3);
            config["maxTokens"] = stmt.getInt(4);
            config["temperature"] = stmt.getDouble(5);
            config["topP"] = stmt.getDouble(6);
            config["reasoningEffort"] = stmt.getText(7);
        }
        return config;
    }

    void Database::saveLLMConfig(const std::string& name, const Json::Value& config) const{
        std::unique_lock lock(m_mutex);

        Statement stmt(
            m_db,
            "INSERT OR REPLACE INTO llm_config (name, api_key, base_url, path, model, max_tokens, temperature, top_p, reasoning_effort) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
        stmt.bind(1, name);
        stmt.bind(2, config["apiKey"].asString());
        stmt.bind(3, config["baseUrl"].asString());
        stmt.bind(4, config["path"].asString());
        stmt.bind(5, config["model"].asString());
        stmt.bind(6, config["maxTokens"].asInt());
        stmt.bind(7, config["temperature"].asFloat());
        stmt.bind(8, config["topP"].asFloat());
        stmt.bind(9, config.get("reasoningEffort", "").asString());
        stmt.exec();
    }

    Json::Value Database::getAllLLMConfigs(){
        std::shared_lock lock(m_mutex);
        Json::Value configs;

        Statement stmt(
            m_db, "SELECT name, api_key, base_url, path, model, max_tokens, temperature, top_p, reasoning_effort FROM llm_config");
        while (stmt.step()) {
            Json::Value cfg;
            cfg["apiKey"] = stmt.getText(1);
            cfg["baseUrl"] = stmt.getText(2);
            cfg["path"] = stmt.getText(3);
            cfg["model"] = stmt.getText(4);
            cfg["maxTokens"] = stmt.getInt(5);
            cfg["temperature"] = stmt.getDouble(6);
            cfg["topP"] = stmt.getDouble(7);
            cfg["reasoningEffort"] = stmt.getText(8);
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
            "SELECT enabled, api_key, base_url, knowledge_dataset_id, memory_dataset_id, memory_document_id FROM kb_config WHERE id = 1");
        if (stmt.step()) {
            config["enabled"] = stmt.getInt(0) != 0;
            config["apiKey"] = stmt.getText(1);
            config["baseUrl"] = stmt.getText(2);
            config["knowledgeDatasetId"] = stmt.getText(3);
            config["memoryDatasetId"] = stmt.getText(4);
            config["memoryDocumentId"] = stmt.getText(5);
        }
        return config;
    }

    void Database::saveKBConfig(const Json::Value& config) const{
        std::unique_lock lock(m_mutex);

        Statement stmt(
            m_db,
            "INSERT OR REPLACE INTO kb_config (id, enabled, api_key, base_url, knowledge_dataset_id, memory_dataset_id, memory_document_id) VALUES (1, ?, ?, ?, ?, ?, ?)");
        stmt.bind(1, config.get("enabled", true).asBool() ? 1 : 0);
        stmt.bind(2, config["apiKey"].asString());
        stmt.bind(3, config["baseUrl"].asString());
        stmt.bind(4, config["knowledgeDatasetId"].asString());
        stmt.bind(5, config["memoryDatasetId"].asString());
        stmt.bind(6, config.isMember("memoryDocumentId") ? config["memoryDocumentId"].asString() : "");
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
            "SELECT window_trigger_count, window_keep_count, memory_extract_max_tokens, router_window_trigger_count, router_window_keep_count, short_term_memory_max, memory_migrate_count FROM memory_config WHERE id = 1");
        if (stmt.step()) {
            config["windowTriggerCount"] = stmt.getInt(0);
            config["windowKeepCount"] = stmt.getInt(1);
            config["memoryExtractMaxTokens"] = stmt.getInt(2);
            config["routerWindowTriggerCount"] = stmt.getInt(3);
            config["routerWindowKeepCount"] = stmt.getInt(4);
            config["shortTermMemoryMax"] = stmt.getInt(5);
            config["memoryMigrateCount"] = stmt.getInt(6);
        }
        return config;
    }

    void Database::saveMemoryConfig(const Json::Value& config) const{
        std::unique_lock lock(m_mutex);

        Statement stmt(
            m_db,
            "INSERT OR REPLACE INTO memory_config (id, window_trigger_count, window_keep_count, memory_extract_max_tokens, router_window_trigger_count, router_window_keep_count, short_term_memory_max, memory_migrate_count) VALUES (1, ?, ?, ?, ?, ?, ?, ?)");
        stmt.bind(1, config["windowTriggerCount"].asInt());
        stmt.bind(2, config["windowKeepCount"].asInt());
        stmt.bind(3, config["memoryExtractMaxTokens"].asInt());
        stmt.bind(4, config["routerWindowTriggerCount"].asInt());
        stmt.bind(5, config["routerWindowKeepCount"].asInt());
        stmt.bind(6, config["shortTermMemoryMax"].asInt());
        stmt.bind(7, config["memoryMigrateCount"].asInt());
        stmt.exec();
        spdlog::info("记忆配置已保存");
    }

    bool Database::hasMemoryConfig() const{
        std::shared_lock lock(m_mutex);
        Statement stmt(m_db, "SELECT 1 FROM memory_config WHERE id = 1");
        return stmt.step();
    }

    // ============================================================
    //                      用量统计操作
    // ============================================================

    void Database::addUsageRecord(const std::string& model, const int promptTokens,
                                  const int completionTokens, const int totalTokens,
                                  const int cachedTokens) const{
        std::unique_lock lock(m_mutex);
        Statement stmt(m_db,
            "INSERT INTO llm_usage (model, prompt_tokens, completion_tokens, total_tokens, cached_tokens) "
            "VALUES (?, ?, ?, ?, ?)");
        stmt.bind(1, model);
        stmt.bind(2, promptTokens);
        stmt.bind(3, completionTokens);
        stmt.bind(4, totalTokens);
        stmt.bind(5, cachedTokens);
        stmt.exec();
    }

    Json::Value Database::getUsageSummary(const int days) const{
        std::shared_lock lock(m_mutex);
        Json::Value result;

        // 汇总
        {
            Statement stmt(m_db,
                "SELECT COUNT(*), COALESCE(SUM(prompt_tokens),0), COALESCE(SUM(completion_tokens),0), "
                "COALESCE(SUM(total_tokens),0), COALESCE(SUM(cached_tokens),0) "
                "FROM llm_usage WHERE created_at >= datetime('now', ?)");
            stmt.bind(1, fmt::format("-{} days", days));
            if (stmt.step()) {
                result["total_calls"] = stmt.getInt(0);
                result["total_prompt"] = stmt.getInt64(1);
                result["total_completion"] = stmt.getInt64(2);
                result["total_tokens"] = stmt.getInt64(3);
                result["total_cached"] = stmt.getInt64(4);
            }
        }

        // 按模型
        {
            Json::Value byModel(Json::arrayValue);
            Statement stmt(m_db,
                "SELECT model, COUNT(*), COALESCE(SUM(prompt_tokens),0), COALESCE(SUM(completion_tokens),0), "
                "COALESCE(SUM(total_tokens),0), COALESCE(SUM(cached_tokens),0) "
                "FROM llm_usage WHERE created_at >= datetime('now', ?) "
                "GROUP BY model ORDER BY SUM(total_tokens) DESC");
            stmt.bind(1, fmt::format("-{} days", days));
            while (stmt.step()) {
                Json::Value item;
                item["model"] = stmt.getText(0);
                item["calls"] = stmt.getInt(1);
                item["prompt"] = stmt.getInt64(2);
                item["completion"] = stmt.getInt64(3);
                item["total"] = stmt.getInt64(4);
                item["cached"] = stmt.getInt64(5);
                byModel.append(item);
            }
            result["by_model"] = byModel;
        }

        // 按天
        {
            Json::Value byDay(Json::arrayValue);
            Statement stmt(m_db,
                "SELECT date(created_at) AS day, COUNT(*), COALESCE(SUM(total_tokens),0) "
                "FROM llm_usage WHERE created_at >= datetime('now', ?) "
                "GROUP BY day ORDER BY day");
            stmt.bind(1, fmt::format("-{} days", days));
            while (stmt.step()) {
                Json::Value item;
                item["day"] = stmt.getText(0);
                item["calls"] = stmt.getInt(1);
                item["total"] = stmt.getInt64(2);
                byDay.append(item);
            }
            result["by_day"] = byDay;
        }

        return result;
    }

    Json::Value Database::getRecentUsage(const int limit) const{
        std::shared_lock lock(m_mutex);
        Json::Value result(Json::arrayValue);
        Statement stmt(m_db,
            "SELECT created_at, model, prompt_tokens, completion_tokens, total_tokens, cached_tokens "
            "FROM llm_usage ORDER BY id DESC LIMIT ?");
        stmt.bind(1, limit);
        while (stmt.step()) {
            Json::Value item;
            item["time"] = stmt.getText(0);
            item["model"] = stmt.getText(1);
            item["prompt"] = stmt.getInt(2);
            item["completion"] = stmt.getInt(3);
            item["total"] = stmt.getInt(4);
            item["cached"] = stmt.getInt(5);
            result.append(item);
        }
        return result;
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
            R"(CREATE TABLE IF NOT EXISTS short_term_memory (
        group_id INTEGER PRIMARY KEY,
        memory_content TEXT,
        watermark_id INTEGER DEFAULT 0,
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
            R"(CREATE TABLE IF NOT EXISTS llm_config (
        name TEXT PRIMARY KEY,
        api_key TEXT,
        base_url TEXT,
        path TEXT,
        model TEXT,
        max_tokens INTEGER DEFAULT 1024,
        temperature REAL DEFAULT 0.7,
        top_p REAL DEFAULT 0.9,
        reasoning_effort TEXT DEFAULT ''
    ))",
            R"(CREATE TABLE IF NOT EXISTS kb_config (
        id INTEGER PRIMARY KEY CHECK (id = 1),
        enabled INTEGER DEFAULT 1,
        api_key TEXT,
        base_url TEXT,
        knowledge_dataset_id TEXT,
        memory_dataset_id TEXT,
        memory_document_id TEXT
    ))",
            R"(CREATE TABLE IF NOT EXISTS memory_config (
        id INTEGER PRIMARY KEY CHECK (id = 1),
        window_trigger_count INTEGER DEFAULT 100,
        window_keep_count INTEGER DEFAULT 50,
        memory_extract_max_tokens INTEGER DEFAULT 4000,
        router_window_trigger_count INTEGER DEFAULT 20,
        router_window_keep_count INTEGER DEFAULT 10,
        short_term_memory_max INTEGER DEFAULT 15,
        memory_migrate_count INTEGER DEFAULT 5
    ))",
            R"(CREATE TABLE IF NOT EXISTS qq_config (
        id INTEGER PRIMARY KEY CHECK (id = 1),
        access_token TEXT,
        self_qq_number INTEGER,
        qq_http_host TEXT,
        bot_name TEXT DEFAULT '小喵'
    ))",
            R"(CREATE TABLE IF NOT EXISTS llm_usage (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        model TEXT NOT NULL,
        prompt_tokens INTEGER DEFAULT 0,
        completion_tokens INTEGER DEFAULT 0,
        total_tokens INTEGER DEFAULT 0,
        cached_tokens INTEGER DEFAULT 0,
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
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
        bool kbHasEnabled = false;
        bool llmHasReasoning = false;
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
        auto callbackKBEnabled = [](void* data, int argc, char** argv, char** colNames) -> int {
            for (int i = 0; i < argc; i++) {
                if (argv[i] && std::string(argv[i]) == "enabled") {
                    *static_cast<bool*>(data) = true;
                    break;
                }
            }
            return 0;
        };
        auto callbackReasoning = [](void* data, int argc, char** argv, char** colNames) -> int {
            for (int i = 0; i < argc; i++) {
                if (argv[i] && std::string(argv[i]) == "reasoning_effort") {
                    *static_cast<bool*>(data) = true;
                    break;
                }
            }
            return 0;
        };
        sqlite3_exec(m_db, "PRAGMA table_info(custom_tools)", callback, &hasScriptContent, nullptr);
        sqlite3_exec(m_db, "PRAGMA table_info(custom_tools)", callbackReadme, &hasReadme, nullptr);
        sqlite3_exec(m_db, "PRAGMA table_info(kb_config)", callbackKBEnabled, &kbHasEnabled, nullptr);
        sqlite3_exec(m_db, "PRAGMA table_info(llm_config)", callbackReasoning, &llmHasReasoning, nullptr);

        if (!hasScriptContent) {
            spdlog::info("数据库迁移: 添加 script_content 列");
            sqlite3_exec(m_db, "ALTER TABLE custom_tools ADD COLUMN script_content TEXT", nullptr, nullptr, nullptr);
        }
        if (!hasReadme) {
            spdlog::info("数据库迁移: 添加 readme 列");
            sqlite3_exec(m_db, "ALTER TABLE custom_tools ADD COLUMN readme TEXT", nullptr, nullptr, nullptr);
        }
        if (!kbHasEnabled) {
            spdlog::info("数据库迁移: 添加 kb_config.enabled 列");
            sqlite3_exec(m_db, "ALTER TABLE kb_config ADD COLUMN enabled INTEGER DEFAULT 1", nullptr, nullptr, nullptr);
        }
        // 数据库迁移: long_term_memory → short_term_memory
        // 注：createTables() 可能已创建空的 short_term_memory，需先检查旧表是否有数据
        {
            bool hasOldTable = false;
            auto checkExists = [](void* data, int argc, char** argv, char**) -> int {
                if (argc > 0 && argv[0]) *static_cast<bool*>(data) = true;
                return 0;
            };
            sqlite3_exec(m_db,
                "SELECT name FROM sqlite_master WHERE type='table' AND name='long_term_memory'",
                checkExists, &hasOldTable, nullptr);

            if (hasOldTable) {
                // 检查旧表是否有数据
                int oldCount = 0;
                auto countCb = [](void* data, int argc, char** argv, char**) -> int {
                    if (argc > 0 && argv[0]) *static_cast<int*>(data) = std::stoi(argv[0]);
                    return 0;
                };
                sqlite3_exec(m_db, "SELECT COUNT(*) FROM long_term_memory",
                    countCb, &oldCount, nullptr);

                if (oldCount > 0) {
                    bool hasNewTable = false;
                    sqlite3_exec(m_db,
                        "SELECT name FROM sqlite_master WHERE type='table' AND name='short_term_memory'",
                        checkExists, &hasNewTable, nullptr);

                    if (hasNewTable) {
                        // createTables() 创建了空表，先删掉再重命名旧表
                        sqlite3_exec(m_db, "DROP TABLE short_term_memory",
                            nullptr, nullptr, nullptr);
                    }
                    spdlog::info("数据库迁移: 重命名 long_term_memory → short_term_memory ({} 条数据)", oldCount);
                    sqlite3_exec(m_db, "ALTER TABLE long_term_memory RENAME TO short_term_memory",
                        nullptr, nullptr, nullptr);
                } else {
                    // 旧表为空，直接删掉即可
                    spdlog::info("数据库迁移: 删除空的 long_term_memory 表");
                    sqlite3_exec(m_db, "DROP TABLE long_term_memory",
                        nullptr, nullptr, nullptr);
                }
            }
        }

        // 数据库迁移: 移除废弃的 short_term_memory_limit 列
        {
            bool hasColumn = false;
            auto checkCol = [](void* data, int argc, char** argv, char**) -> int {
                for (int i = 0; i < argc; i++) {
                    if (argv[i] && std::string(argv[i]) == "short_term_memory_limit") {
                        *static_cast<bool*>(data) = true;
                        break;
                    }
                }
                return 0;
            };
            sqlite3_exec(m_db, "PRAGMA table_info(memory_config)", checkCol, &hasColumn, nullptr);
            if (hasColumn) {
                spdlog::info("数据库迁移: 移除废弃的 short_term_memory_limit 列");
                sqlite3_exec(m_db, "ALTER TABLE memory_config DROP COLUMN short_term_memory_limit",
                    nullptr, nullptr, nullptr);
            }
        }

        // 数据库迁移: 上下文窗口配置（窗口触发条数/保留条数/提取 maxTokens/记忆水位线）
        {
            struct ColCheck{
                const std::string& name;
                bool found = false;
            };

            auto checkCol = [](void* data, int argc, char** argv, char**) -> int {
                auto* state = static_cast<ColCheck*>(data);
                for (int i = 0; i < argc; i++) {
                    if (argv[i] && state->name == argv[i]) {
                        state->found = true;
                        break;
                    }
                }
                return 0;
            };

            auto ensureColumn = [&](const std::string& table, const std::string& colName,
                                    const std::string& colDefault) {
                ColCheck state{colName, false};
                sqlite3_exec(m_db, fmt::format("PRAGMA table_info({})", table).c_str(), checkCol, &state, nullptr);
                if (!state.found) {
                    spdlog::info("数据库迁移: 新增 {}.{} 列", table, colName);
                    sqlite3_exec(m_db, fmt::format("ALTER TABLE {} ADD COLUMN {} INTEGER DEFAULT {}",
                                                   table, colName, colDefault).c_str(),
                        nullptr, nullptr, nullptr);
                }
            };
            auto dropColumn = [&](const std::string& table, const std::string& colName) {
                ColCheck state{colName, false};
                sqlite3_exec(m_db, fmt::format("PRAGMA table_info({})", table).c_str(), checkCol, &state, nullptr);
                if (state.found) {
                    spdlog::info("数据库迁移: 移除废弃的 {}.{} 列", table, colName);
                    sqlite3_exec(m_db, fmt::format("ALTER TABLE {} DROP COLUMN {}", table, colName).c_str(),
                        nullptr, nullptr, nullptr);
                }
            };

            ensureColumn("memory_config", "window_trigger_count", "100");
            ensureColumn("memory_config", "window_keep_count", "50");
            ensureColumn("memory_config", "memory_extract_max_tokens", "4000");
            ensureColumn("memory_config", "router_window_trigger_count", "20");
            ensureColumn("memory_config", "router_window_keep_count", "10");
            ensureColumn("short_term_memory", "watermark_id", "0");
            dropColumn("memory_config", "memory_trigger_count");
            dropColumn("memory_config", "memory_chat_record_limit");
            dropColumn("memory_config", "executor_context_limit");

            // 老群已有短期记忆：水位线初始化为该群最新记录 id，
            // 避免升级后首次触发时把全部历史重新提取一遍（历史已浓缩在现有记忆中）
            sqlite3_exec(m_db,
                "UPDATE short_term_memory SET watermark_id = "
                "(SELECT COALESCE(MAX(id), 0) FROM chat_records c WHERE c.group_id = short_term_memory.group_id) "
                "WHERE watermark_id = 0 "
                "AND EXISTS (SELECT 1 FROM chat_records c WHERE c.group_id = short_term_memory.group_id)",
                nullptr, nullptr, nullptr);
            if (const int backfilled = sqlite3_changes(m_db); backfilled > 0) {
                spdlog::info("数据库迁移: 回填 {} 个群的记忆水位线", backfilled);
            }
        }

        // 数据库迁移: emojis 表已废弃（表情包改为直接使用 QQ 收藏表情，不再存库）
        {
            bool hasEmojisTable = false;
            auto checkEmojiTable = [](void* data, int argc, char** argv, char**) -> int {
                if (argc > 0 && argv[0]) *static_cast<bool*>(data) = true;
                return 0;
            };
            sqlite3_exec(m_db,
                "SELECT name FROM sqlite_master WHERE type='table' AND name='emojis'",
                checkEmojiTable, &hasEmojisTable, nullptr);
            if (hasEmojisTable) {
                spdlog::info("数据库迁移: 删除废弃的 emojis 表（表情包改为直接使用 QQ 收藏表情）");
                sqlite3_exec(m_db, "DROP TABLE emojis", nullptr, nullptr, nullptr);
            }
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
            {"executor", "", "http://127.0.0.1:3001", "/v1/chat/completions", "deepseek-chat", 150, 0.7f, 0.9f},
            {"executorThinking", "", "http://127.0.0.1:3001", "/v1/chat/completions", "deepseek-reasoner", 512, 0.7f, 0.9f},
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
            "INSERT INTO kb_config (id, enabled, api_key, base_url, knowledge_dataset_id, memory_dataset_id) VALUES (1, 1, '', '', '', '')");
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
            "INSERT INTO memory_config (id, window_trigger_count, window_keep_count, memory_extract_max_tokens, router_window_trigger_count, router_window_keep_count, short_term_memory_max, memory_migrate_count) VALUES (1, 100, 50, 4000, 20, 10, 15, 5)");
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