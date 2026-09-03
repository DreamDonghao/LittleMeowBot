/// @file AgentToolManager.cpp
/// @brief Agent 工具管理器 - 实现
/// @author donghao
/// @date 2026-04-02

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

#include <agent/AgentToolManager.hpp>
#include <agent/BuiltinTools.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <random>
#include <service/OneBotClient.hpp>
#include <service/ToolRegistry.hpp>
#include <spdlog/spdlog.h>
#include <storage/ToolStore.hpp>
#include <tuple>
#include <util/HttpUtil.hpp>
#include <util/JsonUtil.hpp>

namespace insoulforge {

    void AgentToolManager::registerAllTools() {
        registerReplyTools();
        registerInfoTools();
        registerActionTools();

        spdlog::info("ToolManager: 工具注册完成（共20个内置工具）");
    }

    void AgentToolManager::registerCustomTools() {
        auto &registry = ToolRegistry::instance();

        // 先清除所有已注册的自定义工具
        registry.clearAllCustomTools();


        // 只注册启用的工具
        const auto tools = ToolStore::getEnabledCustomTools();
        int count = 0;

        for (const auto &tool: tools) {
            json params;
            if (!tool.parameters.empty()) {
                std::ignore = tryParseJson(tool.parameters, params);
                // 确保顶层 type 为 object（OpenAI function calling 要求）
                if (!params.is_null() && !params.contains("type")) {
                    params["type"] = "object";
                }
            }

            // 根据执行类型注册不同的 handler
            if (tool.executorType == "python") {
                registry.registerTool(
                  {
                    .name = tool.name,
                    .description = tool.description,
                    .parameters = params,
                    .handler = [script = tool.scriptContent](json args) -> drogon::Task<std::string> {
                        co_return co_await executePythonTool(script, std::move(args));
                    },
                  },
                  ToolCategory::INFORMATION);
            } else if (tool.executorType == "http") {
                registry.registerTool(
                  {
                    .name = tool.name,
                    .description = tool.description,
                    .parameters = params,
                    .handler = [config = tool.executorConfig](json args) -> drogon::Task<std::string> {
                        co_return co_await executeHttpTool(config, std::move(args));
                    },
                  },
                  ToolCategory::INFORMATION);
            }

            registry.recordCustomTool(tool.name);
            count++;
            spdlog::info("ToolManager: 注册自定义工具 '{}' ({})", tool.name, tool.executorType);
        }

        spdlog::info("ToolManager: 自定义工具注册完成（共{}个）", count);
    }

    drogon::Task<std::string> AgentToolManager::executePythonTool(std::string scriptContent, json args) {
        if (scriptContent.empty()) {
            co_return std::string("脚本内容为空");
        }

        // 获取配置的Python解释器路径
        std::string pythonPath = ToolStore::getCustomToolPython();

        // 构建输入参数 JSON
        const std::string inputJson = dumpJson(args, false);

        // 创建临时脚本文件（析构时自动删除）
        struct TempFile {
            std::string path;

            explicit TempFile(std::string p) : path(std::move(p)) {}

            TempFile(const TempFile &) = delete;

            TempFile &operator=(const TempFile &) = delete;

            ~TempFile() {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        };

        std::random_device rd;
        const auto tmpDir = std::filesystem::temp_directory_path();
        TempFile tmpScript((tmpDir / ("tool_" + std::to_string(rd()) + ".py")).string());
        TempFile tmpInput((tmpDir / ("tool_input_" + std::to_string(rd()) + ".json")).string());

        // 写入脚本 - 去除开头可能的多余空白，保留内部缩进
        std::string cleanScript = std::move(scriptContent);
        // 去除开头的空白行
        if (size_t firstNonSpace = cleanScript.find_first_not_of(" \t\n\r");
          firstNonSpace != std::string::npos && firstNonSpace > 0) {
            cleanScript = cleanScript.substr(firstNonSpace);
        }

        {
            std::ofstream scriptFile(tmpScript.path);
            scriptFile << cleanScript;
        }
        {
            std::ofstream inputFile(tmpInput.path);
            inputFile << inputJson;
        }

        // 调试：打印脚本内容
        spdlog::debug("Python脚本内容:\n{}", cleanScript);

        // 执行: pythonPath script.py input.json
        std::string cmd = pythonPath + " " + tmpScript.path + " " + tmpInput.path + " 2>&1";
        spdlog::debug("执行Python工具: {}", cmd);

        std::array<char, 4096> buffer{};
        std::string result;

        // RAII 管理管道：出错路径自动 pclose
        struct PipeCloser {
            void operator()(FILE *f) const noexcept {
                if (f)
                    std::ignore = pclose(f);
            }
        };
        std::unique_ptr<FILE, PipeCloser> pipe(popen(cmd.c_str(), "r"));
        if (!pipe) {
            co_return std::string("执行脚本失败");
        }

        while (fgets(buffer.data(), buffer.size(), pipe.get())) {
            result += buffer.data();
        }

        if (const int exitCode = pclose(pipe.release()); exitCode != 0) {
            spdlog::warn("Python工具执行返回非零: {}, 输出: {}", exitCode, result);
        }

        // 移除末尾换行
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
            result.pop_back();
        }

        co_return result;
    }

