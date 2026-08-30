/// @file TaskStore.cpp
/// @brief 定时任务存储 - 实现
/// @author donghao
/// @date 2026-08-30

#include <storage/TaskStore.hpp>
#include <storage/Database.hpp>
#include <storage/Statement.hpp>

namespace insoulforge {
    TaskStore &TaskStore::instance() {
        static TaskStore store;
        return store;
    }

    int64_t TaskStore::addScheduledTask(const ScheduledTask &task) const {
        auto &db = Database::instance();
        std::unique_lock lock(db.mutex());
        Statement stmt(db.handle(),
                       "INSERT INTO scheduled_tasks (session_type, target_id, remind_time, content) "
                       "VALUES (?, ?, ?, ?)");
        stmt.bind(1, task.sessionType);
        stmt.bind(2, task.targetId);
        stmt.bind(3, task.remindTime);
        stmt.bind(4, task.content);
        stmt.exec();
        return Statement::lastInsertRowId(db.handle());
    }

    std::vector<TaskStore::ScheduledTask> TaskStore::getPendingScheduledTasks() const {
        auto &db = Database::instance();
        std::shared_lock lock(db.mutex());
        std::vector<ScheduledTask> tasks;
        Statement stmt(db.handle(),
                       "SELECT id, session_type, target_id, remind_time, content FROM scheduled_tasks "
                       "WHERE status = 'pending' ORDER BY remind_time ASC");
        while (stmt.step()) {
            ScheduledTask task;
            task.id = stmt.getInt64(0);
            task.sessionType = stmt.getText(1);
            task.targetId = stmt.getInt64(2);
            task.remindTime = stmt.getInt64(3);
            task.content = stmt.getText(4);
            tasks.push_back(std::move(task));
        }
        return tasks;
    }

    void TaskStore::finishScheduledTask(const int64_t id) const {
        auto &db = Database::instance();
        std::unique_lock lock(db.mutex());
        Statement stmt(db.handle(), "UPDATE scheduled_tasks SET status='done' WHERE id=?");
        stmt.bind(1, id);
        stmt.exec();
    }
} // namespace insoulforge