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
#include <storage/MemoryStore.hpp>
#include <storage/ChatRecordStore.hpp>
#include <storage/SessionStore.hpp>
#include <storage/AffinityStore.hpp>
#include <model/QQMessage.hpp>
#include <util/tool.h>

namespace insoulforge {
    namespace {
        /// @brief 单批提取上限：积压超过时分批处理
        constexpr size_t kMaxExtractBatch = 300;
        /// @brief 提取输入附带的重叠条数（保留区最新若干条，保证上下文连贯）
        constexpr size_t kOverlapCount = 10;
        /// @brief 好感度单次评分的变化量上限
        constexpr int kMaxAffinityDelta = 5;
        /// @brief 好感度评分单次输入记录上限（积压过大时截断）
        constexpr size_t kMaxAffinityRecords = 300;

        /// @brief 同群并发 eviction 防重入
        /// @details 不能用 std::mutex 直接跨 co_await（协程可能在不同线程恢复），
        ///          改用标记集合 + RAII，协程销毁时自动清除标记
        class EvictionGuard {
        public:
            explicit EvictionGuard(const uint64_t sessionId) : m_groupId(sessionId) {
                std::lock_guard lock(s_mutex);
                m_acquired = s_evicting.insert(sessionId).second;
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
            const uint64_t sessionId) {
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

            auto result = co_await ApiClient::requestLLM(messages, 0.4f, 0.9f, maxTokens, "memory", sessionId);
            if (!result) {
                Logger::session(sessionId).error("maintainMemory: API 请求失败");
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

        /// @brief 好感度评分：对刚滑出窗口的记录单独发一次请求，让 LLM 评估每个用户的好感度变化量
        /// @details 只对本次真正滑出的记录评分——提取失败时水位线不推进、同批记录会重试，
        ///          评分若不跟着水位线走会重复加减。评分失败仅跳过本批，不阻塞记忆流程
        drogon::Task<void> updateAffinityFromRecords(
            const uint64_t sessionId, const std::vector<Json::Value> &records, const size_t evictedCount) {
            const size_t limit = std::min(evictedCount, kMaxAffinityRecords);
            if (limit == 0) co_return;

            Json::Value messages;
            Json::Value item;
            item["role"] = "system";
            item["content"] = R"(你是一个【群聊好感度评估器】。
机器人看完了一段群聊记录，请评估每个发言用户在这段对话中给机器人留下的印象变化。

评分规则：
- 只输出一个 JSON 对象，键为用户 QQ 号（字符串），值为好感度变化量（整数，-5 到 5）
- 正面（有趣、友好、真诚分享、帮助、陪伴）给正分；负面（辱骂、恶意挑衅、骚扰、令人不适）给负分
- 普通闲聊、无明显印象变化 → 不要输出该用户
- 机器人自己的消息（qq 为 "self"）和系统消息不要评估
- 只依据对话内容判断，不要虚构记录中不存在的 QQ 号

示例：
{"123456": 2, "789012": -3}

没有任何值得调整的变化时输出：{})";
            messages.append(item);
            item.clear();
            item["role"] = "user";
            item["content"] = "=== 群聊记录 ===\n" + formatRecordsText(records, 0, limit)
                              + "\n\n请输出好感度变化 JSON：";
            messages.append(item);

            const auto result = co_await ApiClient::requestLLM(messages, 0.3f, 0.9f, 256, "affinity", sessionId);
            if (!result) {
                Logger::session(sessionId).warn("好感度评分: API 请求失败，本批跳过");
                co_return;
            }

            // 容忍模型输出 ```json 围栏等杂质：截取首尾大括号之间的内容
            const std::string &text = result.value();
            const size_t start = text.find('{');
            const size_t end = text.rfind('}');
            if (start == std::string::npos || end == std::string::npos || end <= start) {
                Logger::session(sessionId).warn("好感度评分: 响应中无 JSON，跳过: {}", text.substr(0, 100));
                co_return;
            }

            Json::Value deltas;
            if (!Json::Reader().parse(text.substr(start, end - start + 1), deltas) || !deltas.isObject()) {
                Logger::session(sessionId).warn("好感度评分: JSON 解析失败，跳过");
                co_return;
            }

            int applied = 0;
            for (const auto &qqStr: deltas.getMemberNames()) {
                const uint64_t qqNumber = parseUInt64(qqStr);
                if (qqNumber == 0 || qqNumber == QQMessage::kSystemAccountId) continue;
                const Json::Value &deltaValue = deltas[qqStr];
                if (!deltaValue.isIntegral()) continue;
                if (const int delta = std::clamp(deltaValue.asInt(), -kMaxAffinityDelta, kMaxAffinityDelta); delta != 0) {
                    AffinityStore::instance().adjustAffinity(sessionId, qqNumber, delta);
                    ++applied;
                }
            }
            Logger::session(sessionId).info("好感度评分完成: {} 条记录，更新 {} 人", limit, applied);
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
            const std::string &shortTermMemory, const uint64_t sessionId) {
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

            auto result = co_await ApiClient::requestLLM(messages, 0.3f, 0.9f, 256, "memory", sessionId);
            if (!result) {
                Logger::session(sessionId).error("selectMemoriesToMigrate: API 请求失败");
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
            uint64_t sessionId, const std::string &shortTermMemory) {
            const int maxLines = Config::instance().shortTermMemoryMax;

            // 检查 RAGFlow 是否实际可用（不仅 enabled，还需要配置了必要参数）
            const auto &kb = Config::instance().knowledgeBase;
            if (!kb.enabled || kb.memoryDatasetId.empty() || kb.memoryDocumentId.empty()) {
                Logger::session(sessionId).info("短期记忆超限，RAGFlow 未配置或未启用，仅保留 {} 条", maxLines);
                std::string trimmed = trimToMaxLines(shortTermMemory, maxLines);
                MemoryStore::instance().updateShortTermMemory(sessionId, trimmed);
                co_return trimmed;
            }

            // 获取群名
            std::string groupName = SessionStore::instance().getSessionName(sessionId);
            if (groupName.empty()) {
                groupName = std::to_string(sessionId);
            }

            // 1. 让 LLM 筛选值得长期保存的记忆
            const std::string toMigrate = co_await selectMemoriesToMigrate(shortTermMemory, sessionId);

            if (toMigrate.empty() || toMigrate == "无") {
                Logger::session(sessionId).info("无记忆需要迁移");
                std::string trimmed = trimToMaxLines(shortTermMemory, maxLines);
                MemoryStore::instance().updateShortTermMemory(sessionId, trimmed);
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
                    co_await RAGFlowClient::addMemory(prefixedMemory, sessionId)
                ) {
                    Logger::session(sessionId).info("迁移记忆 [{}]: {}", groupName, line);
                    successCount++;
                } else {
                    Logger::session(sessionId).warn("迁移记忆 [{}] 失败: {}", groupName, line);
                }
            }

            if (successCount == 0) {
                Logger::session(sessionId).warn("迁移到 RAGFlow 全部失败，保留短期记忆");
                std::string trimmed = trimToMaxLines(shortTermMemory, maxLines);
                MemoryStore::instance().updateShortTermMemory(sessionId, trimmed);
                co_return trimmed;
            }

            // 3. 从短期记忆中删除已迁移的条目
            std::string remaining = removeMigratedLines(shortTermMemory, toMigrate);

            // 4. 确保不超过maxLines
            remaining = trimToMaxLines(remaining, maxLines);
            MemoryStore::instance().updateShortTermMemory(sessionId, remaining);

            Logger::session(sessionId).info("迁移完成，成功 {} 条，短期记忆保留 {} 条", successCount, countLines(remaining));
            co_return remaining;
        }
    }

