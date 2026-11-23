# 222-视觉接口JSON结构修复

## 已完成内容

- 已按续写规则读取最新快照 `221-任务清理与视觉串行执行最终验证.md`，没有读取全部历史快照。
- 已读取当前项目错误日志，定位到视觉接口请求失败根因：

```text
视觉接口请求失败：status=400 endpoint=http://115.231.35.105:12345/v1/chat/completions body={"error":"'response_format.type' must be 'json_schema' or 'text'"}
```

- 已创建本阶段源码备份：`backup/v0.1.170`。
- 已修复视觉接口请求体：默认 `response_format.type` 从旧的 `json_object` 改为当前服务支持的 `json_schema`。
- 已为不同视觉调用场景绑定明确 schema 名称：
  - `vision_repair`
  - `vision_status`
  - `vision_frame_analysis`
  - `vision_video_summary`
  - `vision_dimension_analysis`
- 已保留服务端拒绝 `response_format` 时改用 `text` 的二次请求兜底。
- 已补充 `VisionApiClientImageTest` 断言：
  - 请求体使用 `json_schema`。
  - schema 名称符合当前业务场景。
  - 当服务端返回 `response_format.type must be 'json_schema' or 'text'` 时，第二次请求切换为 `text`。
- 已完成 Release 构建并更新产物：`output/v0.1.142/CineVault-Setup-v0.1.142.exe`。
- 已确认构建 staging 目录未残留额外临时缓存。

## 当前修改到哪个模块

- 当前模块：视觉接口网络请求模块。
- 代码文件：`dit-tools-src/cinevault-pro/src/infrastructure/network/VisionApiClient.cpp`。
- 测试文件：`dit-tools-src/cinevault-pro/tests/unit/VisionApiClientImageTest.cpp`。

## 具体修改的代码前后对比

### 1. response_format 类型

修改前：

```cpp
{QStringLiteral("response_format"), QJsonObject{{QStringLiteral("type"), QStringLiteral("json_object")}}},
```

修改后：

```cpp
{QStringLiteral("response_format"), jsonSchemaResponseFormat(schemaName, schema)},
```

并新增：

```cpp
QJsonObject jsonSchemaResponseFormat(const QString &name, const QJsonObject &schema)
{
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("json_schema")},
        {QStringLiteral("json_schema"),
         QJsonObject{
             {QStringLiteral("name"), name},
             {QStringLiteral("strict"), false},
             {QStringLiteral("schema"), schema}
         }}
    };
}
```

### 2. makeChatPayload 参数

修改前：

```cpp
QJsonObject makeChatPayload(const QString &model, const QJsonArray &content, int maxTokens)
```

修改后：

```cpp
QJsonObject makeChatPayload(const QString &model,
                            const QJsonArray &content,
                            int maxTokens,
                            const QString &schemaName,
                            const QJsonObject &schema)
```

### 3. 调用点绑定 schema

修改前：

```cpp
makeChatPayload(model, content, maxTokens)
```

修改后示例：

```cpp
makeChatPayload(model,
                content,
                maxTokens,
                QStringLiteral("vision_dimension_analysis"),
                dimensionAnalysisSchema())
```

### 4. 单元测试新增兜底验证

新增测试：

```cpp
void VisionApiClientImageTest::analyzeDimensions_fallsBackToTextWhenResponseFormatRejected()
```

核心断言：

```cpp
QCOMPARE(firstResponseFormat.value(QStringLiteral("type")).toString(), QStringLiteral("json_schema"));
QCOMPARE(secondResponseFormat.value(QStringLiteral("type")).toString(), QStringLiteral("text"));
```

## 验证记录

- 定向编译：`VisionApiClientImageTest` Release 目标构建通过。
- 定向测试：`ctest -R VisionApiClientImageTest --output-on-failure`，1/1 通过。
- 排除无关崩溃测试后全局测试：`ctest -E ImportServiceLegacyRescanTest --output-on-failure`，15/15 通过。
- 全量测试：15/16 通过；`ImportServiceLegacyRescanTest` 仍 SegFault，属于当前工作树中的未跟踪测试，和本轮视觉 JSON 修复无直接关联。
- Windows 安装包构建：`.\tool\build_windows.ps1 -EnableFfmpeg` 通过。
- 最新产物：`output/v0.1.142/CineVault-Setup-v0.1.142.exe`。

## 待办清单（未完成）

- 单独排查 `ImportServiceLegacyRescanTest` SegFault，避免后续全量测试被该未跟踪测试阻断。
- 安装或运行 `output/v0.1.142/CineVault-Setup-v0.1.142.exe` 后，用真实视觉服务做一次人工烟测，确认日志中不再出现 `response_format.type=json_object` 相关 400 错误。
- 如远端视觉模型仍返回非 JSON 内容，再继续处理响应解析或自动修复链路。

## 下一步要做什么

- 建议先用 v0.1.142 安装包覆盖运行一次，复现原先会触发视觉分析的操作。
- 若日志不再出现 `response_format.type` 400 错误，本轮问题可视为已关闭。
- 若出现新的“返回 JSON 结构错误”，下一步应读取最新 `app.log`，判断是服务端请求格式问题、模型输出内容问题，还是本地解析规则问题。

