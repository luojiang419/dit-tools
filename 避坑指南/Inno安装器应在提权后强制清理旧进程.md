# Inno 安装器应在提权后强制清理旧进程

日期：2026-07-31

## 现象

安装包运行后提示旧版 `CineVault.exe` 无法自动结束，覆盖安装失败。

## 原因

`InitializeSetup()` 可能在 UAC 提权前执行。此时调用 `taskkill /F` 不能可靠结束另一个管理员权限或旧会话中的进程；如果把失败结果直接作为 `InitializeSetup()` 返回值，安装会在真正提权前被阻断。

## 正确处理

- `InitializeSetup()` 只初始化参数，不因旧进程存在而失败。
- 将强制结束放在 `PrepareToInstall()`，这里已进入正式安装的提权阶段。
- 使用 `taskkill /F /T /IM CineVault.exe` 结束进程树。
- 用 PowerShell `Get-Process ... | Stop-Process -Force` 作为第二通道。
- 循环等待进程真正从任务列表消失，再允许安装器替换文件。
- 保留安装日志，记录每次结束命令的返回码。

## 验收顺序

```text
启动旧版 CineVault
  ↓
运行新版安装包
  ↓
提权后的 PrepareToInstall 强制结束旧进程
  ↓
安装器退出码 0
  ↓
验证安装 EXE 哈希
  ↓
启动新版并检查 /api/health
```
