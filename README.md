# LittleMeowBot 🐱

一个智能 QQ 群聊机器人，基于 Agent 架构，支持自定义角色、长期记忆、多工具调用。

QQ交流群：1097487360

> ⚠️ **使用约束**
> - ✅ 个人和非商业组织免费使用
> - ❌ **禁止商业使用**
> - ❌ **禁止收费代挂 QQ 机器人服务**
> - 📧 商业授权联系：dreamdonghao@outlook.com

## ✨ 特性

- **智能对话** - Agent 架构，理解上下文，智能回复
- **自定义角色** - 可设置 Bot 名称和人设
- **长期记忆** - 记住群友的喜好、习惯、重要事件
- **自定义工具** - 支持 Python 脚本，可视化配置，一键导入导出
- **图片识别** - 自动识别群聊图片内容
- **Web 管理后台** - 可视化配置，实时查看聊天记录
- **知识库对接** - 支持 RAGFlow，可接入自定义知识库

## 🚀 快速开始

### 前置要求

本机器人需要配合 **OneBot** 协议实现使用，推荐：

- [Lagrange](https://github.com/LagrangeDev/Lagrange.Core) - .NET 实现
- [NapCat](https://github.com/NapNeko/NapCatQQ) - 基于 NTQQ
- [LLOneBot](https://github.com/LLOneBot/LLOneBot) - NTQQ 插件
- [OpenShamrock](https://github.com/WhiteMinds/LiteLoaderBDS) - 安卓 QQ

选择一个 OneBot 实现，部署并启用 HTTP 服务。

接收端口7778

### 启动步骤

1. 下载安装包并解压

2. 在exe目录下运行run.sh

   ```sudo bash run.sh```

3. 访问管理后台：`http://localhost:7778/admin.html`

### 首次配置

在管理后台完成以下配置：

1. **OneBot 配置** - 填写连接参数
    - Access Token
    - Bot QQ 号
    - HTTP 服务地址
    - Bot 名称（可自定义）

2. **LLM 配置** - 配置模型 API
    - 支持三层 Agent 分别配置不同模型
    - 兼容 OpenAI API 格式

3. **启用群聊** - 添加要启用的 QQ 群

## 📖 使用指南

### 群聊命令

在群中 @机器人 发送命令：

| 命令                | 说明    | 权限  |
|-------------------|-------|-----|
| `/help`           | 显示帮助  | 所有人 |
| `/status`         | 查看群状态 | 所有人 |
| `/admins`         | 查看管理员 | 所有人 |
| `/enable [群号]`    | 启用群聊  | 管理员 |
| `/disable [群号]`   | 禁用群聊  | 管理员 |
| `/groups`         | 启用群列表 | 管理员 |
| `/addadmin <QQ号>` | 添加管理员 | 管理员 |
| `/deladmin <QQ号>` | 移除管理员 | 管理员 |

### 机器人能力

- 💬 **闲聊** - 自然对话，像真人一样
- ❓ **回答问题** - 学习、技术、生活问题
- 🌤️ **查天气** - "北京天气怎么样"
- 🔍 **网络搜索** - "搜索最近的新闻"
- 🎲 **掷骰子/随机** - "掷骰子" / "随机1-100"
- 🎭 **抽奖/选择** - "从A、B、C中抽一个"
- ⏰ **查时间** - "现在几点"
- 👤 **@群友** - 自动识别并转换
- 💨 **拍一拍** - "拍一拍某人" 轻松互动
- 📝 **回复引用** - 引用回复特定消息
- ↩️ **撤回消息** - 撤回刚才发的消息
- 🔇 **禁言** - 可配置权限
- 🧠 **记忆** - 记住群友喜好，长期不忘
- 📚 **知识库** - 查询已配置的专业知识

### 记忆系统

机器人有两层记忆：

| 类型   | 存储      | 用途          |
|------|---------|-------------|
| 短期记忆 | 本地数据库   | 最近发生的事，快速回忆 |
| 长期记忆 | RAGFlow | 重要信息，永久保存   |

当短期记忆积累到上限，机器人会自动筛选重要信息存入长期记忆。

## ⚙️ 配置说明

### OneBot 配置

| 参数           | 说明               |
|--------------|------------------|
| Access Token | OneBot API 访问令牌  |
| Bot QQ 号     | 机器人自身的 QQ 号      |
| HTTP 服务地址    | OneBot HTTP 服务地址 |
| Bot 名称       | 机器人在群聊中的名称       |

### LLM 配置

多层 Agent 可分别配置：

| Agent            | 用途         | 建议配置        |
|------------------|------------|-------------|
| Router           | 快速路由决策     | 轻量模型，低温度    |
| Planner          | 意图分析规划     | 中等模型        |
| Executor         | 生成回复       | 主力模型，较高温度   |
| ExecutorThinking | 深度思考（可选）  | 推理模型，如 DeepSeek |

**思考模式**：启用 ExecutorThinking 后，机器人会先用推理模型分析问题，再生成回复，适合复杂问题的深度思考。

### 记忆参数

| 参数      | 说明          | 默认值 |
|---------|-------------|-----|
| 记忆触发间隔  | 每 N 条消息生成记忆 | 16  |
| 聊天记录保留数 | 保留的记录数量     | 18  |
| 短期记忆上限  | 触发迁移的阈值     | 15  |
| 每次迁移条数  | 迁移到长期记忆的数量  | 5   |

### 提示词定制

在管理后台可修改 Agent 提示词，支持 `{botName}` 占位符自动替换。

## 🔧 高级功能

### 自定义工具

支持通过 Python 脚本扩展机器人能力：

1. 进入管理后台 → 自定义工具
2. 点击"添加工具"或"导入"
3. 填写工具名称、描述、参数定义和 Python 脚本
4. 保存并启用

工具支持：
- **参数定义** - JSON Schema 格式，LLM 自动理解
- **Python 脚本** - 通过 `sys.argv[1]` 接收参数 JSON 文件路径
- **说明文档** - Markdown 格式，记录作者、用法、联系方式
- **导入导出** - JSON 文件格式，方便分享

#### 内置工具配置文件

项目提供了一些常用工具配置，位于 `agentTools/` 目录：

| 文件 | 功能 | 依赖 |
|------|------|------|
| `random.json` | 随机数生成 | 无 |
| `get_time.json` | 获取时间 | 无 |
| `get_weather.json` | 天气查询 | 无 |
| `search_web.json` | 网络搜索 | `duckduckgo-search` |

导入方法：
1. 管理后台 → 自定义工具 → 导入
2. 上传 JSON 文件

### 对接知识库

1. 部署 RAGFlow 服务
2. 创建知识库 Dataset
3. 在管理后台填写 RAGFlow API 配置

机器人即可通过 `search_knowledge` 工具查询知识库。

## 📁 数据存储

所有数据存储在 `data/little_meow_bot.db`：

- 聊天记录
- 短期/长期记忆
- 群配置
- 管理员列表
- LLM 配置
- 自定义工具
- 提示词

---

## 🛠️ 开发者指南

### 环境要求

- Linux (Ubuntu 22.04 推荐)
- GCC 11+ 或 Clang 14+
- CMake 3.5+

### 安装依赖

```bash
sudo apt update && sudo apt install -y \
    cmake g++ \
    libdrogon-dev \
    libspdlog-dev \
    libfmt-dev \
    libsqlite3-dev \
    libjsoncpp-dev \
    libssl-dev \
    libbrotli-dev \
    zlib1g-dev \
    libuuid1
```

### 编译

```bash
git clone https://github.com/DreamDonghao/LittleMeowBot.git
cd LittleMeowBot
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### 项目结构

```
LittleMeowBot/
├── main.cpp                 # 入口
├── controllers/             # HTTP 控制器
│   ├── ProcessQQMessages.cc # 消息处理
│   ├── AdminController.cc   # 管理 API
│   └── AdminWebSocket.cc    # WebSocket
├── agentTools/              # 工具配置文件
│   ├── random.json
│   ├── get_time.json
│   ├── get_weather.json
│   └── search_web.json
└── include/
    ├── agent/               # Agent 系统
    ├── config/              # 配置管理
    ├── model/               # 数据模型
    ├── service/             # 服务层
    └── storage/             # 数据存储
```

### 添加自定义工具

#### 方式一：Web 管理后台（推荐）

在管理后台可视化配置，支持 Python 脚本。

#### 方式二：C++ 内置工具

编辑 `src/agent/AgentToolManager.cpp`：

```cpp
registry.registerTool(
    {
        .name = "my_tool",
        .description = "工具描述",
        .parameters = paramsJson,
        .handler = [](const Json::Value& args) -> drogon::Task<std::string> {
            co_return "结果";
        }
    }, ToolCategory::ACTION);
```

### 工具 JSON 格式

```json
{
  "name": "tool_name",
  "description": "工具描述，LLM 会根据此判断何时调用",
  "parameters": {
    "type": "object",
    "properties": {
      "param1": {
        "type": "string",
        "description": "参数说明"
      }
    },
    "required": ["param1"]
  },
  "scriptContent": "import json\nimport sys\n\n# 参数从 sys.argv[1] 传入的 JSON 文件读取\nwith open(sys.argv[1]) as f:\n    args = json.load(f)\n\nprint(args['param1'])",
  "readme": "# 工具说明\n\n作者、用法、联系方式等"
}
```

### 添加新 API

在 `controllers/AdminController.h` 中声明，在 `AdminController.cc` 中实现。

---

## 📄 许可证

AGPL-3.0 with Additional Terms

详见 [LICENSE](LICENSE)。

---

Made with  by [DreamDonghao](https://github.com/DreamDonghao)
