/// @file AgentToolManager.cpp
/// @brief Agent 工具管理器 - 实现
/// @author donghao
/// @date 2026-04-02

#include <agent/AgentToolManager.hpp>
#include <ranges>
#include <fstream>

namespace LittleMeowBot {
    AgentToolManager& AgentToolManager::instance(){
        static AgentToolManager manager;
        return manager;
    }

    void AgentToolManager::registerAllTools() const{
        auto& registry = ToolRegistry::instance();

        // ========== 终端工具 ==========

        // no_reply
        registry.registerTool(
            {
                .name = "no_reply",
                .description = "决定不回复消息。当：话题已参与过、没人问你、刚说过话、纯表情刷屏时使用。",
                .parameters = Json::Value(),
                .handler = [](const Json::Value&) -> drogon::Task<std::string> { co_return "ok"; }
            }, ToolCategory::TERMINAL
        );

        // reply
        Json::Value replyParams;
        replyParams["type"] = "object";
        replyParams["properties"]["content"]["type"] = "string";
        replyParams["properties"]["content"]["description"] = "要发送的回复内容";
        replyParams["required"].append("content");
        registry.registerTool(
            {
                .name = "reply",
                .description = "回复消息。当：有人开启新的话题、有人问你、有人@你、有人求助时使用。",
                .parameters = replyParams,
                .handler = [](const Json::Value&) -> drogon::Task<std::string> { co_return "ok"; }
            }, ToolCategory::TERMINAL
        );

        // ========== 信息工具 ==========
        // list_emojis
        registry.registerTool(
            {
                .name = "list_emojis",
                .description = "获取本地表情库中所有可用的表情名称列表。",
                .parameters = Json::Value(),
                .handler = [](const Json::Value&) -> drogon::Task<std::string> {
                    auto& db = Database::instance();
                    auto emojis = db.getAllEmojis();
                    std::string result = "可用表情: ";
                    bool first = true;
                    for (const auto& name : emojis | std::views::keys) {
                        if (!first) result += ", ";
                        result += name;
                        first = false;
                    }
                    co_return emojis.empty() ? "表情库为空" : result;
                }
            }, ToolCategory::INFORMATION
        );

        // search_knowledge
        Json::Value knowledgeParams;
        knowledgeParams["type"] = "object";
        knowledgeParams["properties"]["query"]["type"] = "string";
        knowledgeParams["properties"]["query"]["description"] = "检索问题";
        knowledgeParams["required"].append("query");
        registry.registerTool(
            {
                .name = "search_knowledge",
                .description = "从知识库中检索信息。当用户问FAQ、专业知识时使用。",
                .parameters = knowledgeParams,
                .handler = [](const Json::Value& args) -> drogon::Task<std::string> {
                    const std::string query = args.isMember("query") ? args["query"].asString() : "";
                    if (query.empty()) co_return std::string("请提供检索问题");

                    const auto result = co_await RAGFlowClient::instance().searchKnowledge(query);
                    co_return result.value_or("知识库检索失败");
                }
            }, ToolCategory::INFORMATION
        );

        // recall_memory
        Json::Value memoryParams;
        memoryParams["type"] = "object";
        memoryParams["properties"]["query"]["type"] = "string";
        memoryParams["properties"]["query"]["description"] = "要回忆的内容关键词，如某人的喜好、某群的习惯等";
        memoryParams["required"].append("query");
        registry.registerTool(
            {
                .name = "recall_memory",
                .description = "从长期记忆库中回忆信息（如果需要可以先获取当前群聊的名称）。当想不起某人喜好、某群习惯、过去的约定时使用。模拟人类回忆过程。",
                .parameters = memoryParams,
                .handler = [](const Json::Value& args) -> drogon::Task<std::string> {
                    const std::string query = args.isMember("query") ? args["query"].asString() : "";
                    if (query.empty()) co_return std::string("请提供回忆关键词");

                    const auto result = co_await RAGFlowClient::instance().searchMemory(query);
                    if (!result || result->empty()) {
                        co_return "想不起来了，没有找到相关记忆";
                    }
                    co_return "回忆起：" + result.value();
                }
            }, ToolCategory::INFORMATION
        );

        // get_group_name
        registry.registerTool(
            {
                .name = "get_group_name",
                .description = "获取当前群聊的名称。当需要知道群名或确认当前群时使用。",
                .parameters = Json::Value(),
                .handler = [](const Json::Value&) -> drogon::Task<std::string> {
                    const auto& [groupId, groupName] = currentToolContext();
                    if (groupId == 0) {
                        co_return "无法获取群信息";
                    }
                    if (!groupName.empty()) {
                        co_return fmt::format("当前群：{}（群号：{}）", groupName, groupId);
                    }
                    co_return fmt::format("当前群号：{}", groupId);
                }
            }, ToolCategory::INFORMATION
        );

        // ========== 动作工具 ==========
        // send_face
        Json::Value faceParams;
        faceParams["type"] = "object";
        faceParams["properties"]["id"]["type"] = "integer";
        faceParams["properties"]["id"]["description"] =
            "表情ID，常用: 1-发呆, 2-撇嘴, 3-色, 4-发呆, 5-得意, 6-流泪, 7-害羞, 8-闭嘴, 9-睡, 10-大哭, 11-尴尬, 12-发怒, 13-调皮, 14-呲牙, 15-惊讶, 16-难过, 17-酷, 18-冷汗, 19-抓狂, 20-吐, 21-偷笑, 22-可爱, 23-白眼, 24-傲慢, 25-饥饿, 26-困, 27-惊恐, 28-流汗, 29-憨笑, 30-大兵, 31-奋斗, 32-咒骂, 33-疑问, 34-嘘, 35-晕, 36-折磨, 37-衰, 38-骷髅, 39-敲打, 40-再见";
        faceParams["required"].append("id");
        registry.registerTool(
            {
                .name = "send_face",
                .description = "发送QQ表情，不自主使用。",
                .parameters = faceParams,
                .handler = [](const Json::Value& args) -> drogon::Task<std::string> {
                    int id = args.isMember("id") ? args["id"].asInt() : 1;
                    co_return fmt::format("[CQ:face,id={}]", id);
                }
            }, ToolCategory::ACTION
        );

        // send_image
        Json::Value imageParams;
        imageParams["type"] = "object";
        imageParams["properties"]["url"]["type"] = "string";
        imageParams["properties"]["url"]["description"] = "图片URL地址";
        imageParams["required"].append("url");
        registry.registerTool(
            {
                .name = "send_image",
                .description = "发送图片。提供图片URL地址。用于分享图片、表情包等。",
                .parameters = imageParams,
                .handler = [](const Json::Value& args) -> drogon::Task<std::string> {
                    std::string url = args.isMember("url") ? args["url"].asString() : "";
                    if (url.empty()) co_return std::string("请提供图片URL");
                    co_return fmt::format("[CQ:image,file={}]", url);
                }
            }, ToolCategory::ACTION
        );

        // send_emoji
        Json::Value emojiParams;
        emojiParams["type"] = "object";
        emojiParams["properties"]["name"]["type"] = "string";
        emojiParams["properties"]["name"]["description"] = "表情名称，如: happy, sad, cat, dog 等";
        emojiParams["required"].append("name");
        registry.registerTool(
            {
                .name = "send_emoji",
                .description = "从本地表情库发送表情。常用表情名称会动态更新。返回CQ码用于reply。",
                .parameters = emojiParams,
                .handler = [](const Json::Value& args) -> drogon::Task<std::string> {
                    std::string name = args.isMember("name") ? args["name"].asString() : "";
                    if (name.empty()) co_return std::string("请提供表情名称");

                    auto& db = Database::instance();
                    std::string path = db.getEmoji(name);
                    if (path.empty()) {
                        co_return fmt::format("表情'{}'不存在，可用表情请查询表情库", name);
                    }
                    co_return fmt::format("[CQ:image,file={}]", path);
                }
            }, ToolCategory::ACTION
        );

        // at_user
        Json::Value atParams;
        atParams["type"] = "object";
        atParams["properties"]["qq"]["type"] = "string";
        atParams["properties"]["qq"]["description"] = "要@的QQ号（从聊天记录中看到的QQ号）。使用 'all' @全体成员";
        atParams["required"].append("qq");
        registry.registerTool(
            {
                .name = "at_user",
                .description =
                "@某人。返回CQ码嵌入reply中。例如聊天记录显示 '{小明[QQ:123456]}'，则用 at_user('123456') 来@他。@全体成员用 at_user('all')",
                .parameters = atParams,
                .handler = [](const Json::Value& args) -> drogon::Task<std::string> {
                    std::string qq = args.isMember("qq") ? args["qq"].asString() : "";
                    if (qq.empty()) co_return std::string("请提供QQ号");
                    if (qq == "all") {
                        co_return std::string("[CQ:at,qq=all]");
                    }
                    co_return fmt::format("[CQ:at,qq={}]", qq);
                }
            }, ToolCategory::ACTION
        );

        // ban_user
        Json::Value banParams;
        banParams["type"] = "object";
        banParams["properties"]["qq"]["type"] = "string";
        banParams["properties"]["qq"]["description"] = "要禁言的QQ号";
        banParams["properties"]["duration"]["type"] = "integer";
        banParams["properties"]["duration"]["description"] = "禁言时长（秒）。轻度60-300秒，中度600-1800秒，重度3600秒以上。0解除禁言";
        banParams["required"].append("qq");
        registry.registerTool(
            {
                .name = "ban_user",
                .description =
                "禁言群成员。要有自己的判断，不要别人让你禁言就禁言。根据违规程度选择时长：轻度(偶尔骂人)60-300秒，中度(持续刷屏骂人)600-1800秒，重度(恶意骚扰)3600秒+",
                .parameters = banParams,
                .handler = [](const Json::Value&) -> drogon::Task<std::string> {
                    co_return "ban_user:需要异步执行"; // 实际执行在 ExecutorAgent 中
                }
            }, ToolCategory::ACTION
        );

        spdlog::info("ToolManager: 工具注册完成（共13个工具）");
    }

