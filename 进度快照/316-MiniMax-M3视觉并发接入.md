# 进度快照 316

- 时间：2026-08-03
- 已完成内容：接入 MiniMax-M3 专用视觉协议；模型为 `MiniMax-M3` 时，Base URL 自动解析为 `https://api.minimaxi.com/v1/text/chatcompletion_v2`，请求使用 `max_completion_tokens` 和图文混合 messages；视频待解析帧按最多 200 条一批并发提交，结果仍在原任务线程依次写入 SQLite；其它模型维持原端点、请求体和串行帧请求。设置页会显示 M3 的专用接口与并发提示。
- 当前修改模块：`VisionApiClient`、`VideoAnalysisService`、设置页 QML、视觉客户端单元测试。

## 关键代码前后对比

### 端点与请求体

修改前：`normalizeEndpoint(baseUrl)` 总是将根地址解析到 `/v1/chat/completions`，`makeChatPayload` 统一发送 `max_tokens`、`response_format` 和通用 thinking 参数。

修改后：`normalizeEndpoint(baseUrl, model)` 对大小写无关的 `MiniMax-M3` 返回 `/v1/text/chatcompletion_v2`；M3 的请求使用官方字段 `max_completion_tokens`、最小合法温度值 `0.01` 与原有图文 content 数组，且不发送官方文档仅支持其他模型的 `response_format`。其余模型保留原逻辑。

### 视频帧并发

修改前：`VideoAnalysisService` 在单一循环中逐帧调用 `analyzeFrame`，同一视频的视觉请求为串行。

修改后：当 `VisionApiClient::maxConcurrentFrameRequests(model)` 返回 200 时，使用专用 `QThreadPool` 在一批最多 200 个未完成帧上并发调用；每个帧保留原有最多 3 次重试。所有网络结果完成后才按原顺序更新帧状态、任务进度和数据库，避免 SQLite 跨线程写入。

## 验证

- `VisionApiClientImageTest`：通过，覆盖 M3 原生端点和 200 并发规则。
- `CineVault`（`windows-msvc-release-ffmpeg`）：完整目标编译通过。
- 已检索源码：未发现本次用户提供的 API Key 被写入源码、快照或构建配置。

## 待办清单

- 版本已从 `0.1.198` 递增至 `0.1.199`，需以新版本号重新执行构建校验。
- 创建本模块源码备份并检查临时缓存配额。

## 下一步

使用 `0.1.199` 重新配置并构建测试目标与应用目标；随后生成备份并完成清理检查。
