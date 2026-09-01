# InSoulForge

一个基于 Agent 架构的智能 QQ 群聊机器人后端。

QQ交流群：1097487360


> - ✅ 个人和非商业组织免费使用
> - ❌ **禁止商业使用**
> - ❌ **禁止收费代挂 QQ 机器人服务**
> - 📧 商业授权联系：dreamdonghao@outlook.com

## ✨ 特性

- **Web 管理后台** - 可视化查看与配置
- **智能对话** - 两层 Agent 架构（Router + Executor），可自行判断是非需要回复
- **好感度** - 可以根据对话自动调整对某人的好感度
- **图片识别** - 可单独配置识图模型来识别群聊图片内容
- **自定义角色** - 可设置 Bot 的提示词来定制人设和性格
- **分页滑动上下文** - 按配置滑动提取，权衡上下文长度与缓存命中
- **智能记忆** - 记忆自动提炼，记住群友的喜好、习惯、重要事件
- **长期记忆召回** - 长期记忆本地向量化（SQLite 存储 + 余弦相似度检索），可选配 Embedding 模型
- **适配QQ功能** - Bot 可自行收藏发送表情包、引用、@群友、撤回消息、拍一拍、禁言
- **自定义工具** - 支持 Python 脚本 / HTTP 接口，可视化配置，一键导入导出
- **定时任务** - 提供低耦合的定时任务功能，可定时发送消息以及与自定义工具组合使用

## 🚀 快速开始

### 前置要求

本机器人需要配合 **OneBot** 协议实现使用，推荐使用 **napcat**

选择一个 OneBot 实现，部署并启用 HTTP 服务。

消息接收端口：7778

### 方式一：Docker 部署（推荐）

镜像已发布至 Docker Hub 和 GitHub Container Registry，支持 **amd64 / arm64** 架构：

```bash
# Docker Hub
docker pull dreamdonghao/insoulforge:latest

# 或 GitHub Container Registry
docker pull ghcr.io/dreamdonghao/insoulforge:latest
```

启动容器：

```bash
docker run -d --name insoulforge \
  -p 7778:7778 \
  -v ./data:/app/data \
  -v ./logs:/app/logs \
  -v ./uploads:/app/uploads \
  dreamdonghao/insoulforge:latest
```

> 数据库、日志和表情包分别挂载到宿主机的 `./data`、`./logs`、`./uploads` 目录（也可自定义目录），升级镜像时数据不会丢失。

> 如果 napcat 也运行在 Docker 中，注意容器内的 `127.0.0.1` 指向容器自身。启动后让两个容器加入同一个 docker
> 网络，然后用容器名互访（无需重建容器，connect 直接生效）：
>
> ```bash
> docker network create bot-net
> docker network connect bot-net napcat       # napcat 换成你的 napcat 容器名
> docker network connect bot-net insoulforge
> ```
>
> 然后在管理后台将 OneBot 的 HTTP 服务地址填为 `http://napcat:3000`（容器名 + napcat HTTP 端口），napcat
> 的上报地址填 `http://insoulforge:7778/`。

然后访问管理后台：`http://localhost:7778/index.html`

### 方式二：本地部署

1. 下载安装包并解压

2. 在项目根目录下运行run.sh

   ```sudo bash run.sh```

3. 访问管理后台：`http://localhost:7778/index.html`

### 首次配置

在管理后台完成以下配置：

1. **OneBot 配置** - 填写连接参数
    - Access Token
    - Bot QQ 号
    - HTTP 服务地址
    - Bot 名称

2. **LLM 配置** - 配置模型 API
    - 支持 Router / Executor / Memory / Image 分别配置不同模型
    - 兼容 OpenAI API 格式

3. **启用群聊** - 添加要启用的 QQ 群或用户

## 📖 使用指南

推荐使用web页面进行配置

### 群聊命令

在群中 @机器人 发送命令（私聊无需 @，直接发送即可）：