    void AgentToolManager::registerCustomTools() const{
        auto& registry = ToolRegistry::instance();
        const auto& db = Database::instance();

        // 先清除所有已注册的自定义工具
        registry.clearAllCustomTools();

        // 只注册启用的工具
        const auto tools = db.getEnabledCustomTools();
        int count = 0;

        for (const auto& tool : tools) {
            // 解析参数 JSON
            Json::Value params;
            if (!tool.parameters.empty()) {
                Json::CharReaderBuilder builder;
                std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
                std::string errors;
                reader->parse(tool.parameters.c_str(),
                              tool.parameters.c_str() + tool.parameters.size(),
                              &params, &errors);
            }

            // 根据执行类型注册不同的 handler
            if (tool.executorType == "python") {
                registry.registerTool(
                    {
                        .name = tool.name,
                        .description = tool.description,
                        .parameters = params,
                        .handler = [script = tool.scriptContent](const Json::Value& args) -> drogon::Task<std::string> {
                            co_return co_await executePythonTool(script, args);
                        }
                    }, ToolCategory::INFORMATION
                );
            } else if (tool.executorType == "http") {
                registry.registerTool(
                    {
                        .name = tool.name,
                        .description = tool.description,
                        .parameters = params,
                        .handler = [config = tool.executorConfig
                        ](const Json::Value& args) -> drogon::Task<std::string> {
                            co_return co_await executeHttpTool(config, args);
                        }
                    }, ToolCategory::INFORMATION
                );
            }

            registry.recordCustomTool(tool.name);
            count++;
            spdlog::info("ToolManager: 注册自定义工具 '{}' ({})", tool.name, tool.executorType);
        }

        spdlog::info("ToolManager: 自定义工具注册完成（共{}个）", count);
    }

