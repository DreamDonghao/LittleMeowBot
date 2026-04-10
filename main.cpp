/// @file main.cpp
/// @brief 程序入口 - LittleMeowBot 主程序
/// @author donghao
/// @date 2026-04-02
/// @details 初始化并启动 QQ 群聊机器人服务：
///          - 数据库初始化：SQLite 持久化存储
///          - 配置加载：从数据库读取 LLM、知识库、QQ Bot 配置
///          - Agent 系统初始化：注册内置工具和自定义工具
///          - HTTP 服务启动：监听 7778 端口，提供管理界面和 API
///          支持通过输入 "quit" 命令优雅退出

#include <iostream>
#include <drogon/drogon.h>
#include <spdlog/spdlog.h>
#include <util/Log.hpp>
#include <storage/Database.hpp>
#include <config/Config.hpp>
#include <agent/AgentSystem.hpp>
#include <model/QQMessage.hpp>

int main(){
    using namespace LittleMeowBot;
    try {
        // 系统初始化
        Log::init(true, "../logs/bot.log", true);
        auto& database = Database::instance();
        database.initialize("../data/little_meow_bot.db");

        auto& config = LittleMeowBot::Config::instance();
        config.loadFromDatabase();

        // 初始化 QQ 昵称
        QQMessage::setCustomQQName(config.selfQQNumber, config.botName + "(我)");

        // 初始化 Agent 系统
        AgentSystem::instance().initialize();

        Log::info("系统初始化完成 - 启用群: {}, 管理员: {}", database.getEnabledGroups().size(), database.getAdmins().size());

        // 启动服务
        // 启动 quit 线程
        std::jthread quit([]() {
            std::string input;
            while (std::cin >> input) {
                if (input == "quit") {
                    drogon::app().quit();
                    return;
                }
            }
        });

        drogon::app().addListener("0.0.0.0", 7778);
        drogon::app().setDocumentRoot("../public");
        Log::info("HTTP服务启动，端口: 7778");
        Log::info("管理后台: http://localhost:7778/index.html");

        drogon::app().run();

        // 清理
        database.close();
        Log::info("系统正常退出");
    } catch (const std::exception& e) {
        Log::fatal("程序崩溃: {}", e.what());
        Log::shutdown();
        return 1;
    } catch (...) {
        Log::fatal("程序崩溃: 未知错误");
        Log::shutdown();
        return 1;
    }
    Log::shutdown();
    return 0;
}
