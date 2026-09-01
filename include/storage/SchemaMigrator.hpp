/// @file SchemaMigrator.hpp
/// @brief 数据库 Schema 版本迁移
/// @author donghao
/// @date 2026-08-30
/// @details 基于 PRAGMA user_version 的版本迁移：
///          - 版本号存于 SQLite 的 user_version
///          - 每个版本迁移是一个有序步骤，启动时从当前版本逐个执行到最新版本
///          - 全新数据库直接创建最新 Schema 并写入最新版本号
///          - 每个迁移步骤在独立事务中执行，失败则回滚并抛出异常

#pragma once
#include <sqlite3.h>

namespace insoulforge {
    /// @brief Schema 版本迁移器
    class SchemaMigrator {
    public:
        /// @brief 执行迁移：检查当前版本并按序应用所有待执行步骤
        /// @throws DbError 任一迁移步骤失败时抛出（已回滚）
        static void migrate(sqlite3 *db);

        /// @brief 当前代码对应的最新 Schema 版本号
        static constexpr int kLatestVersion = 5;

    private:
        SchemaMigrator() = default;

        static void migrateV0ToV1(sqlite3 *db);

        static void migrateV1ToV2(sqlite3 *db);

        static void migrateV2ToV3(sqlite3 *db);

        static void migrateV3ToV4(sqlite3 *db);

        static void migrateV4ToV5(sqlite3 *db);

        static int getUserVersion(sqlite3 *db);

        static void setUserVersion(sqlite3 *db, int version);

        /// @brief 创建全新数据库的最新 Schema 并写入最新版本号
        static void createFreshSchema(sqlite3 *db);

        /// @brief 数据库是否已有用户表（区分全新库与未版本化的旧库）
        static bool hasUserTables(sqlite3 *db);
    };
} // namespace insoulforge