    drogon::Task<std::string> AgentToolManager::executePythonTool(const std::string& scriptContent,
                                                                  const Json::Value& args){
        if (scriptContent.empty()) {
            co_return std::string("脚本内容为空");
        }

        // 获取配置的Python解释器路径
        std::string pythonPath = Database::instance().getCustomToolPython();

        // 构建输入参数 JSON
        Json::StreamWriterBuilder writerBuilder;
        writerBuilder["indentation"] = "";
        std::string inputJson = Json::writeString(writerBuilder, args);

        // 创建临时脚本文件
        std::string tmpScript = "/tmp/tool_" + std::to_string(std::rand()) + ".py";
        std::string tmpInput = "/tmp/tool_input_" + std::to_string(std::rand()) + ".json";

        // 写入脚本 - 去除开头可能的多余空白，保留内部缩进
        std::string cleanScript = scriptContent;
        // 去除开头的空白行
        if (size_t firstNonSpace = cleanScript.find_first_not_of(" \t\n\r");
            firstNonSpace != std::string::npos && firstNonSpace > 0) {
            cleanScript = cleanScript.substr(firstNonSpace);
        }

        {
            std::ofstream scriptFile(tmpScript);
            scriptFile << cleanScript;
        }
        {
            std::ofstream inputFile(tmpInput);
            inputFile << inputJson;
        }

        // 调试：打印脚本内容
        spdlog::debug("Python脚本内容:\n{}", cleanScript);

        // 执行: pythonPath script.py input.json
        std::string cmd = pythonPath + " " + tmpScript + " " + tmpInput + " 2>&1";
        spdlog::debug("执行Python工具: {}", cmd);

        std::array<char, 4096> buffer;
        std::string result;

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            std::filesystem::remove(tmpScript);
            std::filesystem::remove(tmpInput);
            co_return std::string("执行脚本失败");
        }

