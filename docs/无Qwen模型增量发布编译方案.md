# 无 Qwen 模型增量发布编译方案

## 目标

Qwen3 0.6B 模型只通过已经交付给用户的一次性安装包分发。后续 Windows 正式构建继续保留本地智能搜索能力和 llama.cpp 运行时，但不下载、不复制、不安装、不上传 Qwen GGUF 模型。

## 发布策略

- 正式构建启用 `CINEVAULT_ENABLE_LOCAL_SEARCH_ASSISTANT=ON`，保留智能搜索代码和运行时。
- `CINEVAULT_BUNDLE_SEARCH_ASSISTANT_MODEL=OFF` 是所有正式预设继承的默认值。
- `PrepareSearchAssistantDependencies.cmake` 默认只准备 llama.cpp 运行时；只有显式传入 `-DINCLUDE_MODEL=ON` 才会准备模型。
- `build_windows.ps1` 固定传入 `-DINCLUDE_MODEL=OFF`，且只精确复制 BGE 模型目录，不再复制整个 `data/models`。
- Inno Setup 额外排除 `qwen3-0.6b` 和所有 `*.gguf` 文件。
- CI 不再组装或上传携带 Qwen 的 GPU 测试包。

## 升级兼容

应用仍从以下原路径发现模型：

`{app}/data/models/qwen3-0.6b/Qwen3-0.6B-Q8_0.gguf`

新安装包使用相同 `AppId` 和默认安装目录，且没有删除旧模型的安装逻辑。覆盖安装时，旧版已经写入该路径的模型会保留并继续被当前运行时使用。全新安装且没有模型时，应用沿用现有资产缺失处理并回退到本地规则。

安装启动测试会在目标目录预放一个模型哨兵文件，安装后校验文件内容未被删除或覆盖。

## 防回归门禁

`tool/assert_model_free_payload.ps1` 在 Inno Setup 编译前递归检查 staging：

- 出现名为 `qwen3-0.6b` 的目录立即失败；
- 出现任何 `.gguf` 文件立即失败。

`tool/test_model_free_payload.ps1` 同时验证正常 BGE 资源可通过、Qwen 目录会被拒绝、大小写不同的 GGUF 扩展名也会被拒绝。GitHub Actions 在正式编译前运行该测试。

## 标准构建命令

```powershell
./tool/build_windows.ps1 -RealWorkflow -Version v0.1.181
```

版本参数必须与仓库 `VERSION` 一致；正式发布工作流仍按现有规则自动递增版本。

## 本次实测结果

- Qwen 原始文件大小：639,446,688 字节。
- 无模型 staging：739,750,860 字节，未发现 Qwen 目录或 GGUF。
- v0.1.180 含模型安装包：823,682,469 字节（785.52 MiB）。
- v0.1.181 安装包：209,757,433 字节。
- 安装包减少 613,925,036 字节，体积下降 74.53%。
- 安装包 SHA-256：`f3ad3cfb7f82b4c2722eae913076bc233a05e92fdb853530939231d02fe33c97`。
- 非 GPU 自动化测试：39/39 通过。
- QML 门禁：33 个文件、731 条允许诊断、0 error。
- 安装启动和旧模型保留探针：通过。

## 验收标准

- 依赖准备日志必须出现 `Skipping Qwen model` 和 `includes_model=OFF`。
- 正式 staging 不含 `qwen3-0.6b` 或 `.gguf`。
- 安装包可启动，版本与 `VERSION` 一致。
- 覆盖安装后，旧模型路径及内容保持不变。
- 39 项非 GPU 测试和 QML 门禁全部通过。
