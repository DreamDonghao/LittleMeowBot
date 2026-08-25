/// @file MemoryService.cpp
/// @brief 记忆服务 - 实现

#include <service/MemoryService.hpp>
#include <config/Config.hpp>
#include <spdlog/spdlog.h>
#include <service/RAGFlowClient.hpp>
#include <sstream>
#include <algorithm>
#include <mutex>
#include <unordered_set>
#include <util/Logger.hpp>

namespace LittleMeowBot {
    namespace {
        /// @brief 单批提取上限：积压超过时分批处理
        constexpr size_t kMaxExtractBatch = 300;
        /// @brief 提取输入附带的重叠条数（保留区最新若干条，保证上下文连贯）
        constexpr size_t kOverlapCount = 10;

        /// @brief 同群并发 eviction 防重入
        /// @details 不能用 std::mutex 直接跨 co_await（协程可能在不同线程恢复），
        ///          改用标记集合 + RAII，协程销毁时自动清除标记
        class EvictionGuard {
        public:
            explicit EvictionGuard(const uint64_t groupId) : m_groupId(groupId) {
                std::lock_guard lock(s_mutex);
                m_acquired = s_evicting.insert(groupId).second;
            }

            ~EvictionGuard() {
                if (!m_acquired) return;
                std::lock_guard lock(s_mutex);
                s_evicting.erase(m_groupId);
            }

            EvictionGuard(const EvictionGuard &) = delete;

            EvictionGuard &operator=(const EvictionGuard &) = delete;

            [[nodiscard]] bool acquired() const { return m_acquired; }

        private:
            static inline std::mutex s_mutex;
            static inline std::unordered_set<uint64_t> s_evicting;
            uint64_t m_groupId;
            bool m_acquired = false;
        };

        /// @brief 去除首尾空白
        std::string trim(const std::string &s) {
            const size_t begin = s.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) return "";
            const size_t end = s.find_last_not_of(" \t\r\n");
            return s.substr(begin, end - begin + 1);
        }

        /// @brief 一次 LLM 调用完成"提取 + 合并"：结合现有记忆从对话中提取新条目并输出合并结果
        /// @return nullopt 表示 API 失败（调用方不得推进水位线）
        drogon::Task<std::optional<std::string> > maintainMemory(
            const std::string &existingMemory,
            const std::string &chatRecords,
            int maxTokens,
            const uint64_t groupId) {
            Json::Value messages;
            Json::Value item;
            item["role"] = "system";
            item["content"] = R"(你是一个【群聊记忆维护器】。
从对话中提取值得记住的信息，与现有记忆合并，直接输出合并后的最终记忆列表。

提取规则：
- 每条记忆必须带人物归属（群友名或外号），如：小明喜欢猫
- 每条独立成行，简短客观（5-15字）
- 不要推测未出现的信息，不要扩写背景
- 着重提取：外号别称、喜好、习惯、关系、重要约定

合并规则：
- 去除重复或相似条目
- 合并相关条目（"小明喜欢Python"+"小明经常写脚本"→"小明喜欢用Python写脚本"）
- 冲突时保留新信息
- 最多保留)" + std::to_string(Config::instance().shortTermMemoryMax) + R"(条

不提取：
- 一次性玩笑或情绪宣泄
- 系统指令或控制信息
- 固定套话、无价值内容

示例：
对话："小明：我天天写Python" → 记忆："小明喜欢写Python"
对话："老王：以后叫我老王" → 记忆："小明的外号是老王"

如果没有任何值得记住的内容，输出：无)";

            messages.append(item);
            item.clear();
            item["role"] = "user";
            item["content"] = "现有记忆：\n" + (existingMemory.empty() ? "（空）" : existingMemory)
                              + "\n\n=== 最近对话 ===\n" + chatRecords
                              + "\n\n请直接输出合并后的记忆列表，每行一条，不要解释：";
            messages.append(item);