| 命令                | 说明                                   | 权限   |
|---------------------|----------------------------------------|--------|
| `/help`             | 显示帮助                               | 所有人 |
| `/status`           | 查看当前会话状态                       | 所有人 |
| `/admins`           | 查看管理员列表                         | 所有人 |
| `/about`            | 关于本项目                             | 所有人 |
| `/enable [会话ID]`  | 启用会话（群聊传群号，私聊可不带参数） | 管理员 |
| `/disable [会话ID]` | 禁用会话（私聊可不带参数）             | 管理员 |
| `/groups`           | 查看已启用的会话列表                   | 管理员 |
| `/addadmin <QQ号>`  | 添加管理员                             | 管理员 |
| `/deladmin <QQ号>`  | 移除管理员                             | 管理员 |
| `/listemoji`        | 查看QQ收藏表情列表                     | 管理员 |
| `/delemoji <名称>`  | 从QQ收藏表情中删除                     | 管理员 |

命令支持中文别名，如 `/帮助`、`/状态`、`/启用`。

## ⚙️ 配置说明

### OneBot 配置

| 参数          | 说明                 |
|---------------|----------------------|
| Access Token  | OneBot API 访问令牌  |
| Bot QQ 号     | 机器人自身的 QQ 号   |
| HTTP 服务地址 | OneBot HTTP 服务地址 |
| Bot 名称      | 机器人在群聊中的名称 |

### LLM 配置

各模型可分别配置：

| 模型         | 用途             | 建议配置              |
|--------------|------------------|-----------------------|
| Router       | 快速路由决策     | 轻量模型，低温度      |
| Executor     | 生成回复         | 主力模型，较高温度    |
| Executor思考 | 深度思考（可选） | 推理模型，如 DeepSeek |
| Memory       | 记忆提取与合并   | 轻量模型              |
| Image        | 图片内容识别     | 多模态模型            |

**思考模式**：启用 Executor思考 后，机器人会先用推理模型分析问题，再交给 Executor 生成回复，适合复杂问题的深度思考。

### 记忆与上下文参数

- **上下文触发条数** - 上下文窗口超过该条数时提取，默认 100
- **上下文保留条数** - 触发后保留的最近消息条数，默认 50
- **Router 子窗口触发** - Router 子窗口批量滑动条数，默认 20
- **Router 子窗口保留** - Router 子窗口保留条数，默认 10
- **短期记忆上限** - 触发迁移的阈值，默认 15
- **每次迁移条数** - 迁移到长期记忆的数量，默认 5

### 提示词定制

在管理后台可修改 Router 与 Executor 的系统提示词，支持 `{botName}` 占位符自动替换。

## 🔧 高级功能

### 自定义工具

支持通过 Python 脚本或 HTTP 接口扩展机器人能力：

1. 进入管理后台 → 自定义工具
2. 点击"添加工具"或"导入"
3. 填写工具名称、描述、参数定义和执行方式（Python 脚本 / HTTP 请求）
4. 保存并启用

工具支持：

- **参数定义** - JSON Schema 格式，LLM 自动理解
- **Python 脚本** - 通过 `sys.argv[1]` 接收参数 JSON 文件路径，可自定义 Python 解释器路径
- **HTTP 接口** - 配置请求地址、方法、参数模板
- **说明文档** - Markdown 格式，记录作者、用法、联系方式
- **导入导出** - JSON 文件格式，方便分享

#### 内置工具配置文件

项目提供了一些常用工具配置，位于 `agentTools/` 目录：

| 文件               | 功能       | 依赖                |
|--------------------|------------|---------------------|
| `random.json`      | 随机数生成 | 无                  |
| `get_time.json`    | 获取时间   | 无                  |
| `get_weather.json` | 天气查询   | 无                  |
| `search_web.json`  | 网络搜索   | `duckduckgo-search` |

导入方法：

1. 管理后台 → 自定义工具 → 导入
2. 上传 JSON 文件

### 配置 Embedding（可选）

1. 准备 OpenAI 兼容的 Embedding 服务（如硅基流动、OpenAI）
2. 管理后台 → LLM配置 → Embedding，填写 API 配置

配置后，短期记忆迁移到长期记忆时自动向量化入库，`recall_memory` 工具按余弦相似度召回。未配置时长期记忆自动停用，仅保留短期记忆。

---

## 🛠️ 开发者指南

本项目使用 **C++ **实现，Web页面使用**Vue3**

详细内容请转至[开发文档](./docs/DEVELOPMENT.md)

---

## 📄 许可证

AGPL-3.0 with Additional Terms

详见 [LICENSE](LICENSE)。

---

Made with by [DreamDonghao](https://github.com/DreamDonghao)