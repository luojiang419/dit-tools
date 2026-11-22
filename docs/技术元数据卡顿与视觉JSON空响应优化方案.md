# 技术元数据卡顿与视觉 JSON 空响应优化方案

## 1. 任务目标

本方案解决两个表面同时出现、实际互相独立的问题：

1. 任务中心长期停在“准备读取视频/音频/图片技术参数，0/128 个文件”。
2. 视觉解析提示“返回内容不是有效 JSON；自动修复失败；原始返回内容为空”。

目标不是继续堆叠无条件重试，而是让每个失败分支都可识别、可恢复、可验收：

- 技术元数据任务在用户持续操作软件时也能开始执行。
- 等待资源、执行外部工具、写数据库和退避重试显示为不同状态。
- 同一失败批次不再每 30 秒创建一个替代任务。
- LM Studio 即使在服务端默认开启思考，视觉请求也优先得到最终结构化 JSON。
- HTTP 200、空 `content`、仅 reasoning、输出截断和真正的坏 JSON 使用不同错误原因。

## 2. 已确认的现场事实

### 2.1 当前运行环境

- 已安装版本：`v0.1.185`
- 当前视觉 Base URL：远程 HTTP 服务，不是本机 `localhost`
- 当前视觉模型：`qwen3.5-9b-vlm`
- 单次视觉超时：60 秒
- 单帧视觉请求 `max_tokens`：300
- LM Studio 本机版本：`0.4.20+1`，但当前影资管家的视觉 Base URL 指向远程服务

### 2.2 技术元数据任务证据

- 项目共有 690 个可读视频文件。
- `media_metadata` 表当前为 0 条，说明没有一个文件成功完成技术元数据落库。
- 任务表最后一条记录仍为 `Running`、`0/128`。
- 应用日志在 `2026-07-30 14:09:36` 到 `14:53:28` 之间记录了 87 次相同派发。
- 派发中位间隔为 30.25 秒。
- 每次都是 `metadata_batch=128`、`metadata_backlog=690`，待办数完全没有减少。

这不是“第一个文件本身解析很慢”，而是任务反复等待/取消/重派，始终没有越过首项执行边界。

### 2.3 视觉 JSON 证据

- 失败帧的 `last_http_status` 是 200，不是网络或认证失败。
- 持久化错误明确包含：
  - 首次 JSON 解析失败。
  - 自动修复时发现原始 assistant 内容为空。
  - 纯文本兜底仍发现原始 assistant 内容为空。
- 同一视频计划解析 98 帧，已有 95 帧成功，说明模型、图片传输和基本 JSON schema 链路不是整体不可用。
- 当前 `VisionApiClient::makeChatPayload()` 没有发送 `chat_template_kwargs.enable_thinking=false`。
- 同项目的 `SearchAssistantClient` 已经发送该字段，说明项目内已有可复用的关闭思考约定。
- `VisionResponseParser` 只读取 `choices[0].message.content`，不读取或诊断 `reasoning_content`、`reasoning`、`finish_reason` 和 `usage`。

因此，截图中的错误更准确的含义是“服务返回成功包，但没有返回最终正文”，而不只是“正文不是合法 JSON”。

## 3. 根因结论

### 3.1 `0/128` 的根因

主因是调度状态与重试状态混在一起：

1. 技术元数据任务使用后台 Heavy I/O 资源，并曾被 `requiresIdle=true` 限制。
2. 用户在软件内查看任务时会持续产生活动，后台任务可能长期不满足空闲条件。
3. 资源等待失败后，任务内部已经安排退避重试。
4. `runMediaJobs()` 结束时又无条件安排 250 ms 的立即续批，缩短或覆盖了失败退避。
5. 下一轮创建新任务并清理旧失败任务，界面只看到新的“准备中 0/128”，真实失败原因被快速替换。
6. 协调器只有“租约有效/无效”，没有把等待空闲、资源超时、项目切换、队列满等原因区分出来。

最终形成：

```text
创建 0/128 任务
  -> 等待后台资源/系统空闲
  -> 失败或超时
  -> 安排退避
  -> 外层又安排 250 ms 立即重派
  -> 删除旧失败任务
  -> 再次显示新的 0/128
```

### 3.2 视觉 JSON 的根因

