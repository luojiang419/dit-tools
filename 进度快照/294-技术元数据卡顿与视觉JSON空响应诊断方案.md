# 294 - 技术元数据卡顿与视觉 JSON 空响应诊断方案

## 本轮目标

- 诊断任务中心长期停在“准备读取技术参数，0/128 个文件”的原因。
- 判断视觉解析非有效 JSON 是否由 LM Studio 思考模式或上下文不足引起。
- 形成可直接实施的模块化优化方案，不在原因未闭环前盲目修改业务源码。

## 已完成内容

### 1. 技术元数据现场诊断

- 已读取当前安装版本、应用日志和项目数据库。
- 确认已安装版本为 `v0.1.185`。
- 当前项目有 690 个可读视频文件。
- `media_metadata` 表仍为 0 条，说明技术元数据没有完成首项落库。
- 最后一条任务为 `Running`、`0/128`。
- 日志在约 44 分钟内记录了 87 次相同派发。
- 派发中位间隔为 30.25 秒。
- 每次均为 `metadata_batch=128`、`metadata_backlog=690`，待办完全没有下降。

结论：问题发生在首项 ffprobe 之前的调度/资源等待链，不是某个媒体文件解析过慢，也与 LM Studio 无关。

### 2. 调度代码根因

- 核心技术元数据任务曾使用 `requiresIdle=true`。
- 用户查看任务时持续产生应用活动，后台任务可能长期无法满足空闲门禁。
- 资源失败路径内部会安排退避，但 `runMediaJobs()` 末尾仍无条件安排 250 ms 立即续批。
- 新一轮任务会替代并清理旧失败任务，因此 UI 反复回到新的“准备中 0/128”，真实失败原因不可见。
- `IndexingWorkCoordinator` 返回的无效租约没有区分等待空闲、资源超时、队列满、项目切换和退出。

### 3. 视觉接口现场诊断

- 当前视觉服务为远程 HTTP Base URL。
- 当前模型为 `qwen3.5-9b-vlm`，超时 60 秒。
- 失败帧 `last_http_status=200`。
- 持久化错误明确显示：原始 assistant 内容为空，导致自动修复和纯文本兜底均无材料可用。
- 同一视频 98 帧中已有 95 帧成功，说明模型和图片接口不是整体失效。
- `VisionApiClient` 单帧只配置 300 个输出 token。
- 视觉请求没有 `chat_template_kwargs.enable_thinking=false`。
- `VisionResponseParser` 只读取 `message.content`，没有诊断 reasoning、`finish_reason` 和 token 使用。
- 项目内 `SearchAssistantClient` 已存在关闭思考的请求写法，可复用同一约定。

结论：报错实质是“HTTP 200 但最终正文为空”，高概率由服务端默认开启思考、reasoning 消耗输出预算或 reasoning 与正文分栏导致。不是单纯输入上下文爆炸。

### 4. 方案文档

已新增：

- `docs/技术元数据卡顿与视觉JSON空响应优化方案.md`

方案包含：

- 任务目标。
- 根因证据。
- 五个实施模块。
- 文件/模块清单。
- P0/P1/P2 待办。
- 技术元数据、视觉 JSON 和全局验收标准。
- LM Studio 官方协议依据。

## 当前修改到哪个模块

- 根因诊断与总体设计：已完成。
- 业务源码实现：尚未开始。
- 本轮只新增设计与进度快照文档，没有覆盖现有未提交源码修改。

## 具体修改的代码前后对比

本轮没有修改业务代码。拟实施的关键变化如下。

### 调度责任

修改前：

```cpp
runMetadataJob(...);
scheduleSourceRootRetry(projectDatabasePath, sourceRootId, 250);
```

拟修改后：

```cpp
const auto outcome = runMetadataJob(...);
if (outcome == MediaJobOutcome::ContinueImmediately) {
    scheduleSourceRootRetry(projectDatabasePath, sourceRootId, 250);
}
```

### 视觉请求

修改前：

```json
{
  "model": "qwen3.5-9b-vlm",
  "max_tokens": 300,
  "response_format": {"type": "json_schema"}
}
```

拟修改后：

```json
{
  "model": "qwen3.5-9b-vlm",
  "max_tokens": 640,
  "temperature": 0,
  "chat_template_kwargs": {"enable_thinking": false},
  "response_format": {"type": "json_schema"}
}
```

### 错误分类

修改前：

```text
空 content -> 视觉接口返回内容不是有效 JSON -> 对空文本发起格式修复
```

拟修改后：

```text
空 content + reasoning/finish_reason/usage 诊断
-> 单次重试原始请求并关闭思考
-> 仍为空则给出“仅推理/输出预算耗尽/服务正文为空”的准确原因
```

## 待办清单

- [ ] 模块一：修复技术元数据立即重派和用户空闲门禁。
- [ ] 模块二：实现等待/执行/写入/退避心跳与 UI 状态。
- [ ] 模块三：视觉请求统一关闭思考并调整输出预算。
- [ ] 模块四：增加响应信封解析和空正文单次原始请求重试。
- [ ] 模块五：补充安全日志、单测和真实 LM Studio 回归。
- [ ] 完成全量构建、真实项目验收、版本递增和安装包。

## 验收重点

- 用户持续操作软件时，5 秒内显示首个技术元数据文件正在处理。
- 30 秒内 `media_metadata` 至少出现一条终态记录。
- 690 待办持续下降，不再每 30 秒重建任务。
- 服务端默认开启思考时，应用仍取得最终 JSON。
- 空 `content` 不再误报成普通坏 JSON，也不再对空文本发起格式修复。
- 日志不记录 API Key、图片 Base64、完整 prompt、正文或 reasoning。

## 备份说明

- 本轮未修改业务源码，不属于大模块代码开发，因此没有新增源码备份版本。
- 当前工作区已有上一轮未提交源码修改，本轮已全部保留。

## 下一步

- 按方案先实现 P0 模块一：调度 outcome、退避责任和 ffprobe 空闲门禁。
- 完成定向测试后再进入视觉请求契约模块，避免两条独立故障链一次性混改。

