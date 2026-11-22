# Windows QtTest 巨量压力测试默认 5 分钟函数超时

## 现象

在 Windows 上执行百万文件的 `LargeCatalogStressTest` 时，CTest 显示外层超时为 1500 秒，但测试会在约 300 秒时以 `0xc0000409` 结束：

```text
scansExternalFixtureAndWritesMachineReadableBaseline function time: 300008ms
LargeCatalogStressTest ... Exit code 0xc0000409
```

如果只看退出码，容易把它误判为扫描器崩溃或巨量目录卡死。

## 根因

QtTest 自身有独立的单测试函数超时。Qt 6.6.3 的默认值为 300000 毫秒，环境变量名为 `QTEST_FUNCTION_TIMEOUT`。这个限制独立于 CTest 的 `TIMEOUT`；CTest 即使显示 1500 秒，也无法阻止 QtTest 在 5 分钟时先终止测试函数。

本项目百万文件扫描在当前机器上实际需要约 2149 秒，因此还会超过 CTest 当前 1500 秒的外层限制。仅把 `QTEST_FUNCTION_TIMEOUT` 提高到 1400000 仍会在精确的 1400009 毫秒处被终止。

## 诊断方法

不要直接抬高超时后盲等。先检查异常退出留下的临时 `stress.cvdb`：

- 300 秒终止的数据库中有 292512 条 `scan_stage_asset`；
- 1400 秒终止的数据库中有 952000 条 `scan_stage_asset`；
- `scan_session.updated_at` 持续更新到终止前一秒。

这说明扫描持续推进，终止来自测试框架，而不是业务停滞。

也可在 Qt Test DLL 中确认环境变量存在：

```powershell
rg -a -o 'QTEST_[A-Z_]*TIMEOUT[A-Z_]*' C:\Qt\6.6.3\msvc2019_64\bin\Qt6Test.dll
```

## 正确处理

百万规模专项验收应直接运行现有测试可执行文件，并同时设置明确的 QtTest 与外层监控上限：

```powershell
$env:QTEST_FUNCTION_TIMEOUT = '3500000'
$env:CINEVAULT_STRESS_SOURCE = '<仓库外百万文件目录>'
$env:CINEVAULT_STRESS_REPORT = '<证据目录>\large-catalog-1m-scan.json'
& '<Release构建目录>\LargeCatalogStressTest.exe' -v1
```

外层监控应限制在 3600 秒以内，并每 15 秒采样 working set、private bytes 和 CPU 时间。只有退出码为 0、`asset_count` 精确等于 1000000、队列最终清零、峰值内存有界时才判为通过。

普通全量 CTest 不设置 `CINEVAULT_STRESS_SOURCE` 时，该压力用例会跳过，不需要全局放宽所有测试的超时。

## 本次实测结果

```text
asset_count=1000000
scan_elapsed_ms=2149381
peak_working_set_bytes=23257088
peak_private_bytes=10125312
peak_queue_depths.scan.directory_entries=256
queue_depths.scan.directory_entries=0
stage_batch_counts.scan.directory_enumeration=4004
stage_batch_counts.scan.directory_stage_write=4004
sqlite_busy_count=0
```

百万条目相对十万条目扩大 10 倍，峰值 working set 仅约 1.19 倍，证明扫描路径保持有界；但耗时基线必须按真实机器记录，不能继续沿用 QtTest 默认 5 分钟或 CTest 1500 秒作为百万扫描上限。
