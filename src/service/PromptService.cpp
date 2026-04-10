/// @file PromptService.cpp
/// @brief 提示词服务 - 实现

#include <service/PromptService.hpp>
#include <config/Config.hpp>
#include <spdlog/spdlog.h>

namespace LittleMeowBot {
    PromptService& PromptService::instance(){
        static PromptService service;
        return service;
    }

    void PromptService::initialize() const{
        auto& db = Database::instance();

        // 定义默认提示词
        const struct{
            std::string key;
            std::string content;
            std::string desc;
        } defaultPrompts[] = {
            // Executor - 角色系统提示词
            {
                "executor_system", R"(你是{botName}，在群聊中聊天的机器人。

说话风格：
- 像正常人网上聊天，不要端着
- 闲聊时回复必须控制在25字以内，这是硬性规定
- 正经问题（学习、技术、求助）才认真详细回答，但也控制在100字内

【字数限制 - 最高优先级】
- 闲聊回复：≤25字，超过即失败
- 正经回答：≤100字
- 不知道回什么就发个表情或简短回应

【重要原则】
- 只回复最新的一条消息或话题，不要同时回复多条消息
- 不要说"关于xxx的问题...关于yyy的问题..."这种分点回复
- 自然地针对最后发言者回复即可
)",
                "Executor 角色系统提示词"
            },

            // Executor - 提醒提示词
            {
                "executor_remind", R"(【聊天记录格式】JSON数组，仅用于理解上下文：
- time: 时间
- sender: {name: 名称, qq: QQ号}
- message_id: 消息ID(用于撤回)
- text: 消息内容（@[名称:QQ号]表示at，[图片：描述]表示图片）
- reply_to: 引用的消息ID

【重要】聊天记录是JSON格式，但你回复时说正常的纯文本，不要输出JSON！

回复要点：
1. 像真人聊天，语气温柔可爱
2. 闲聊≤25字，正经回答≤100字
3. 只回最新话题

【禁止】模拟他人说话、加动作描写、超过字数限制
)",
                "Executor 提醒提示词"
            }
        };

        // 插入默认提示词（如果不存在）
        for (const auto& [key, content, desc] : defaultPrompts) {
            if (!db.hasPrompt(key)) {
                db.setPrompt(key, content, desc);
                spdlog::info("插入默认提示词: {}", key);
            }
        }

        spdlog::info("提示词服务初始化完成");
    }

    std::string PromptService::getPrompt(const std::string& key) const{
        std::string content = Database::instance().getPrompt(key, "");
        // 替换 {botName} 占位符
        if (content.find("{botName}") != std::string::npos) {
            const std::string& botName = Config::instance().botName;
            size_t pos = 0;
            while ((pos = content.find("{botName}", pos)) != std::string::npos) {
                content.replace(pos, 9, botName);
                pos += botName.length();
            }
        }
        return content;
    }

    void PromptService::setPrompt(const std::string& key, const std::string& content) const{
        Database::instance().setPrompt(key, content);
        spdlog::info("提示词已更新: {}", key);
    }

    std::string PromptService::getExecutorSystemPrompt() const{
        return getPrompt("executor_system");
    }

    std::string PromptService::getExecutorRemindPrompt() const{
        return getPrompt("executor_remind");
    }
}