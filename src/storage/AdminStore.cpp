/// @file AdminStore.cpp
/// @brief 管理员存储 - 实现
/// @author donghao
/// @date 2026-08-30

#include <storage/AdminStore.hpp>
#include <storage/Database.hpp>
#include <storage/Statement.hpp>
#include <spdlog/spdlog.h>

namespace insoulforge {
    AdminStore &AdminStore::instance() {
        static AdminStore store;
        return store;
    }

    bool AdminStore::isAdmin(const uint64_t qqNumber) const {
        auto &db = Database::instance();
        std::shared_lock lock(db.mutex());
        Statement stmt(db.handle(), "SELECT 1 FROM admins WHERE qq_number = ?");
        stmt.bind(1, qqNumber);
        return stmt.step();
    }

    void AdminStore::addAdmin(const uint64_t qqNumber) const {
        auto &db = Database::instance();
        std::unique_lock lock(db.mutex());
        Statement stmt(db.handle(), "INSERT OR IGNORE INTO admins (qq_number) VALUES (?)");
        stmt.bind(1, qqNumber);
        stmt.exec();
        spdlog::info("已添加管理员: {}", qqNumber);
    }

    void AdminStore::removeAdmin(const uint64_t qqNumber) const {
        auto &db = Database::instance();
        std::unique_lock lock(db.mutex());
        Statement stmt(db.handle(), "DELETE FROM admins WHERE qq_number = ?");
        stmt.bind(1, qqNumber);
        stmt.exec();
        spdlog::info("已移除管理员: {}", qqNumber);
    }

    std::vector<uint64_t> AdminStore::getAdmins() const {
        auto &db = Database::instance();
        std::shared_lock lock(db.mutex());
        std::vector<uint64_t> admins;
        Statement stmt(db.handle(), "SELECT qq_number FROM admins");
        while (stmt.step()) {
            admins.push_back(stmt.getInt64(0));
        }
        return admins;
    }
} // namespace insoulforge