/// @file Statement.hpp
/// @brief SQLite Statement RAII 封装
/// @author donghao
/// @date 2026-04-02
/// @details 提供 SQLite 语句的 RAII 封装和事务管理：
///          - Statement: 自动管理 sqlite3_stmt 生命周期
///          - Transaction: 自动管理事务，异常安全
///          - 参数绑定：支持整数、浮点、字符串等类型

#pragma once
#include <sqlite3.h>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <stdexcept>
#include <concepts>

namespace LittleMeowBot {
    /// @brief 数据库错误异常
    class DbError : public std::runtime_error{
    public:
        explicit DbError(std::string_view msg) : std::runtime_error(std::string(msg)){}
    };

    /// @brief SQLite Statement RAII 封装
/// @details 自动管理 sqlite3_stmt 生命周期，防止资源泄漏
    class Statement{
    public:
        /// @brief 构造并准备语句
    /// @param db 数据库连接
    /// @param sql SQL 语句
    /// @throws DbError 准备失败时抛出
        Statement(sqlite3* db, std::string_view sql);

        ~Statement();

        // 禁止拷贝
        Statement(const Statement&) = delete;
        Statement& operator=(const Statement&) = delete;

        // 允许移动
        Statement(Statement&& other) noexcept;
        Statement& operator=(Statement&& other) noexcept;

        // ==================== 绑定参数（使用 Concepts） ====================

        /// @brief 绑定整数类型参数
        void bind(int idx, std::integral auto v) noexcept{
            sqlite3_bind_int64(m_stmt, idx, static_cast<int64_t>(v));
        }

        /// @brief 绑定浮点类型参数
        void bind(int idx, std::floating_point auto v) noexcept{
            sqlite3_bind_double(m_stmt, idx, static_cast<double>(v));
        }

        /// @brief 绑定 std::string 参数
        void bind(const int idx, const std::string& v) const noexcept{
            sqlite3_bind_text(m_stmt, idx, v.c_str(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
        }

        /// @brief 绑定 std::string_view 参数
        void bind(const int idx, const std::string_view v) const noexcept{
            sqlite3_bind_text(m_stmt, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
        }

        /// @brief 绑定 C-string 参数
        void bind(const int idx, const char* v) const noexcept{
            sqlite3_bind_text(m_stmt, idx, v, -1, SQLITE_TRANSIENT);
        }

        /// @brief 绑定 NULL 值
        void bindNull(const int idx) const noexcept{
            sqlite3_bind_null(m_stmt, idx);
        }

        // ==================== 执行 ====================

        /// @brief 执行一步，返回是否有数据行
        /// @return true 表示有数据可读，false 表示执行完毕
        [[nodiscard]] bool step() noexcept;

        /// @brief 执行并忽略结果（用于 INSERT/UPDATE/DELETE）
        void exec() noexcept;

        /// @brief 重置语句，可重新绑定参数执行
        void reset() noexcept;

        // ==================== 获取结果 ====================

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

        // ==================== 工具方法 ====================

        /// @brief 获取最后插入的行ID
        [[nodiscard]] static int64_t lastInsertRowId(sqlite3* db) noexcept{
            return sqlite3_last_insert_rowid(db);
        }

        /// @brief 获取受影响的行数
        [[nodiscard]] static int changes(sqlite3* db) noexcept{
            return sqlite3_changes(db);
        }

    private:
        sqlite3* m_db;
        sqlite3_stmt* m_stmt = nullptr;
    };

    /// @brief 数据库事务 RAII 封装
/// @details 自动管理事务，析构时如果未提交则回滚
    class Transaction{
    public:
        explicit Transaction(sqlite3* db);
        ~Transaction();

        // 禁止拷贝和移动
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;
        Transaction(Transaction&&) = delete;
        Transaction& operator=(Transaction&&) = delete;

        void commit() noexcept;

    private:
        sqlite3* m_db;
        bool m_committed = false;
    };
} // namespace LittleMeowBot