# Windows 运行进程锁定构建目录

## 问题

Windows 下重新链接 `CineVault.exe` 时，即使主程序已经退出，构建仍可能失败：

- `LNK1104: 无法打开文件 CineVault.exe`
- CMake `copy_directory` 复制 `search-assistant` 运行库失败

## 原因

软件退出后由它启动的 `llama-server.exe` 可能仍然存活，并继续占用构建目录中的 DLL。主程序本身停止并不代表子进程已经结束。

## 处理方式

1. 先停止正在运行的 `CineVault` 实例。
2. 只按路径筛选构建目录下的 `llama-server` 子进程。
3. 再执行完整链接。

示例：

```powershell
$buildPrefix = 'G:\data\app\DIT-tools\dit-tools-src\cinevault-pro\build\windows-msvc-release-real'
Get-Process -Name CineVault -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process -Name llama-server -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -like "$buildPrefix*" } |
    Stop-Process -Force
```

不要结束其他安装目录中的搜索服务；只处理当前构建目录对应的进程。
