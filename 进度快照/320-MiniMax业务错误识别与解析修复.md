# 320 - MiniMax 业务错误识别与解析修复

- 时间：2026-08-03
- 已完成内容：修复 MiniMax-M3 返回 HTTP 200 但没有 `choices` 时被误判为 JSON 格式错误的问题；现在会读取 MiniMax 原生 `base_resp` 并显示具体业务原因，同时不会再无效发送格式修复请求。
- 当前修改模块：`VisionResponseParser`、`VisionApiClient`、视觉 API 客户端单元测试与 Windows 安装包交付。

## 问题与根因

截图中的失败链路为：`视觉接口没有返回可解析结果` → `自动修复失败：无法读取原始返回内容`。

MiniMax 原生接口即使 HTTP 状态码为 200，也可能通过响应根节点的 `base_resp.status_code` 返回业务失败（例如 1008 余额不足）；该响应没有 `choices` 和 assistant 正文。旧代码只检查 `choices`，因此把真实服务错误误判为可修复的 JSON 格式问题，并额外发起没有输入内容的修复请求。

## 关键代码前后对比

### MiniMax 响应信封

修改前：`extractAssistantEnvelope()` 只读取 `choices`。当 `choices` 为空时固定返回“视觉接口没有返回可解析结果”。

修改后：先读取 `base_resp.status_code`，对 1001（超时）、1002（RPM 限流）、1004（鉴权失败）、1008（余额不足）、1013（服务内部错误）、1027（输出内容被拒绝）和 1039（Token 限制）显示带 `status_msg` 的明确 MiniMax 错误；成功响应仍沿用原有 `choices` 解析。

### 自动修复分支

修改前：原响应不存在 assistant 正文时，失败提示会附加“自动修复失败：无法读取原始返回内容”。

修改后：无 assistant 正文时直接保留原始服务错误，不再进行误导性的格式修复失败包装。

## 验证

- `VisionApiClientJsonTest`：新增 `base_resp.status_code=1008` 覆盖，验证错误包含 MiniMax、状态码、余额不足与服务端详情。
- `VisionApiClientImageTest`：新增 MiniMax 业务错误覆盖，验证帧解析只请求一次，不发格式修复请求，且不显示“自动修复失败”。
- Windows MSVC 环境下构建两项视觉测试成功。
- `ctest --test-dir dit-tools-src/cinevault-pro/build/windows-msvc-release-real --output-on-failure -C Release`：50/50 通过。
- 正式构建：`tool/build_windows.ps1 -RealWorkflow` 完成；安装包 staging 目录已清理。
- 安装包：`output/v0.1.201/CineVault-Setup-v0.1.201.exe`。
- SHA-256：`D1A5253650077579324A67A0BC6F4A55E8FBBB5BA074E017A734EE64C224BCEB`。

## 待办清单

- 安装 v0.1.201 后，用原视频再次执行 MiniMax-M3 解析。
- 若仍失败，界面会显示可操作的 MiniMax 业务状态码；根据提示检查余额、密钥、RPM 配额或内容审核状态。

## 下一步

等待真实视频复测结果；若出现新的具体 MiniMax 状态码，再按该状态进行针对性处理。
