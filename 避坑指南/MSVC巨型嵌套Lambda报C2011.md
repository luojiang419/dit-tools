# MSVC 巨型嵌套 Lambda 报 C2011

## 问题表现

在 `VideoAnalysisService::startNextAnalysis` 的长工作 Lambda 内增加检查点逻辑后，MSVC 14.44 报外层 `<lambda_1>` 类重定义，并将 `reloadFrames` 小 Lambda 末尾定位为先前声明，随后产生一系列级联语法错误。

## 触发条件

QtConcurrent 的大型工作 Lambda 内嵌多个捕获 Lambda，使用当前 MSVC `/std:c++latest` 配置。此次源码括号与作用域检查未发现语法错误。

## 根本原因

当前证据指向该编译器对这种嵌套 Lambda 组合的处理问题；未建立独立编译器最小复现，不将其泛化为所有 MSVC 版本的问题。

## 无效尝试

确认文件稳定后重编译仍失败；仅将工作 Lambda 命名后再传入 QtConcurrent 也未解决。

## 正确解决方案

移除只封装一次数据库查询的 `reloadFrames` Lambda，两个调用点直接使用已有 `loadFrameRows` 函数。工作 Lambda 单独命名以降低表达式复杂度。

## 验证方法

同一工具链重新编译 `CineVault` 成功；关联恢复和解析测试通过。

## 如何避免

长工作函数中直接复用已有函数；遇到此类诊断先核对源码和首个错误位置，不机械清缓存或删除业务逻辑。

## 影响模块

VideoAnalysisService、Windows MSVC 构建。