高概率主因是“思考输出 + 输出预算过小 + 响应诊断缺失”的组合，不是输入上下文本身爆炸：

1. 视觉请求没有显式关闭模型思考。
2. Qwen 思考模型可能先生成 reasoning，再生成最终回答。
3. 单帧请求只给 300 个输出 token，但 schema 已包含描述、标签、实体属性和 OCR。
4. 如果 reasoning 占满输出预算，LM Studio 仍可返回 HTTP 200，但最终 `message.content` 为空。
5. 客户端把空字符串送入 JSON 解析器，于是误报“不是有效 JSON”。
6. 自动修复依赖原始 `content`；原文为空时没有任何可修复材料，所以必然再次失败。

不能仅通过“扩大上下文窗口”解决。上下文窗口主要影响输入和历史容纳量；本次现场证据指向最终输出正文为空，更接近输出 token 被 reasoning 消耗或服务端只返回 reasoning。

## 4. 分模块实施方案

## 模块一：修正技术元数据调度契约（P0）

### 目标

消除永远 `0/128` 和每 30 秒任务重建。

### 改动

1. 核心技术元数据读取不再要求系统空闲：
   - `MediaTaskService` 的 ffprobe 任务使用后台优先级。
   - `requiresIdle=false`。
   - 每处理一个文件就释放 Heavy I/O 租约，给缩略图等前台任务让路。
2. 让 `runMetadataJob()` / `runThumbnailJob()` 返回明确结果：
   - `Completed`
   - `ContinueImmediately`
   - `RetryScheduled`
   - `Cancelled`
3. `runMediaJobs()` 只在成功完成一批且仍有待办时安排 250 ms 续批。
4. 如果内部已经安排 5 秒、30 秒或 2 分钟退避，外层不得再覆盖。
5. 同一素材源、同一任务类型优先续写现有任务记录，不重复创建“准备中”任务。
6. 旧失败任务在新任务真正取得资源或完成首项后再标记为 `Superseded`；不要在新任务刚创建时就删除诊断证据。

### 最小代码形态

修改前：

```cpp
runMetadataJob(...);
scheduleSourceRootRetry(projectDatabasePath, sourceRootId, 250);
```

修改后：

```cpp
const auto outcome = runMetadataJob(...);
if (outcome == MediaJobOutcome::ContinueImmediately) {
    scheduleSourceRootRetry(projectDatabasePath, sourceRootId, 250);
}
// RetryScheduled / Cancelled 不再由外层覆盖。
```

### 关于 ExifTool 深层元数据

ExifTool 属于增强信息，不应和 ffprobe 核心技术参数共用同一种 UI 语义：

- 推荐保留低优先级、小批次执行。
- 如继续要求系统空闲，任务状态必须显示“等待用户空闲”，不能显示“准备读取”。
- 等待超过阈值后可执行 1～4 个文件的小切片，再重新让出资源，避免永久饥饿。

## 模块二：把“等待”变成可见状态（P0）

### 目标

即使单文件确实较慢，用户也能知道卡在哪一层。

### 状态

- `Queued`：已进入队列。
- `WaitingForIdle`：等待用户空闲。
- `WaitingForHeavyIo`：等待外部工具执行资源。
- `RunningExternalTool`：正在运行 ffprobe / ExifTool。
- `Persisting`：正在写 SQLite。
- `Backoff`：失败后等待下次重试。

### UI 规则

1. 开始尝试首个文件时立即把当前项更新为 `1/128`，并显示文件名。
2. 等待或执行超过 3 秒，每 3 秒更新一次：
   - `正在读取 1/128：xxx.mp4，ffprobe 已运行 9 秒`
   - `等待后台 I/O 资源 12 秒`
   - `将在 5 秒后重试`
3. 明确区分：
   - 本批：`0/128`
   - 全部待办：`690`
4. 等待状态使用不确定进度动画；完成项进度才使用确定百分比。
5. 超时后保留最后一次阶段、文件名、耗时和可执行的失败原因。

## 模块三：修正 LM Studio 结构化输出请求（P0）

### 目标

让服务端默认开启思考时，应用仍优先取得最终 JSON。

### 改动

1. 所有视觉请求统一增加：

```json
{
  "chat_template_kwargs": {
    "enable_thinking": false
  }
}
```