            auto result = co_await ApiClient::requestLLM(messages, 0.4f, 0.9f, maxTokens, "memory", groupId);
            if (!result) {
                Logger::group(groupId).error("maintainMemory: API 请求失败");
                co_return std::nullopt;
            }
            co_return trim(result.value());
        }

        /// @brief 把记录区间拼接为 JSON 数组字符串（与旧 getChatRecordsText 格式一致）
        std::string formatRecordsText(
            const std::vector<Json::Value> &records, size_t from, size_t to) {
            std::string text = "[";
            bool first = true;
            for (size_t i = from; i < to; ++i) {
                if (!first) text += ",";
                first = false;
                text += records[i]["content"].asString();
            }
            text += "]";
            return text;
        }

        /// @brief 统计文本行数
        int countLines(const std::string &text) {
            if (text.empty()) return 0;
            int count = 0;
            std::istringstream stream(text);
            std::string line;
            while (std::getline(stream, line)) {
                if (!line.empty()) count++;
            }
            return count;
        }

        /// @brief 截断记忆到最多 N 条（保留前 N 条，重要信息通常排在前面）
        std::string trimToMaxLines(const std::string &memory, int maxLines) {
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

            std::string result;
            for (int i = 0; i < maxLines && i < static_cast<int>(lines.size()); ++i) {
                result += lines[i] + "\n";
            }
            return result;
        }

        /// @brief 筛选值得长期保存的记忆
        drogon::Task<std::string> selectMemoriesToMigrate(
            const std::string &shortTermMemory, const uint64_t groupId) {
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
- 最多筛选)" + std::to_string(migrateCount) + R"(条，宁缺毋滥，不足时不要凑数

短期记忆：
)" + shortTermMemory;

            messages.append(item);
            item.clear();
            item["role"] = "user";
            item["content"] = "请输出值得长期保存的记忆：";
            messages.append(item);

            auto result = co_await ApiClient::requestLLM(messages, 0.3f, 0.9f, 256, "memory", groupId);
            if (!result) {
                Logger::group(groupId).error("selectMemoriesToMigrate: API 请求失败");
                co_return "无";
            }
            co_return result.value();
        }

        /// @brief 从文本中删除已迁移的行（双向子串匹配）
        std::string removeMigratedLines(
            const std::string &original,
            const std::string &migrated) {
            std::vector<std::string> migratedLines;
            std::istringstream stream(migrated);
            std::string line;
            while (std::getline(stream, line)) {
                if (!line.empty() && line != "无") {
                    migratedLines.push_back(line);
                }
            }

            std::string remaining;
            std::istringstream origStream(original);
            while (std::getline(origStream, line)) {
                if (line.empty()) continue;

                bool shouldRemove = false;
                for (const auto &migratedLine: migratedLines) {
                    if (line.find(migratedLine) != std::string::npos ||
                        migratedLine.find(line) != std::string::npos) {
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

        /// @brief 从短期记忆迁移到长期记忆库
        drogon::Task<std::string> migrateToLongTermMemory(
            uint64_t groupId, const std::string &shortTermMemory) {
            const int maxLines = Config::instance().shortTermMemoryMax;

            // 检查 RAGFlow 是否实际可用（不仅 enabled，还需要配置了必要参数）
            const auto &kb = Config::instance().knowledgeBase;
            if (!kb.enabled || kb.memoryDatasetId.empty() || kb.memoryDocumentId.empty()) {
                Logger::group(groupId).info("短期记忆超限，RAGFlow 未配置或未启用，仅保留 {} 条", maxLines);
                std::string trimmed = trimToMaxLines(shortTermMemory, maxLines);
                Database::instance().updateShortTermMemory(groupId, trimmed);
                co_return trimmed;
            }

            // 获取群名
            std::string groupName = Database::instance().getGroupName(groupId);
            if (groupName.empty()) {
                groupName = std::to_string(groupId);
            }

            // 1. 让 LLM 筛选值得长期保存的记忆
            const std::string toMigrate = co_await selectMemoriesToMigrate(shortTermMemory, groupId);

            if (toMigrate.empty() || toMigrate == "无") {
                Logger::group(groupId).info("无记忆需要迁移");
                std::string trimmed = trimToMaxLines(shortTermMemory, maxLines);
                Database::instance().updateShortTermMemory(groupId, trimmed);
                co_return trimmed;
            }

            // 2. 逐条存入 RAGFlow 长期记忆库
            int successCount = 0;
            std::istringstream stream(toMigrate);
            std::string line;
            while (std::getline(stream, line)) {
                if (line.empty() || line == "无") continue;

                // 每条记忆单独存储，加上群名前缀
                if (std::string prefixedMemory = "[" + groupName + "] " + line;
                    co_await RAGFlowClient::addMemory(prefixedMemory, groupId)
                ) {
                    Logger::group(groupId).info("迁移记忆 [{}]: {}", groupName, line);
                    successCount++;
                } else {
                    Logger::group(groupId).warn("迁移记忆 [{}] 失败: {}", groupName, line);
                }
            }

            if (successCount == 0) {
                Logger::group(groupId).warn("迁移到 RAGFlow 全部失败，保留短期记忆");
                std::string trimmed = trimToMaxLines(shortTermMemory, maxLines);
                Database::instance().updateShortTermMemory(groupId, trimmed);
                co_return trimmed;
            }

            // 3. 从短期记忆中删除已迁移的条目
            std::string remaining = removeMigratedLines(shortTermMemory, toMigrate);

            // 4. 确保不超过maxLines
            remaining = trimToMaxLines(remaining, maxLines);
            Database::instance().updateShortTermMemory(groupId, remaining);

            Logger::group(groupId).info("迁移完成，成功 {} 条，短期记忆保留 {} 条", successCount, countLines(remaining));
            co_return remaining;
        }
    }

    drogon::Task<void> MemoryService::appendAndMergeMemory(uint64_t groupId) {
        EvictionGuard guard(groupId);
        if (!guard.acquired()) {
            co_return; // 该群正在提取中，等下一轮消息触发
        }

        auto &db = Database::instance();
        auto &config = Config::instance();

        // 1. 检查窗口是否超限
        const uint64_t watermark = db.getMemoryWatermark(groupId);
        const size_t count = db.getChatRecordCountSince(groupId, watermark);
        if (count <= static_cast<size_t>(config.windowTriggerCount)) {
            co_return;
        }

        size_t toDrop = count - config.windowKeepCount;
        if (toDrop == 0) {
            toDrop = 1; // 配置异常兜底（keep >= trigger），至少推进一条，保证触发循环能终止
        }

        const auto records = db.getChatRecordsSince(groupId, watermark, 0);
        if (records.size() < toDrop) {
            Logger::group(groupId).warn("窗口记录数与计数不一致，跳过本轮");
            co_return;
        }

        // 2. 分批提取+合并，逐批推进水位线
        std::string existingMemory = db.getShortTermMemory(groupId);
        uint64_t chunkEndId = watermark;
        size_t processed = 0;
        int successChunks = 0;

        while (processed < toDrop) {
            const size_t batchEnd = std::min(processed + kMaxExtractBatch, toDrop);
            // 附带后续最多 10 条做上下文连贯（可能来自待删除区或保留区，重复提取由 LLM 合并去重）
            const size_t overlapEnd = std::min(records.size(), batchEnd + kOverlapCount);
            const std::string chunkText = formatRecordsText(records, processed, overlapEnd);

            Logger::group(groupId).info("记忆提取: 待删第 {}-{} 条（共 {} 条）", processed + 1, batchEnd, toDrop);

            const auto result = co_await maintainMemory(
                existingMemory, chunkText, config.memoryExtractMaxTokens, groupId);
            if (!result) {
                // API 失败：水位线停在本批之前，下条消息自动重试
                Logger::group(groupId).warn("记忆提取失败，水位线保持 {}，下条消息将重试", chunkEndId);
                break;
            }
            if (!result->empty() && *result != "无") {
                existingMemory = *result;
            }

            // 原子写记忆+水位线，崩溃后重提取同批记录，LLM 合并去重保证幂等
            chunkEndId = records[batchEnd - 1]["id"].asUInt64();
            db.updateShortTermMemoryWithWatermark(groupId, existingMemory, chunkEndId);
            processed = batchEnd;
            successChunks++;
        }

        if (successChunks == 0) {
            co_return;
        }

        Logger::group(groupId).info("窗口已滑动: 滑出 {} 条，水位线 -> {}，记忆 {} 条",
                                    processed, chunkEndId, countLines(existingMemory));

        // 3. 检查是否需要迁移到长期记忆
        if (countLines(existingMemory) > config.shortTermMemoryMax) {
            Logger::group(groupId).info("短期记忆超限({}>{})，触发迁移",
                                        countLines(existingMemory), config.shortTermMemoryMax);
            co_await migrateToLongTermMemory(groupId, existingMemory);
        }

        co_return;
    }
}