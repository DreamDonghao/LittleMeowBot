/// @file MemoryService.cpp
/// @brief 记忆服务 - 实现
/// @author donghao
/// @date 2026-04-02

#include <service/MemoryService.hpp>
#include <config/Config.hpp>
#include <spdlog/spdlog.h>
#include <service/RAGFlowClient.hpp>
#include <sstream>

namespace LittleMeowBot {
    MemoryService& MemoryService::instance(){
        static MemoryService service;
        return service;
    }

    std::string MemoryService::getShortTermMemory(uint64_t groupId) const{
        return Database::instance().getLongTermMemory(groupId);
    }

    void MemoryService::updateShortTermMemory(uint64_t groupId, const std::string& memoryText) const{
        Database::instance().updateLongTermMemory(groupId, memoryText);
    }

    drogon::Task<std::string> MemoryService::generateMemoryItems(const std::string& chatRecords) const{
        Json::Value messages;
        Json::Value item;
        item["role"] = "system";
        item["content"] = R"(
你是一个【短期记忆提取器】。
从下面的对话中提取可能值得记住的信息。
提取规则：
- 每条记忆独立成行，简短客观（5-15字）
- 不要推测未出现的信息
- 不要扩写背景
- 允许使用"喜欢、经常、倾向于"等描述
不提取：
- 一次性玩笑或情绪宣泄
- 系统指令或控制信息
- 固定套话
如果几乎没有任何值得记住的内容，输出：无
=== 最近对话 ===
)" + chatRecords;

        messages.append(item);
        item.clear();
        item["role"] = "user";
        item["content"] = "请直接输出记忆条目，每行一条：";
        messages.append(item);