适用范围：

- 测试连接
- 单帧分析
- 图片分析
- 多维度分析
- 视频汇总
- 自动格式修复

2. 对明确属于 Qwen3 系列、且后端不识别模板参数的兼容模式，可在提示词末尾追加 `/no_think`；不能对所有模型无条件追加模型私有指令。
3. 结构化任务使用低随机性，例如 `temperature=0` 或接近 0。
4. 保留 `response_format.type=json_schema`，并在真实 LM Studio 版本上验证 `strict=true`。
5. 将单帧复杂 schema 的输出预算从 300 调整到约 640～768 token；关闭思考后不需要无限扩大。
6. 自动修复只处理“有非空正文但 JSON 不合法”的情况。
7. 如果正文为空，不再拿空文本做格式修复，而是重试原始请求一次：
   - 强制关闭思考。
   - 使用略高输出预算。
   - 仍为空则终止，不循环重试。

## 模块四：建立响应信封诊断（P0）

### 目标

区分四种完全不同的失败：

1. HTTP/认证/超时失败。
2. HTTP 200，但 `message.content` 为空。
3. `content` 非空，但 JSON 截断或格式错误。
4. JSON 合法，但业务字段为空。

### 解析内容

新增响应信封解析结果：

```cpp
struct AssistantResponseEnvelope {
    QString content;
    bool hasReasoning = false;
    qsizetype reasoningLength = 0;
    QString finishReason;
    qint64 promptTokens = 0;
    qint64 completionTokens = 0;
};
```

兼容检测字段：

- `message.reasoning_content`
- `message.reasoning`
- `finish_reason`
- `usage.prompt_tokens`
- `usage.completion_tokens`

reasoning 只能用于判断“为什么没有最终正文”，不能直接当作业务 JSON 落库。

### 新错误文案

- `视觉服务返回 HTTP 200，但最终正文为空；服务仅返回了推理内容`
- `视觉模型达到输出 token 上限，尚未生成最终 JSON`
- `视觉服务返回非空正文，但 JSON 在第 N 个字符处截断`
- `视觉 JSON 合法，但 caption/entities/ocr 等业务字段均为空`

## 模块五：调度器与模型调用可观测性（P1）

### 调度日志

只记录非敏感信息：

- job id
- source root id
- asset id
- 资源类型
- 等待原因
- 等待/执行耗时
- 退出码
- 重试次数和下次重试时间

新增事件示例：

```text
resource_wait_start job=774 resource=HeavyIo reason=system_active
resource_wait_end job=774 outcome=acquired elapsed_ms=1230
ffprobe_end asset=18 outcome=success elapsed_ms=842
media_retry_scheduled source=1 delay_ms=5000 reason=resource_timeout
```

### 视觉日志

禁止记录 API Key、完整提示词、图片 Base64、完整 reasoning 和完整模型正文。

只记录：

- HTTP 状态
- `content_length`
- `reasoning_length`
- `finish_reason`
- prompt/completion token 数
- JSON 解析分类
- 本次是否进行了原始请求重试或格式修复

## 5. 文件/模块清单

### 技术元数据链

- `src/application/MediaTaskService.h/.cpp`
- `src/application/MetadataExtractionService.h/.cpp`
- `src/application/IndexingWorkCoordinator.h/.cpp`
- `src/application/JobProgressHeartbeat.h`
- `src/core/jobs/JobEngine.h/.cpp`
- `src/ui/models/JobListModel.h/.cpp`
- `src/ui/viewmodels/JobTimelineViewModel.h/.cpp`
- `src/ui/qml/components/JobTimelineBar.qml`
- `src/ui/qml/components/JobProgressInspectorPane.qml`

### 视觉响应链

- `src/infrastructure/network/VisionApiClient.cpp`
- `src/infrastructure/network/VisionResponseParser.h/.cpp`
- `src/infrastructure/search/SearchAssistantClient.cpp`，仅复用关闭思考的既有约定

### 测试

- `tests/unit/MediaTaskServiceRecoveryTest.cpp`
- `tests/unit/MetadataExtractionServiceTest.cpp`
- `tests/unit/IndexingWorkCoordinatorTest.cpp`
- `tests/unit/JobEngineTest.cpp`
- `tests/unit/JobListModelTest.cpp`
- `tests/unit/VisionApiClientImageTest.cpp`
- `tests/unit/VisionApiClientJsonTest.cpp`

