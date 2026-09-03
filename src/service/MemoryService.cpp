/// @file MemoryService.cpp
/// @brief 记忆服务 - 实现

#include <algorithm>
#include <config/Config.hpp>
#include <model/QQMessage.hpp>
#include <mutex>
#include <ranges>
#include <service/LongTermMemory.hpp>
#include <service/MemoryService.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <storage/AffinityStore.hpp>
#include <storage/ChatRecordStore.hpp>
#include <storage/LongTermMemoryStore.hpp>
#include <storage/MemoryStore.hpp>
#include <unordered_map>
#include <unordered_set>
#include <util/CommonUtil.hpp>
#include <util/JsonUtil.hpp>
#include <util/Logger.hpp>

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
        /// @brief 每条新记忆最多召回的长期记忆条数
        constexpr int kRecallPerMemory = 2;
        /// @brief 单轮召回候选总量上限（超出时保留相似度最高的）
        constexpr size_t kMaxRecalledEntries = 20;

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
                if (!m_acquired)
                    return;
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

        /// @brief 按行拆分文本，跳过空行
        std::vector<std::string> splitLines(const std::string &text) {
            std::vector<std::string> lines;
            std::istringstream stream(text);
            std::string line;
            while (std::getline(stream, line)) {
                if (!line.empty())
                    lines.push_back(line);
            }
            return lines;
        }

        /// @brief 把若干行拼接回文本（每行末尾换行，与既有存储格式一致）
        std::string joinLines(const std::vector<std::string> &lines) {
            std::string result;
            for (const auto &line: lines)
                result += line + '\n';
            return result;
        }

        /// @brief 把条目编号为 "1. xxx" 的多行文本（空列表显示占位符）
        std::string numberedLines(const std::vector<std::string> &lines) {
            std::string text;
            for (size_t i = 0; i < lines.size(); ++i)
                text += std::to_string(i + 1) + ". " + lines[i] + '\n';
            return text.empty() ? "（空）" : text;
        }

        /// @brief LLM 响应文本 → JSON 对象（容忍模型输出 ```json 围栏等杂质）
        /// @return nullopt 表示 API 失败或输出不是合法 JSON（调用方不得推进水位线），失败原因已按 tag 记日志
        std::optional<json> parseLlmJson(
          const std::optional<std::string> &result, const std::string &tag, const uint64_t sessionId) {
            if (!result) {
                Logger::session(sessionId).error("{}: API 请求失败", tag);
                return std::nullopt;
            }

            std::string payload;
            if (!tryExtractJsonObject(*result, payload)) {
                Logger::session(sessionId).warn("{}: 响应中无 JSON: {}", tag, result->substr(0, 100));
                return std::nullopt;
            }

            json parsed;
            if (!tryParseJson(payload, parsed) || !parsed.is_object()) {
                Logger::session(sessionId).warn("{}: JSON 解析失败，本批重试", tag);
                return std::nullopt;
            }
            return parsed;
        }

        /// @brief 从聊天记录中提取新记忆，每条只包含一条完整信息
        /// @return nullopt 表示 API 失败或输出不是合法 JSON（调用方不得推进水位线）
        drogon::Task<std::optional<std::vector<std::string>>> extractMemories(
          std::string chatRecords, const int maxTokens, const uint64_t sessionId) {
            json messages;
            json item;
            item["role"] = "system";
            item["content"] = R"(你是一个【群聊记忆提取器】。
从群聊记录中提取值得记住的信息。

提取规则：
- 每条记忆必须带人物归属（群友名或外号），如：小明喜欢猫
- 每条只包含一条完整信息，简短客观（5-15字）
- 不要推测未出现的信息，不要扩写背景
- 着重提取：外号别称、喜好、习惯、关系、重要约定

不提取：
- 一次性玩笑或情绪宣泄
- 系统指令或控制信息
- 固定套话、无价值内容

只输出一个 JSON 对象，memories 为字符串数组：
{"memories": ["小明喜欢写Python", "老王的外号是老王"]}
没有任何值得记住的内容时输出：{"memories": []})";

            messages.push_back(item);
            item.clear();
            item["role"] = "user";
            item["content"] = "=== 群聊记录 ===\n" + std::move(chatRecords) + "\n\n请输出提取结果 JSON：";
            messages.push_back(item);

            const auto parsed = parseLlmJson(
              co_await LlmClient::requestLLM(std::move(messages), 0.4f, 0.9f, maxTokens, "memory", sessionId),
              "记忆提取", sessionId);
            if (!parsed) {
                co_return std::nullopt;
            }
            const json &memoriesArr = atOrNull(*parsed, "memories");
            if (!memoriesArr.is_array()) {
                Logger::session(sessionId).warn("记忆提取: 输出缺少 memories 数组，本批重试");
                co_return std::nullopt;
            }

            std::vector<std::string> memories;
            for (const auto &entry: memoriesArr) {
                if (std::string text = trim(jsonToString(entry)); !text.empty())
                    memories.push_back(std::move(text));
            }
            co_return memories;
        }

        /// @brief 用新记忆在长期记忆库中召回合并候选：每条至多 kRecallPerMemory 条、相似度需达到召回阈值，
        ///        跨查询按 id 去重（同一旧记忆只参与一次整理），候选总量超限时保留相似度最高的
        /// @return 召回条目（id + 内容 + 相似度）；Embedding 失败的查询跳过（召回是优化，不阻塞记忆流程）
        drogon::Task<std::vector<SimilarMemory>> recallForMerge(
          std::vector<std::string> newMemories, const uint64_t sessionId) {
            const float threshold = static_cast<float>(Config::instance().longTermRecallThreshold);
            std::unordered_map<int64_t, SimilarMemory> recalled;

            for (const auto &memory: newMemories) {
                const auto embedding = co_await LlmClient::requestEmbedding(memory, sessionId);
                if (!embedding) {
                    Logger::session(sessionId).warn("记忆召回: 向量化失败，跳过该条查询");
                    continue;
                }
                for (const auto &hit: LongTermMemoryStore::searchSimilar(sessionId, *embedding, kRecallPerMemory)) {
                    if (hit.similarity < threshold)
                        continue;
                    auto [it, inserted] = recalled.try_emplace(hit.id, hit);
                    if (!inserted && it->second.similarity < hit.similarity)
                        it->second.similarity = hit.similarity;
                }
            }

            std::vector<SimilarMemory> result;
            result.reserve(recalled.size());
            for (auto &item: recalled | std::views::values)
                result.push_back(item);
            if (result.size() > kMaxRecalledEntries) {
                std::ranges::sort(result, [](const auto &a, const auto &b) { return a.similarity > b.similarity; });
                result.resize(kMaxRecalledEntries);
            }
            co_return result;
        }

        /// @brief 单条合并后的长期记忆（sources 为被它合并或取代的召回条目 id）
        struct MergedLongTermMemory {
            std::vector<int64_t> sources;
            std::string content;
        };

        /// @brief 归类整理结果
        struct ReconcileResult {
            std::vector<std::string> shortTerm;
            std::vector<MergedLongTermMemory> longTerm;
        };

        /// @brief 把新记忆、当前短期记忆与召回的长期记忆合并归类为短期/长期两部分
        /// @param longTermEnabled Embedding 是否可用（不可用时禁止输出长期记忆）
        /// @return nullopt 表示 API 失败或输出不是合法 JSON（调用方不得推进水位线）
        drogon::Task<std::optional<ReconcileResult>> reconcileMemory(std::vector<std::string> currentShortTerm,
          std::vector<std::string> newMemories, std::vector<SimilarMemory> recalled, const bool longTermEnabled,
          const uint64_t sessionId) {
            const auto &config = Config::instance();

            std::string systemPrompt = R"(你是一个【群聊记忆整理器】。
把新提取的记忆与当前短期记忆、召回的长期记忆合并整理，归类为短期记忆和长期记忆两部分。

合并规则：
- 去除重复或相似条目
- 合并相关条目（"小明喜欢Python"+"小明经常写脚本"→"小明喜欢用Python写脚本"）
- 冲突时保留较新的信息（新提取的记忆最新，召回的长期记忆最旧）
- 每条记忆带人物归属（群友名或外号），简短客观（5-15字）

归类规则：
- 短期记忆：临时状态、近期事件、上下文相关，最多)" +
                                       std::to_string(config.shortTermMemoryMax) +
                                       R"(条