    drogon::Task<void> MemoryService::appendAndMergeMemory(uint64_t sessionId) {
        EvictionGuard guard(sessionId);
        if (!guard.acquired()) {
            co_return; // 该群正在提取中，等下一轮消息触发
        }

        auto &memoryStore = MemoryStore::instance();
        auto &config = Config::instance();

        // 1. 检查窗口是否超限
        const uint64_t watermark = memoryStore.getMemoryWatermark(sessionId);
        const size_t count = ChatRecordStore::instance().getChatRecordCountSince(sessionId, watermark);
        if (count <= static_cast<size_t>(config.windowTriggerCount)) {
            co_return;
        }

        size_t toDrop = count - config.windowKeepCount;
        if (toDrop == 0) {
            toDrop = 1; // 配置异常兜底（keep >= trigger），至少推进一条，保证触发循环能终止
        }

        const auto records = ChatRecordStore::instance().getChatRecordsSince(sessionId, watermark, 0);
        if (records.size() < toDrop) {
            Logger::session(sessionId).warn("窗口记录数与计数不一致，跳过本轮");
            co_return;
        }

        // 2. 分批提取+合并，逐批推进水位线
        std::string existingMemory = memoryStore.getShortTermMemory(sessionId);
        uint64_t chunkEndId = watermark;
        size_t processed = 0;
        int successChunks = 0;

        while (processed < toDrop) {
            const size_t batchEnd = std::min(processed + kMaxExtractBatch, toDrop);
            // 附带后续最多 10 条做上下文连贯（可能来自待删除区或保留区，重复提取由 LLM 合并去重）
            const size_t overlapEnd = std::min(records.size(), batchEnd + kOverlapCount);
            const std::string chunkText = formatRecordsText(records, processed, overlapEnd);

            Logger::session(sessionId).info("记忆提取: 待删第 {}-{} 条（共 {} 条）", processed + 1, batchEnd, toDrop);

            const auto result = co_await maintainMemory(
                existingMemory, chunkText, config.memoryExtractMaxTokens, sessionId);
            if (!result) {
                // API 失败：水位线停在本批之前，下条消息自动重试
                Logger::session(sessionId).warn("记忆提取失败，水位线保持 {}，下条消息将重试", chunkEndId);
                break;
            }
            if (!result->empty() && *result != "无") {
                existingMemory = *result;
            }

            // 原子写记忆+水位线，崩溃后重提取同批记录，LLM 合并去重保证幂等
            chunkEndId = records[batchEnd - 1]["id"].asUInt64();
            memoryStore.updateShortTermMemoryWithWatermark(sessionId, existingMemory, chunkEndId);
            processed = batchEnd;
            successChunks++;
        }

        if (successChunks == 0) {
            co_return;
        }

        Logger::session(sessionId).info("窗口已滑动: 滑出 {} 条，水位线 -> {}，记忆 {} 条",
                                    processed, chunkEndId, countLines(existingMemory));

        // 3. 好感度评分（独立请求，只针对本次滑出的记录）
        co_await updateAffinityFromRecords(sessionId, records, processed);

        // 4. 检查是否需要迁移到长期记忆
        if (countLines(existingMemory) > config.shortTermMemoryMax) {
            Logger::session(sessionId).info("短期记忆超限({}>{})，触发迁移",
                                        countLines(existingMemory), config.shortTermMemoryMax);
            co_await migrateToLongTermMemory(sessionId, existingMemory);
        }

        co_return;
    }
}