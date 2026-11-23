# MSVC：QML 缓存对象文件短暂占用

## 现象

并行构建 `CineVault` 时，MSVC 可能在写入生成的 QML 缓存对象文件时提示“Permission denied”，例如 `SettingsPage_qml.cpp.obj`。

## 原因

构建目录中的并行 QML 缓存编译偶发争用；这不是源代码编译错误。

## 处理

确认没有 `cmake`、`ninja` 或 `cl` 进程仍在运行后，以单并发重试：

```powershell
cmake --build <build-dir> --target CineVault --config Release --parallel 1
```

本次单并发重试成功。