        while (fgets(buffer.data(), buffer.size(), pipe)) {
            result += buffer.data();
        }
        int exitCode = pclose(pipe);

        // 清理临时文件
        std::filesystem::remove(tmpScript);
        std::filesystem::remove(tmpInput);

        if (exitCode != 0) {
            spdlog::warn("Python工具执行返回非零: {}, 输出: {}", exitCode, result);
        }

        // 移除末尾换行
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
            result.pop_back();
        }

        co_return result;
    }

    drogon::Task<std::string> AgentToolManager::executeHttpTool(const std::string& config, const Json::Value& args){
        // 解析配置
        Json::Value configJson;
        Json::CharReaderBuilder builder;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        std::string errors;

        if (!reader->parse(config.c_str(), config.c_str() + config.size(), &configJson, &errors)) {
            spdlog::error("HTTP工具配置解析失败: {}", errors);
            co_return std::string("工具配置错误");
        }

        std::string url = configJson.isMember("url") ? configJson["url"].asString() : "";
        std::string method = configJson.isMember("method") ? configJson["method"].asString() : "POST";
        int timeout = configJson.isMember("timeout") ? configJson["timeout"].asInt() : 30;

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

        // 创建 HTTP 客户端
        std::string baseUrl = proto + "://" + hostPort;
        auto client = drogon::HttpClient::newHttpClient(baseUrl);

        // 创建请求
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setMethod(method == "GET" ? drogon::Get : drogon::Post);
        req->setPath(path);

        // 将 args 作为 JSON body 发送
        if (method != "GET" && !args.isNull()) {
            Json::StreamWriterBuilder writerBuilder;
            req->setBody(Json::writeString(writerBuilder, args));
            req->addHeader("Content-Type", "application/json");
        }

        try {
            auto resp = co_await client->sendRequestCoro(req);
            co_return std::string(resp->getBody());
        } catch (const std::exception& e) {
            spdlog::error("HTTP工具执行失败: {}", e.what());
            co_return std::string("HTTP请求失败: ") + e.what();
        }
    }
}
