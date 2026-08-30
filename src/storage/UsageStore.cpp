/// @file UsageStore.cpp
/// @brief LLM 用量统计存储 - 实现
/// @author donghao
/// @date 2026-08-30

#include <storage/UsageStore.hpp>
#include <storage/Database.hpp>
#include <storage/Statement.hpp>
#include <fmt/core.h>

namespace insoulforge {
    UsageStore &UsageStore::instance() {
        static UsageStore store;
        return store;
    }

    void UsageStore::addUsageRecord(const std::string &role, const std::string &model,
                                    const int promptTokens, const int completionTokens,
                                    const int totalTokens, const int cachedTokens) const {
        auto &db = Database::instance();
        std::unique_lock lock(db.mutex());
        Statement stmt(db.handle(),
                       "INSERT INTO llm_usage (role, model, prompt_tokens, completion_tokens, total_tokens, cached_tokens) "
                       "VALUES (?, ?, ?, ?, ?, ?)");
        stmt.bind(1, role);
        stmt.bind(2, model);
        stmt.bind(3, promptTokens);
        stmt.bind(4, completionTokens);
        stmt.bind(5, totalTokens);
        stmt.bind(6, cachedTokens);
        stmt.exec();
    }

    Json::Value UsageStore::getUsageSummary(const int days) const {
        auto &db = Database::instance();
        std::shared_lock lock(db.mutex());
        Json::Value result;
        const std::string sinceDate = fmt::format("-{} days", days);

        // 汇总
        {
            Statement stmt(db.handle(),
                           "SELECT COUNT(*), COALESCE(SUM(prompt_tokens),0), COALESCE(SUM(completion_tokens),0), "
                           "COALESCE(SUM(total_tokens),0), COALESCE(SUM(cached_tokens),0) "
                           "FROM llm_usage WHERE created_at >= datetime('now', 'localtime', ?)");
            stmt.bind(1, sinceDate);
            if (stmt.step()) {
                result["total_calls"] = stmt.getInt(0);
                result["total_prompt"] = stmt.getInt64(1);
                result["total_completion"] = stmt.getInt64(2);
                result["total_tokens"] = stmt.getInt64(3);
                result["total_cached"] = stmt.getInt64(4);
            }
        }

        // 今日汇总（按本地日期）
        {
            Statement stmt(db.handle(),
                           "SELECT COUNT(*), COALESCE(SUM(prompt_tokens),0), COALESCE(SUM(completion_tokens),0), "
                           "COALESCE(SUM(total_tokens),0), COALESCE(SUM(cached_tokens),0) "
                           "FROM llm_usage WHERE date(created_at, 'localtime') = date('now', 'localtime')");
            if (stmt.step()) {
                result["today"]["calls"] = stmt.getInt(0);
                result["today"]["prompt"] = stmt.getInt64(1);
                result["today"]["completion"] = stmt.getInt64(2);
                result["today"]["total"] = stmt.getInt64(3);
                result["today"]["cached"] = stmt.getInt64(4);
            }
        }

        // 今日按角色
        {
            Json::Value todayByRole(Json::arrayValue);
            Statement stmt(db.handle(),
                           "SELECT role, MAX(model), COUNT(*), COALESCE(SUM(prompt_tokens),0), "
                           "COALESCE(SUM(completion_tokens),0), COALESCE(SUM(total_tokens),0), COALESCE(SUM(cached_tokens),0) "
                           "FROM llm_usage WHERE date(created_at, 'localtime') = date('now', 'localtime') "
                           "GROUP BY role ORDER BY SUM(total_tokens) DESC");
            while (stmt.step()) {
                Json::Value item;
                item["role"] = stmt.getText(0);
                item["model"] = stmt.getText(1);
                item["calls"] = stmt.getInt(2);
                item["prompt"] = stmt.getInt64(3);
                item["completion"] = stmt.getInt64(4);
                item["total"] = stmt.getInt64(5);
                item["cached"] = stmt.getInt64(6);
                todayByRole.append(item);
            }
            result["today_by_role"] = todayByRole;
        }

        // 按角色
        {
            Json::Value byRole(Json::arrayValue);
            Statement stmt(db.handle(),
                           "SELECT role, MAX(model), COUNT(*), COALESCE(SUM(prompt_tokens),0), "
                           "COALESCE(SUM(completion_tokens),0), COALESCE(SUM(total_tokens),0), COALESCE(SUM(cached_tokens),0) "
                           "FROM llm_usage WHERE created_at >= datetime('now', 'localtime', ?) "
                           "GROUP BY role ORDER BY SUM(total_tokens) DESC");
            stmt.bind(1, sinceDate);
            while (stmt.step()) {
                Json::Value item;
                item["role"] = stmt.getText(0);
                item["model"] = stmt.getText(1);
                item["calls"] = stmt.getInt(2);
                item["prompt"] = stmt.getInt64(3);
                item["completion"] = stmt.getInt64(4);
                item["total"] = stmt.getInt64(5);
                item["cached"] = stmt.getInt64(6);
                byRole.append(item);
            }
            result["by_role"] = byRole;
        }

        // 按天
        {
            Json::Value byDay(Json::arrayValue);
            Statement stmt(db.handle(),
                           "SELECT date(created_at, 'localtime') AS day, COUNT(*), COALESCE(SUM(total_tokens),0) "
                           "FROM llm_usage WHERE created_at >= datetime('now', 'localtime', ?) "
                           "GROUP BY day ORDER BY day");
            stmt.bind(1, sinceDate);
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

    Json::Value UsageStore::getRecentUsage(const int limit) const {
        auto &db = Database::instance();
        std::shared_lock lock(db.mutex());
        Json::Value result(Json::arrayValue);
        Statement stmt(db.handle(),
                       "SELECT datetime(created_at, 'localtime') AS time, role, model, prompt_tokens, completion_tokens, total_tokens, cached_tokens "
                       "FROM llm_usage ORDER BY id DESC LIMIT ?");
        stmt.bind(1, limit);
        while (stmt.step()) {
            Json::Value item;
            item["time"] = stmt.getText(0);
            item["role"] = stmt.getText(1);
            item["model"] = stmt.getText(2);
            item["prompt"] = stmt.getInt(3);
            item["completion"] = stmt.getInt(4);
            item["total"] = stmt.getInt(5);
            item["cached"] = stmt.getInt(6);
            result.append(item);
        }
        return result;
    }
} // namespace insoulforge