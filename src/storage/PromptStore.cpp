/// @file PromptStore.cpp
/// @brief 提示词存储 - 实现
/// @author donghao
/// @date 2026-08-30

#include <storage/PromptStore.hpp>
#include <storage/Database.hpp>
#include <storage/Statement.hpp>

namespace insoulforge {
    PromptStore &PromptStore::instance() {
        static PromptStore store;
        return store;
    }

    std::string PromptStore::getPrompt(const std::string &key, const std::string &defaultValue) const {
        auto &db = Database::instance();
        std::shared_lock lock(db.mutex());
        Statement stmt(db.handle(), "SELECT prompt_content FROM prompts WHERE prompt_key = ?");
        stmt.bind(1, key);
        return stmt.step() ? stmt.getText(0) : defaultValue;
    }

    void PromptStore::setPrompt(const std::string &key, const std::string &content, const std::string &description) {
        auto &db = Database::instance();
        std::unique_lock lock(db.mutex());
        Statement stmt(
                    db.handle(), "INSERT OR REPLACE INTO prompts (prompt_key, prompt_content, description) VALUES (?, ?, ?)");
        stmt.bind(1, key);
        stmt.bind(2, content);
        stmt.bind(3, description);
        stmt.exec();
    }

    bool PromptStore::hasPrompt(const std::string &key) const {
        auto &db = Database::instance();
        std::shared_lock lock(db.mutex());
        Statement stmt(db.handle(), "SELECT 1 FROM prompts WHERE prompt_key = ?");
        stmt.bind(1, key);
        return stmt.step();
    }

    std::unordered_map<std::string, std::string> PromptStore::getAllPrompts() const {
        auto &db = Database::instance();
        std::shared_lock lock(db.mutex());
        std::unordered_map<std::string, std::string> prompts;

        Statement stmt(db.handle(), "SELECT prompt_key, prompt_content FROM prompts");
        while (stmt.step()) {
            prompts[stmt.getText(0)] = stmt.getText(1);
        }
        return prompts;
    }
} // namespace insoulforge