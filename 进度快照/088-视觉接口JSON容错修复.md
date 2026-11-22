# 进度快照 088

- 时间：2026-06-27
- 已完成内容：完成素材管理中心视觉接口 JSON 解析容错修复，解决模型在 JSON 前后追加说明、Markdown 代码块或 `<think>` 推理内容时被误判为“不是有效 JSON”的问题。
- 当前修改模块：素材管理中心视觉接口 JSON 解析容错模块。
- 阶段备份：
  - `backup/v0.1.73`
- 修改文件：
  - `dit-tools-src/cinevault-pro/src/infrastructure/network/VisionApiClient.cpp`
  - `dit-tools-src/cinevault-pro/tests/unit/VisionApiClientJsonTest.cpp`
  - `dit-tools-src/cinevault-pro/CMakeLists.txt`
  - `backup/v0.1.73/视觉接口JSON容错修复-前后对比.md`
  - `进度快照/088-视觉接口JSON容错修复.md`

## 已完成内容

- 读取最新快照 `087-素材备份最终验证打包与清理.md`，确认上一阶段素材备份首版已完成，本轮从视觉接口问题开始。
- 定位失败文案来自 `VisionApiClient.cpp` 的 `parseAssistantJson()`。
- 将原先只接受纯 JSON 或开头 Markdown 代码块的逻辑，增强为：
  - 先移除 `<think>...</think>` 推理块。
  - 优先解析 Markdown fenced JSON。
  - 兜底扫描文本中的 JSON 对象。
  - 多个合法对象时优先使用最后一个，兼容“先复述 schema，最后给实际结果”的模型回复。
- 新增 `VisionApiClientJsonTest`，覆盖纯 JSON、Markdown JSON、前后缀文本、schema 后实际 JSON、推理标签后 JSON、无 JSON 失败、content 数组文本等场景。
- 完成真实工作流 Release 构建、CTest、安装包打包、5 秒启动烟测和空 staging 清理。

## 当前修改到哪个模块

- 当前完成模块：素材管理中心视觉接口 JSON 解析容错。
- 下一模块：等待真实视觉接口复核；如仍失败，再进入“原始响应截断诊断日志”模块。

## 代码前后对比

### 1. 视觉接口 JSON 抽取

修改前：

```cpp
QString extractJsonBlock(QString text)
{
    text = text.trimmed();
    if (text.startsWith(QStringLiteral("```"))) {
        const auto firstBrace = text.indexOf(QLatin1Char('{'));
        const auto lastBrace = text.lastIndexOf(QLatin1Char('}'));
        if (firstBrace >= 0 && lastBrace > firstBrace) {
            return text.mid(firstBrace, lastBrace - firstBrace + 1);
        }
    }
    return text;
}
```

修改后：

```cpp
bool parsesJsonObject(const QString &text);
QStringList extractJsonObjectCandidates(const QString &text);
QString stripReasoningBlocks(QString text);

QString extractJsonBlock(QString text)
{
    text = stripReasoningBlocks(text);
    // 优先解析 fenced JSON，再扫描全文 JSON 对象。
    ...
}
```

### 2. 测试覆盖

修改前：

```text
没有 VisionApiClient JSON 容错测试。
```

修改后：

```cpp
parseAssistantJson_acceptsPureJson();
parseAssistantJson_acceptsMarkdownJson();
parseAssistantJson_acceptsJsonWithTextAroundIt();
parseAssistantJson_prefersLastValidObject();
parseAssistantJson_acceptsJsonAfterReasoningTag();
parseAssistantJson_rejectsContentWithoutJsonObject();
parseAssistantJson_acceptsTextContentArray();
```

### 3. CTest 接入

修改前：

```cmake
add_test(NAME CineVaultSmokeTest COMMAND CineVaultSmokeTest)
add_test(NAME BackupPlannerTest COMMAND BackupPlannerTest)
```

修改后：

```cmake
add_test(NAME VisionApiClientJsonTest COMMAND VisionApiClientJsonTest)
```

## 验证结果

- 阶段备份：`bash tool/create_backup.sh`，生成 `backup/v0.1.73`。
- 新增测试目标构建：成功。
- 新增测试：`VisionApiClientJsonTest` 通过。
- 真实工作流 Release 构建：成功。
- 真实工作流 CTest：3/3 通过。
- 真实工作流打包：成功生成 `output/v0.1.67/CineVault-Setup-v0.1.67.exe`。
- 安装包大小：52.92 MB。
- 安装包 SHA256：`D3D78B096B0F242A12EE0A6EEE843463B0A844545C276C089188FB9A38B1AB72`。
- 启动烟测：真实工作流 `CineVault.exe` 启动 5 秒后手动结束，未提前退出。
- 临时缓存清理：已删除空的 `output/_installer_staging`。

## 待办清单（未完成）

- 人工或使用真实视觉接口重新解析之前失败的视频，确认摘要和逐帧解析能生成。
- 如果实际接口仍失败，下一步补充原始响应的安全截断诊断日志，区分空内容、非对象 JSON、接口错误文本和模型格式漂移。

## 下一步要做什么

安装或运行 `output/v0.1.67/CineVault-Setup-v0.1.67.exe`，进入素材管理中心，对之前失败的视频执行重新解析；如仍出现失败，把新的失败原因继续发来，我会从 `088` 快照后直接接着处理。