    drogon::Task<std::string> AgentToolManager::executeHttpTool(std::string config, json args) {
        // 解析配置
        json configJson;
        if (!tryParseJson(config, configJson)) {
            spdlog::error("HTTP工具配置解析失败");
            co_return std::string("工具配置错误");
        }

        std::string url = getStr(configJson, "url");
        std::string method = getStr(configJson, "method", "POST");

        if (url.empty()) {
            co_return std::string("未配置URL");
        }

        // 解析 URL
        // 格式: http://host:port/path 或 https://host:port/path
        size_t protoEnd = url.find("://");
        if (protoEnd == std::string::npos) {
            co_return std::string("URL格式错误");
        }
        std::string proto = url.substr(0, protoEnd);
        std::string rest = url.substr(protoEnd + 3);

        size_t pathStart = rest.find('/');
        std::string hostPort = pathStart == std::string::npos ? rest : rest.substr(0, pathStart);
        std::string path = pathStart == std::string::npos ? "/" : rest.substr(pathStart);

        // 创建 HTTP 客户端并发送（GET 无 body，其余把 args 作为 JSON body）
        std::string baseUrl = proto + "://" + hostPort;
        const bool isGet = (method == "GET");
        const auto resp = co_await HttpUtil::send("[HttpTool]", baseUrl, path, isGet ? drogon::Get : drogon::Post,
          isGet ? json() : std::move(args), "", 30.0, currentToolContext().sessionId);
        if (!resp) {
            co_return std::string("HTTP请求失败");
        }
        co_return std::string((*resp)->getBody());
    }

    namespace {
        // 收藏表情缓存（60秒TTL）
        std::mutex g_favEmojiCacheMutex;
        json g_favEmojiCache(nullptr);
        std::chrono::steady_clock::time_point g_favEmojiCacheTime{};

        // CQ码参数值不能包含逗号和方括号，替换为空格
        std::string sanitizeCqParam(std::string s) {
            for (char &c: s) {
                if (c == ',' || c == '[' || c == ']')
                    c = ' ';
            }
            return s;
        }
    } // namespace

    drogon::Task<json> AgentToolManager::fetchFavoriteEmojis(const std::optional<uint64_t> sessionId) {
        using namespace std::chrono_literals;
        {
            std::lock_guard lock(g_favEmojiCacheMutex);
            if (!g_favEmojiCache.is_null() && std::chrono::steady_clock::now() - g_favEmojiCacheTime < 60s) {
                co_return g_favEmojiCache;
            }
        }

        json result(json::array());
        const json data = co_await OneBotClient::fetchCustomFaceDetail(sessionId);

        int idx = 0;
        for (const auto &item: data) {
            json emoji;
            const std::string desc = sanitizeCqParam(getStr(item, "desc"));
            const std::string md5 = getStr(item, "md5");
            // 无名表情用 md5 前6位兜底（稳定且可区分），序号会随列表顺序漂移
            std::string fallback = md5.size() >= 6 ? "表情" + md5.substr(0, 6) : fmt::format("表情{}", idx + 1);
            emoji["name"] = desc.empty() ? fallback : desc;
            emoji["summary"] = desc;
            emoji["emoji_id"] = getStr(item, "eId");
            emoji["emoji_package_id"] = getStr(item, "epId");
            emoji["key"] = getStr(item, "key");
            emoji["url"] = getStr(item, "url");
            emoji["md5"] = md5;
            emoji["res_id"] = getStr(item, "resId");
            emoji["is_mark_face"] = getBool(item, "isMarkFace");
            result.push_back(emoji);
            idx++;
        }

        // 网络异常（resp 为空）时缓存空结果，避免高频重试
        {
            std::lock_guard lock(g_favEmojiCacheMutex);
            g_favEmojiCache = result;
            g_favEmojiCacheTime = std::chrono::steady_clock::now();
        }
        co_return result;
    }

    drogon::Task<json> AgentToolManager::findFavoriteEmoji(std::string name, const std::optional<uint64_t> sessionId) {
        for (const json emojis = co_await fetchFavoriteEmojis(sessionId); const auto &emoji: emojis) {
            if (getStr(emoji, "name") == name || getStr(emoji, "summary") == name) {
                co_return emoji;
            }
        }
        co_return json(nullptr);
    }

    void AgentToolManager::invalidateFavoriteEmojiCache() {
        std::lock_guard lock(g_favEmojiCacheMutex);
        g_favEmojiCache = json(nullptr);
    }
} // namespace insoulforge
