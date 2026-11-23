# GitHub 发布更新说明

本文档约定 CineVault Windows 安装包的自动发布步骤，供自动更新功能读取 GitHub `latest release` 使用。

## 发布前提

- 仓库：`luojiang419/dit-tools`
- 自动更新只读取公开 GitHub Release
- 安装包资产名称必须严格符合：
  - `CineVault-Setup-vX.Y.Z.exe`
- 应用启动时会请求：
  - `https://api.github.com/repos/luojiang419/dit-tools/releases/latest`

## 自动发布流程

仓库已配置 `.github/workflows/release-windows.yml`。当桌面端相关代码 push 到 `main` 后，GitHub Actions 会自动执行：

- 读取最新正式 GitHub Release 标签；
- 自动递增语义版本的补丁号；
- 首次自动发布版本不低于 `v0.1.169`；
- 在干净的 `windows-2022` Runner 上准备 Qt、FFmpeg、ExifTool、本地搜索与模型依赖；
- 构建真实工作流安装包并运行完整 CTest；
- 校验安装包名称、产品版本、大小和 SHA-256；
- 创建 Draft Release，上传安装包与 `.sha256` 文件；
- 复核远端资产和标签指向后发布为 Latest Release；
- 任一步骤失败时保留旧 Latest Release，并清理本次草稿与标签。

日常发布只需提交并 push：

```powershell
git push origin main
```

仅修改 Markdown、进度快照、备份、官网或反馈服务时不会触发桌面端正式发布。

发布工作流使用并发串行锁；连续 push 会依次读取刚发布的 Latest Release，避免生成重复版本。对已经成功发布的同一提交执行工作流重跑时会自动跳过，避免同一提交产生多个正式版本。

## 本地构建

本地开发仍可让脚本根据 `output` 历史目录生成下一个测试版本：

```powershell
powershell -ExecutionPolicy Bypass -File tool/build_windows.ps1 -RealWorkflow
```

需要复现指定版本时显式传入版本号：

```powershell
powershell -ExecutionPolicy Bypass -File tool/build_windows.ps1 -RealWorkflow -Version vX.Y.Z
```

输出路径保持为：

```text
output/vX.Y.Z/CineVault-Setup-vX.Y.Z.exe
```

## 发布后检查

- GitHub Release 页面可见最新版本
- 资产名称与版本号完全一致
- 旧版本启动后可检测到更高版本并开始后台下载
- 下载完成后出现统一确认框：
  - `立即更新`
  - `下次启动更新`

## 说明

- 当前更新闭环只覆盖 Windows 安装器更新
- 当前为完整安装包更新，尚未引入差分更新
- GitHub Actions 使用仓库临时 `GITHUB_TOKEN` 创建 Release，不需要保存个人访问令牌
- 当前安装包尚未配置 Authenticode 数字签名证书
- 工作流会校验 GitHub Release 资产摘要；现有客户端仍以文件名和文件大小作为下载完整性判断
- 如果最新 Release 不存在、缺少匹配资产或网络失败，应用只提示中文状态，不中断启动