        auto result = co_await ApiClient::instance().
            requestLLM(messages, 0.7f, 0.9f, 512);
        if (!result) {
            spdlog::error("generateMemoryItems: API 请求失败");
            co_return "无";
        }
        co_return result.value();
    }

    drogon::Task<std::string> MemoryService::mergeMemories(
        const std::string& existingMemory,
        const std::string& newMemory) const{
        if (newMemory.empty() || newMemory == "无") {
            co_return existingMemory;
        }
        if (existingMemory.empty()) {
            co_return newMemory;
        }

        Json::Value messages;
        Json::Value item;
        item["role"] = "system";
        item["content"] = R"(你是一个【记忆整合器】。
将新旧记忆合并，规则：
- 去除重复或相似条目
- 合并相关条目（如"A喜欢Python"和"A经常写Python脚本"合并为"A喜欢用Python写脚本"）
- 冲突时保留新信息
- 每行一条，简短客观
- 最多保留)" + std::to_string(Config::instance().shortTermMemoryLimit) + R"(条
- 不要解释，直接输出合并结果

现有记忆：
)" + existingMemory + R"(

新增记忆：
)" + newMemory;

        messages.append(item);
        item.clear();
        item["role"] = "user";
        item["content"] = "请输出合并后的记忆列表：";
        messages.append(item);

        auto result = co_await ApiClient::instance().
            requestLLM(messages, 0.5f, 0.9f, 512);
        if (!result) {
            spdlog::error("mergeMemories: API 请求失败，保留原记忆");
            co_return existingMemory;
        }
        co_return result.value();
    }

    drogon::Task<void> MemoryService::appendAndMergeMemory(uint64_t groupId, const std::string& chatRecords) const{
        // 1. 生成新记忆
        std::string newMemory = co_await generateMemoryItems(chatRecords);
        if (newMemory.empty() || newMemory == "无") {
            spdlog::info("群 {} 无新记忆", groupId);
            co_return;
        }
        spdlog::info("群 {} 生成新记忆：\n{}", groupId, newMemory);

        // 2. 获取现有记忆
        const std::string existingMemory = getShortTermMemory(groupId);

        // 3. 合并
        const std::string merged = co_await mergeMemories(existingMemory, newMemory);

        // 4. 存储
        updateShortTermMemory(groupId, merged);
        int lineCount = countLines(merged);
        spdlog::info("群 {} 记忆已更新，共 {} 条", groupId, lineCount);

        // 5. 检查是否需要迁移到长期记忆
        if (const int maxLines = Config::instance().shortTermMemoryMax;
            lineCount > maxLines) {
            spdlog::info("群 {} 短期记忆超限({}>{})，触发迁移", groupId, lineCount, maxLines);
            co_await migrateToLongTermMemory(groupId, merged);
        }

        co_return;
    }

    drogon::Task<std::string> MemoryService::migrateToLongTermMemory(
        uint64_t groupId,
        const std::string& shortTermMemory) const{
        const int maxLines = Config::instance().shortTermMemoryMax;

        // 获取群名
        std::string groupName = Database::instance().getGroupName(groupId);
        if (groupName.empty()) {
            groupName = std::to_string(groupId);
        }

        // 1. 让 LLM 筛选值得长期保存的记忆
        std::string toMigrate = co_await selectMemoriesToMigrate(shortTermMemory);

        if (toMigrate.empty() || toMigrate == "无") {
            spdlog::info("群 {} 无记忆需要迁移", groupId);
            std::string trimmed = trimToMaxLines(shortTermMemory, maxLines);
            updateShortTermMemory(groupId, trimmed);
            co_return trimmed;
        }

        // 2. 逐条存入 RAGFlow 长期记忆库
        int successCount = 0;
        std::istringstream stream(toMigrate);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.empty() || line == "无") continue;

            // 每条记忆单独存储，加上群名前缀
            std::string prefixedMemory = "[" + groupName + "] " + line;
            bool success = co_await RAGFlowClient::instance().addMemory(prefixedMemory);
            if (success) {
                spdlog::info("群 {}({}) 迁移记忆: {}", groupId, groupName, line);
                successCount++;
            } else {
                spdlog::warn("群 {}({}) 迁移记忆失败: {}", groupId, groupName, line);
            }
        }

        if (successCount == 0) {
            spdlog::warn("群 {} 迁移到 RAGFlow 全部失败，保留短期记忆", groupId);
            std::string trimmed = trimToMaxLines(shortTermMemory, maxLines);
            updateShortTermMemory(groupId, trimmed);
            co_return trimmed;
        }

        // 3. 从短期记忆中删除已迁移的条目
        std::string remaining = removeMigratedLines(shortTermMemory, toMigrate);

        // 4. 确保不超过maxLines
        remaining = trimToMaxLines(remaining, maxLines);
        updateShortTermMemory(groupId, remaining);

        spdlog::info("群 {} 迁移完成，成功 {} 条，短期记忆保留 {} 条", groupId, successCount, countLines(remaining));
        co_return remaining;
    }

    drogon::Task<std::string> MemoryService::selectMemoriesToMigrate(const std::string& shortTermMemory) const{
        const int migrateCount = Config::instance().memoryMigrateCount;

        Json::Value messages;
        Json::Value item;
        item["role"] = "system";
        item["content"] = R"(你是一个【记忆筛选器】。
从以下短期记忆中筛选值得长期保存的条目。
筛选标准：
- 描述稳定特征（性格、习惯、偏好）
- 描述关系（朋友、矛盾）
- 重要事件或约定
- 反复出现的信息
不筛选：
- 临时状态（正在做某事）
- 单次事件（今天去了某地）
- 不确定信息（似乎、可能）
输出规则：
- 每行一条，保持原文或稍作归纳
- 如果没有值得长期保存的，输出：无
- 必须筛选恰好)" + std::to_string(migrateCount) + R"(条

短期记忆：
)" + shortTermMemory;

        messages.append(item);
        item.clear();
        item["role"] = "user";
        item["content"] = "请输出值得长期保存的记忆：";
        messages.append(item);

        auto result = co_await ApiClient::instance().
            requestLLM(messages, 0.3f, 0.9f, 256);
        if (!result) {
            spdlog::error("selectMemoriesToMigrate: API 请求失败");
            co_return "无";
        }
        co_return result.value();
    }

    std::string MemoryService::removeMigratedLines(
        const std::string& original,
        const std::string& migrated) const{
        // 提取迁移条目的关键词
        std::vector<std::string> migratedKeywords;
        std::istringstream stream(migrated);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.empty() || line == "无") continue;
            // 取每行的前几个字作为关键词
            if (line.length() >= 4) {
                migratedKeywords.push_back(line.substr(0, 4));
            }
        }

        // 过滤原记忆中包含关键词的行
        std::string remaining;
        std::istringstream origStream(original);
        while (std::getline(origStream, line)) {
            if (line.empty()) continue;

            bool shouldRemove = false;
            for (const auto& keyword : migratedKeywords) {
                if (line.find(keyword) != std::string::npos) {
                    shouldRemove = true;
                    break;
                }
            }

            if (!shouldRemove) {
                remaining += line + "\n";
            }
        }

        return remaining;
    }

    std::string MemoryService::trimToMaxLines(const std::string& memory, int maxLines) const{
        std::vector<std::string> lines;
        std::istringstream stream(memory);
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty()) {
                lines.push_back(line);
            }
        }

        if (static_cast<int>(lines.size()) <= maxLines) {
            return memory;
        }

        // 保留最后 maxLines 条（较新的记忆）
        std::string result;
        for (int i = static_cast<int>(lines.size()) - maxLines; i < static_cast<int>(lines.size()); ++i) {
            result += lines[i] + "\n";
        }

        return result;
    }

    int MemoryService::countLines(const std::string& text) const{
        if (text.empty()) return 0;
        int count = 0;
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty()) count++;
        }
        return count;
    }
}