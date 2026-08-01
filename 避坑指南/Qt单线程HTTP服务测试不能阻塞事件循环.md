# Qt 单线程 HTTP 服务测试不能阻塞事件循环

## 问题

为 `WebSearchService` 写单测时，测试客户端最初使用 `QTcpSocket::waitForReadyRead()` 阻塞等待响应。由于测试客户端和 `QTcpServer` 服务端都运行在同一个 Qt 主线程，阻塞等待会导致服务端无法处理 `newConnection/readyRead` 事件，表现为 `ctest` 卡住不结束。

## 处理方式

测试 helper 改为在等待期间循环调用：

```cpp
QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
```

并根据 HTTP `Content-Length` 判断响应体是否完整，避免无限等待。

## 后续避坑

1. 同进程、同线程测试 Qt 网络服务时，不要用阻塞 socket 等待服务端响应。
2. 优先使用事件循环驱动，或把服务端移到独立线程。
3. 如果 `ctest` 卡住，先检查是否是事件循环被测试代码阻塞，而不是先怀疑 TCP 服务本身。
