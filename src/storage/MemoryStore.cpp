/// @file MemoryStore.cpp
/// @brief 短期记忆存储 - 实现
/// @author donghao
/// @date 2026-08-30

#include <storage/MemoryStore.hpp>
#include <storage/Database.hpp>
#include <storage/Statement.hpp>

namespace insoulforge {
    MemoryStore &MemoryStore::instance() {
        static MemoryStore store;
        return store;
    }

    std::string MemoryStore::getShortTermMemory(const uint64_t sessionId) const {
        auto &db = Database::instance();
        std::shared_lock lock(db.mutex());
        Statement stmt(db.handle(), "SELECT memory_content FROM short_term_memory WHERE group_id = ?");
        stmt.bind(1, sessionId);
        return stmt.step() ? stmt.getText(0) : "";
    }

    // upsert 而非 REPLACE：手动编辑记忆(后台)时不能重置水位线
    void MemoryStore::updateShortTermMemory(const uint64_t sessionId, const std::string &memory) const {
        auto &db = Database::instance();
        std::unique_lock lock(db.mutex());
        Statement stmt(
                    db.handle(),
                    "INSERT INTO short_term_memory (group_id, memory_content) VALUES (?, ?) "
                    "ON CONFLICT(group_id) DO UPDATE SET memory_content = excluded.memory_content, updated_at = CURRENT_TIMESTAMP");
        stmt.bind(1, sessionId);
        stmt.bind(2, memory);
        stmt.exec();
    }

    uint64_t MemoryStore::getMemoryWatermark(const uint64_t sessionId) const {
        auto &db = Database::instance();
        std::shared_lock lock(db.mutex());
        Statement stmt(db.handle(), "SELECT watermark_id FROM short_term_memory WHERE group_id = ?");
        stmt.bind(1, sessionId);
        return stmt.step() ? static_cast<uint64_t>(stmt.getInt64(0)) : 0;
    }

    void MemoryStore::updateShortTermMemoryWithWatermark(
        const uint64_t sessionId, const std::string &memory, const uint64_t watermarkId) const {
        auto &db = Database::instance();
        std::unique_lock lock(db.mutex());
        // 单条 upsert 语句天然原子：记忆与水位线要么一起生效要么都不生效
        Statement stmt(
                    db.handle(),
                    "INSERT INTO short_term_memory (group_id, memory_content, watermark_id) VALUES (?, ?, ?) "
                    "ON CONFLICT(group_id) DO UPDATE SET memory_content = excluded.memory_content, "
                    "watermark_id = excluded.watermark_id, updated_at = CURRENT_TIMESTAMP");
        stmt.bind(1, sessionId);
        stmt.bind(2, memory);
        stmt.bind(3, watermarkId);
        stmt.exec();
    }
} // namespace insoulforge