- 长期记忆：稳定特征（性格、习惯、偏好、外号别称）、关系、重要约定，宁缺毋滥
- 召回的长期记忆内容没有变化时不要输出（保持原样）；只有被合并、修改或补充时才输出
- 长期记忆条目的 sources 填写被它合并或取代的召回条目 id（输入中方括号里的数字）；全新条目填空数组

只输出一个 JSON 对象，格式如下：
{"shortTerm": ["小明喜欢用Python写脚本"], "longTerm": [{"sources": [12], "content": "小明是程序员，喜欢用Python写脚本"}]})";
            if (!longTermEnabled) {
                systemPrompt += "\n长期记忆库当前不可用：所有记忆都归入短期记忆，longTerm 固定输出空数组。";
            }

            json messages;
            json item;
            item["role"] = "system";
            item["content"] = systemPrompt;
            messages.push_back(item);
            item.clear();

            std::string recalledText;
            for (const auto &entry: recalled)
                recalledText += "[" + std::to_string(entry.id) + "] " + entry.content + '\n';
            if (recalledText.empty())
                recalledText = "（无）\n";

            item["role"] = "user";
            item["content"] = "=== 当前短期记忆 ===\n" + numberedLines(currentShortTerm) + "\n=== 新提取的记忆 ===\n" +
                              numberedLines(newMemories) + "\n=== 召回的长期记忆 ===\n" + recalledText +
                              "\n请输出整理结果 JSON：";
            messages.push_back(item);

            const auto parsed = parseLlmJson(co_await LlmClient::requestLLM(std::move(messages), 0.3f, 0.9f,
                                               config.memoryExtractMaxTokens, "memory", sessionId),
              "记忆整理", sessionId);
            if (!parsed) {
                co_return std::nullopt;
            }
            const json &shortTermArr = atOrNull(*parsed, "shortTerm");
            if (!shortTermArr.is_array()) {
                Logger::session(sessionId).warn("记忆整理: 输出缺少 shortTerm 数组，本批重试");
                co_return std::nullopt;
            }

            ReconcileResult reconcile;
            for (const auto &entry: shortTermArr) {
                if (std::string text = trim(jsonToString(entry)); !text.empty())
                    reconcile.shortTerm.push_back(std::move(text));
            }
            const size_t maxShortTerm = static_cast<size_t>(std::max(config.shortTermMemoryMax, 0));
            if (reconcile.shortTerm.size() > maxShortTerm) {
                Logger::session(sessionId).warn("记忆整理: 短期记忆超限({} > {})，截断保留前 {} 条",
                  reconcile.shortTerm.size(), maxShortTerm, maxShortTerm);
                reconcile.shortTerm.resize(maxShortTerm);
            }

            const json &longTermArr = atOrNull(*parsed, "longTerm");
            if (longTermEnabled && longTermArr.is_array()) {
                for (const auto &entry: longTermArr) {
                    if (!entry.is_object())
                        continue;
                    std::string content = trim(getStr(entry, "content"));
                    if (content.empty())
                        continue;
                    MergedLongTermMemory merged;
                    merged.content = std::move(content);
                    const json &sources = atOrNull(entry, "sources");
                    if (sources.is_array()) {
                        for (const auto &source: sources) {
                            if (!source.is_number_integer())
                                continue;
                            const auto id = source.get<int64_t>();
                            // 只接受真实召回过的 id，防止模型编造误删无关条目
                            if (std::ranges::any_of(recalled, [id](const auto &r) { return r.id == id; }))
                                merged.sources.push_back(id);
                        }
                    }
                    reconcile.longTerm.push_back(std::move(merged));
                }
            }
            co_return reconcile;
        }

        /// @brief 把记录区间拼接为 JSON 数组字符串（与旧 getChatRecordsText 格式一致）
        std::string formatRecordsText(const std::vector<json> &records, const size_t from, const size_t to) {
            std::string text = "[";
            bool first = true;
            for (size_t i = from; i < to; ++i) {
                if (!first)
                    text += ',';
                first = false;
                text += records[i]["content"].get<std::string>();
            }
            text += ']';
            return text;
        }

        /// @brief 去除记录 content JSON 中的 images 字段（文件名/URL 对提取与评分无用，纯耗 token）
        /// @details 解析失败或非 JSON 内容原样保留（历史存量可能是纯文本），不做修改
        void stripRecordImages(std::vector<json> &records) {
            for (auto &record: records) {
                json content;
                if (!tryParseJson(getStr(record, "content"), content) || !content.is_object())
                    continue;
                if (!content.contains("images"))
                    continue;
                content.erase("images");
                record["content"] = dumpJson(content);
            }
        }

        /// @brief 好感度评分：对刚滑出窗口的记录单独发一次请求，让 LLM 评估每个用户的好感度变化量
        /// @details 只对本次真正滑出的记录评分——提取失败时水位线不推进、同批记录会重试，
        ///          评分若不跟着水位线走会重复加减。评分失败仅跳过本批，不阻塞记忆流程
        drogon::Task<> updateAffinityFromRecords(
          const uint64_t sessionId, std::vector<json> records, const size_t evictedCount) {
            const size_t limit = std::min(evictedCount, kMaxAffinityRecords);
            if (limit == 0)
                co_return;

            json messages;
            json item;
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
            messages.push_back(item);
            item.clear();
            item["role"] = "user";
            item["content"] =
              "=== 群聊记录 ===\n" + formatRecordsText(records, 0, limit) + "\n\n请输出好感度变化 JSON：";
            messages.push_back(item);

            const auto deltas =
              parseLlmJson(co_await LlmClient::requestLLM(std::move(messages), 0.3f, 0.9f, 256, "affinity", sessionId),
                "好感度评分", sessionId);
            if (!deltas) {
                co_return;
            }

            int applied = 0;
            for (const auto &[qqStr, deltaValue]: deltas->items()) {
                const uint64_t qqNumber = parseUInt64(qqStr);
                if (qqNumber == 0 || qqNumber == QQMessage::kSystemAccountId)
                    continue;
                if (!deltaValue.is_number_integer())
                    continue;
                if (const int delta = std::clamp(jsonToInt(deltaValue), -kMaxAffinityDelta, kMaxAffinityDelta);
                  delta != 0) {
                    AffinityStore::adjustAffinity(sessionId, qqNumber, delta);
                    ++applied;
                }
            }
            Logger::session(sessionId).info("好感度评分完成: {} 条记录，更新 {} 人", limit, applied);
        }
    } // namespace

    drogon::Task<> MemoryService::appendAndMergeMemory(const uint64_t sessionId) {
        if (const EvictionGuard guard(sessionId); !guard.acquired()) {
            co_return; // 该群正在提取中，等下一轮消息触发
        }

        auto &config = Config::instance();

        // 1. 检查窗口是否超限
        const uint64_t watermark = MemoryStore::getMemoryWatermark(sessionId);
        const size_t count = ChatRecordStore::getChatRecordCountSince(sessionId, watermark);
        if (count <= static_cast<size_t>(config.windowTriggerCount)) {
            co_return;
        }

        size_t toDrop = count - config.windowKeepCount;
        if (toDrop == 0) {
            toDrop = 1; // 配置异常兜底（keep >= trigger），至少推进一条，保证触发循环能终止
        }

        auto records = ChatRecordStore::getChatRecordsSince(sessionId, watermark, 0);
        if (records.size() < toDrop) {
            Logger::session(sessionId).warn("窗口记录数与计数不一致，跳过本轮");
            co_return;
        }
        stripRecordImages(records);

        // Embedding 未配置时跳过召回与长期记忆写入，只在短期记忆内整理
        const bool longTermEnabled = !config.embedding.baseUrl.empty() && !config.embedding.model.empty();

        // 2. 分批"提取 → 召回 → 归类"，逐批推进水位线
        std::string existingMemory = MemoryStore::getShortTermMemory(sessionId);
        uint64_t chunkEndId = watermark;
        size_t processed = 0;
        int successChunks = 0;
        int longTermAdded = 0;
        int longTermReplaced = 0;

        while (processed < toDrop) {
            const size_t batchEnd = std::min(processed + kMaxExtractBatch, toDrop);
            // 附带后续最多 10 条做上下文连贯（可能来自待删除区或保留区，重复内容由归类合并去重）
            const size_t overlapEnd = std::min(records.size(), batchEnd + kOverlapCount);
            const std::string chunkText = formatRecordsText(records, processed, overlapEnd);

            Logger::session(sessionId).info("记忆提取: 待删第 {}-{} 条（共 {} 条）", processed + 1, batchEnd, toDrop);

            auto extracted = co_await extractMemories(std::move(chunkText), config.memoryExtractMaxTokens, sessionId);
            if (!extracted) {
                // API 失败：水位线停在本批之前，下条消息自动重试
                Logger::session(sessionId).warn("记忆提取失败，水位线保持 {}，下条消息将重试", chunkEndId);
                break;
            }

            chunkEndId = jsonToUInt64(records[batchEnd - 1]["id"]);

            if (extracted->empty()) {
                // 本批没有新记忆，直接推进水位线
                MemoryStore::updateShortTermMemoryWithWatermark(sessionId, existingMemory, chunkEndId);
                processed = batchEnd;
                successChunks++;
                continue;
            }

            std::vector<SimilarMemory> recalled;
            if (longTermEnabled)
                recalled = co_await recallForMerge(*extracted, sessionId);

            const auto reconcile = co_await reconcileMemory(
              splitLines(existingMemory), std::move(*extracted), std::move(recalled), longTermEnabled, sessionId);
            if (!reconcile) {
                Logger::session(sessionId).warn("记忆整理失败，水位线保持 {}，下条消息将重试", chunkEndId);
                break;
            }

            // 先写短期记忆+水位线（崩溃安全；长期记忆操作失败不阻塞记忆流程）
            existingMemory = joinLines(reconcile->shortTerm);
            MemoryStore::updateShortTermMemoryWithWatermark(sessionId, existingMemory, chunkEndId);
            processed = batchEnd;
            successChunks++;

            // 长期记忆：先插入合并结果，成功后才删除被取代的召回条目；
            // 失败不回滚——召回条目保留，下轮相关新记忆会再次召回并重新合并
            for (const auto &entry: reconcile->longTerm) {
                if (co_await LongTermMemory::addMemory(entry.content, sessionId)) {
                    for (const int64_t id: entry.sources)
                        LongTermMemoryStore::deleteMemory(id);
                    longTermAdded++;
                    longTermReplaced += static_cast<int>(entry.sources.size());
                } else {
                    Logger::session(sessionId).warn("长期记忆写入失败，保留原召回条目: {}", entry.content);
                }
            }
        }

        if (successChunks == 0) {
            co_return;
        }

        Logger::session(sessionId).info("窗口已滑动: 滑出 {} 条，水位线 -> {}，短期记忆 {} 条，长期记忆 +{}/-{}",
          processed, chunkEndId, splitLines(existingMemory).size(), longTermAdded, longTermReplaced);

        // 3. 好感度评分（独立请求，只针对本次滑出的记录）
        co_await updateAffinityFromRecords(sessionId, std::move(records), processed);

        co_return;
    }
} // namespace insoulforge
