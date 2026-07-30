# Qt SQLite 空 QString 绑定 NULL 导致 NOT NULL 失败

## 现象

- 新增 `TEXT NOT NULL DEFAULT ''` 字段后，SQL 明明绑定了一个空 `QString`，运行时仍报：

  ```text
  NOT NULL constraint failed: thumbnail.next_retry_at
  ```

- 任务状态先写成运行中，但最终结果无法保存，自动续跑不断重新派发同一批任务，界面看起来一直不前进。

## 根因

默认构造的 `QString` 是 null string。通过 `QSqlQuery::addBindValue()` 绑定时，它可能被转换为 SQL `NULL`，并不等同于数据库空文本 `''`。

## 修复

对 `NOT NULL` 文本列使用显式非 null 空字符串：

```cpp
QString nextRetryAt = QStringLiteral("");
QString failureKind = QStringLiteral("");
```

## 验证

- 同时覆盖成功、永久失败、瞬时失败、重试耗尽四种写入路径。
- 测试必须确认任务不再循环创建，成功结果能把重试字段重置为空文本。
- 数据库迁移测试要验证新列和索引均可在旧项目库上创建。

## 避免方式

- SQLite `NOT NULL` 文本列不要绑定默认构造的 `QString`。
- 新增状态字段时，测试不仅检查 SQL 执行成功，还要检查自动调度是否会因写入失败形成重派发循环。
