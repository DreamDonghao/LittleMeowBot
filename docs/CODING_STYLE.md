# LittleMeowBot 项目编码规范

## 命名规范

### 文件命名

- **头文件**: `PascalCase.hpp` (如 `MemoryManager.hpp`, `ApiClient.hpp`)
- **源文件**: `PascalCase.cc` (如 `ProcessQQMessages.cc`)
- **控制器**: `PascalCaseController.cc/.h` (如 `AdminController.cc`)

### 类命名

- **PascalCase**: `MemoryManager`, `ApiClient`, `AgentSystem`
- **接口/抽象类**: 以 `I` 开头 (如 `IHandler`)
- **异常类**: 以 `Exception` 结尾 (如 `ConfigException`)

### 函数/方法命名

- **camelCase**: `getMemory()`, `processMessage()`, `sendMessage()`
- **getter**: `getName()`, `getGroupId()`
- **setter**: `setName()`, `setGroupId()`
- **bool 返回**: `isXxx()`, `hasXxx()`, `canXxx()` (如 `isEnabled()`, `hasConfig()`)

### 变量命名

- **局部变量**: `camelCase` (如 `groupId`, `messageCount`)
- **成员变量**: `m_` 前缀 + camelCase (如 `m_groupId`, `m_config`)
- **静态变量**: `s_` 前缀 + camelCase (如 `s_instance`)
- **常量**: `UPPER_SNAKE_CASE` (如 `MAX_RETRY_COUNT`)
- **全局变量**: `g_` 前缀 + camelCase (如 `g_newQQMesCounts`)

### 枚举命名

- **枚举类型**: `PascalCase` (如 `ToolCategory`)
- **枚举值**: `UPPER_SNAKE_CASE` 或 `PascalCase`

### 命名空间

- **PascalCase**: `LittleMeowBot`

### 宏命名

- **UPPER_SNAKE_CASE**: `LITTLE_MEOW_BOT_MEMORY_MANAGER_HPP`

## Doxygen 注释规范

### 文件注释

```cpp
/// @file MemoryManager.hpp
/// @brief 短期记忆管理器
/// @author donghao
/// @date 2026-04-02
```

### 类注释

```cpp
/// @brief 短期记忆管理类
/// @details 管理单个群组的短期记忆，支持存储和检索操作
class MemoryManager {
    // ...
};
```

### 方法注释

```cpp
/// @brief 获取群的短期记忆
/// @param groupId 群号
/// @return 短期记忆文本，每行一条记忆条目
[[nodiscard]] std::string getMemory(uint64_t groupId) const;
```

### 成员变量注释

```cpp
uint64_t m_groupId;  ///< 群号
std::string m_name;  ///< 群名称
```

## 代码风格

### 头文件保护

使用 `#pragma once` 而非传统宏保护。

### 包含顺序

1. 标准库头文件
2. 第三方库头文件
3. 项目内部头文件

```cpp
#include <string>
#include <vector>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <config/Config.hpp>
#include <storage/Database.hpp>
```

### 自动格式化

建议使用 clang-format，配置参考：

```yaml
BasedOnStyle: Google
IndentWidth: 4
ColumnLimit: 120
```