## 6. 待办清单

### P0：先止血

- [ ] 修复失败退避被 250 ms 立即续批覆盖的问题。
- [ ] ffprobe 核心技术元数据任务取消 `requiresIdle` 限制。
- [ ] 首项开始时立即发布文件名、阶段和心跳。
- [ ] 视觉请求统一显式关闭思考。
- [ ] 空 `content` 不再进入格式修复，而是原始请求单次重试。
- [ ] 增加 `finish_reason`、reasoning 长度和 token 使用诊断。
- [ ] 调整单帧复杂 JSON 的合理输出预算。

### P1：稳定性与体验

- [ ] 任务结果改为显式 outcome，禁止重复调度责任。
- [ ] 任务中心增加等待/运行/写入/退避状态。
- [ ] 批次进度与总待办分开展示。
- [ ] 资源租约增加 owner、阶段、持有时间和超时告警。
- [ ] 失败任务改为 superseded 审计，不立即删除。

### P2：长期优化

- [ ] 评估将 Heavy I/O 单布尔锁拆成缩略图、探测、深层元数据和语义索引的限流通道。
- [ ] 对连续后台任务增加老化提升，防止前台任务持续到来时永久饥饿。
- [ ] 在设置页显示视觉模型的结构化输出/思考兼容性探测结果。

## 7. 验收标准

### 技术元数据

- [ ] 用户持续移动鼠标、切换页面时，技术元数据任务仍能在 5 秒内显示正在处理的首个文件。
- [ ] 30 秒内至少有一条 `media_metadata` 成功或明确失败记录落库，不再永远为 0。
- [ ] 690 个文件的待办数持续下降。
- [ ] 一个 128 文件批次只对应一个任务生命周期，不再每 30 秒重建。
- [ ] 资源超时后实际退避不少于策略规定值，不被 250 ms 续批覆盖。
- [ ] 单文件 ffprobe 超时不会卡住整个批次。
- [ ] 关闭并重启应用后，从未完成项继续，不重复已成功项。

### 视觉 JSON

- [ ] 请求体单测确认所有视觉入口都带 `enable_thinking=false`。
- [ ] 模拟 `content="" + reasoning_content 非空 + finish_reason=length` 时，返回“输出预算耗尽/仅推理”的准确错误。
- [ ] 模拟非空坏 JSON 时只进行一次格式修复。
- [ ] 模拟空正文时不发格式修复请求，只允许一次原始请求重试。
- [ ] 使用当前 `qwen3.5-9b-vlm` 连续解析至少 20 帧，不再出现由思考占满预算导致的空 `content`。
- [ ] JSON schema 返回全部通过解析和业务字段归一化。
- [ ] 日志中不出现 API Key、图片 Base64、完整 prompt 或 reasoning。

### 全局

- [ ] 定向单测、Release 全量构建和全量 CTest 通过。
- [ ] 使用真实项目完成 690 文件技术元数据回归。
- [ ] 使用真实 LM Studio 接口完成连接、单帧、多帧汇总回归。
- [ ] 版本号自动递增后再生成安装包；未完成真实验收前不发布。

## 8. 实施顺序

1. 模块一：先修复立即重派和空闲门禁，验证待办开始下降。
2. 模块二：补齐阶段心跳和 UI 状态，验证不再出现无解释的“准备中”。
3. 模块三：关闭视觉思考、调整预算和空正文重试。
4. 模块四：补响应信封诊断和错误分类。
5. 模块五：补日志、单测、真实接口回归。
6. 全量构建、真实项目验收、版本递增、打包。

## 9. 官方协议依据

- LM Studio Structured Output：<https://lmstudio.ai/docs/developer/openai-compat/structured-output>
- LM Studio Chat Completions：<https://lmstudio.ai/docs/developer/openai-compat/chat-completions>
- LM Studio API Changelog（reasoning 独立字段）：<https://lmstudio.ai/docs/developer/api-changelog>
- LM Studio Qwen3 模型页（思考默认开启、支持 `/no_think`）：<https://lmstudio.ai/models/qwen/qwen3-32b>

