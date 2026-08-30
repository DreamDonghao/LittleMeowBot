/// @file ChatRecordStore.cpp
/// @brief 聊天记录存储 - 实现
/// @author donghao
/// @date 2026-08-30

#include <storage/ChatRecordStore.hpp>
#include <storage/Database.hpp>
#include <storage/Statement.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <ranges>

namespace insoulforge {
    ChatRecordStore &ChatRecordStore::instance() {
        static ChatRecordStore store;
        return store;
    }

    void ChatRecordStore::addChatRecord(const uint64_t sessionId, const std::string &role,
                                        const std::string &content) const {
        auto &db = Database::instance();
        std::unique_lock lock(db.mutex());
        Statement stmt(db.handle(), "INSERT INTO chat_records (group_id, role, content) VALUES (?, ?, ?)");
        stmt.bind(1, sessionId);
        stmt.bind(2, role);
        stmt.bind(3, content);
        stmt.exec();
    }

    std::vector<Json::Value> ChatRecordStore::getChatRecords(const uint64_t sessionId, const int limit) const {
        auto &db = Database::instance();
        std::shared_lock lock(db.mutex());
        std::vector<Json::Value> records;

        Statement stmt(db.handle(), "SELECT role, content FROM chat_records WHERE group_id = ? ORDER BY id DESC LIMIT ?");
        stmt.bind(1, sessionId);
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

    std::vector<Json::Value> ChatRecordStore::getChatRecordsWithIds(const uint64_t sessionId, const int limit) const {
        auto &db = Database::instance();
        std::shared_lock lock(db.mutex());
        std::vector<Json::Value> records;

        Statement stmt(db.handle(),
                       "SELECT id, role, content FROM chat_records WHERE group_id = ? ORDER BY id DESC LIMIT ?");
        stmt.bind(1, sessionId);
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

    std::vector<Json::Value> ChatRecordStore::getChatRecordsSince(
        const uint64_t sessionId, const uint64_t watermarkId, const int limit) const {
        auto &db = Database::instance();
        std::shared_lock lock(db.mutex());
        std::vector<Json::Value> records;

        Statement stmt(
                    db.handle(),
                    "SELECT id, role, content FROM chat_records WHERE group_id = ? AND id > ? ORDER BY id DESC LIMIT ?");
        stmt.bind(1, sessionId);
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
        std::ranges::reverse(records);
        return records;
    }

    size_t ChatRecordStore::getChatRecordCountSince(const uint64_t sessionId, const uint64_t watermarkId) const {
        auto &db = Database::instance();
        std::shared_lock lock(db.mutex());
        Statement stmt(db.handle(), "SELECT COUNT(*) FROM chat_records WHERE group_id = ? AND id > ?");
        stmt.bind(1, sessionId);
        stmt.bind(2, watermarkId);
        return stmt.step() ? stmt.getInt64(0) : 0;
    }

    void ChatRecordStore::updateChatRecord(const int recordId, const std::string &content) const {
        auto &db = Database::instance();
        std::unique_lock lock(db.mutex());
        Statement stmt(db.handle(), "UPDATE chat_records SET content = ? WHERE id = ?");
        stmt.bind(1, content);
        stmt.bind(2, recordId);
        stmt.exec();
    }

    void ChatRecordStore::deleteChatRecord(const int recordId) const {
        auto &db = Database::instance();
        std::unique_lock lock(db.mutex());
        Statement stmt(db.handle(), "DELETE FROM chat_records WHERE id = ?");
        stmt.bind(1, recordId);
        stmt.exec();
    }

    void ChatRecordStore::clearSessionChatRecords(const uint64_t sessionId) const {
        auto &db = Database::instance();
        std::unique_lock lock(db.mutex());
        Statement stmt(db.handle(), "DELETE FROM chat_records WHERE group_id = ?");
        stmt.bind(1, sessionId);
        stmt.exec();
        spdlog::info("已清空群 {} 的聊天记录", sessionId);
    }
} // namespace insoulforge