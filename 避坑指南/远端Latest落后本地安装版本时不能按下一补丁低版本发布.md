# 远端 Latest 落后本地安装版本时不能按下一补丁低版本发布

## 现象

远端 GitHub Latest 停在旧版本，例如：

```text
Latest Release = v0.1.180
用户已安装 = v0.1.185
仓库 VERSION = 0.1.186
```

如果 CI 脚本机械地从 Latest 递增一个补丁，就会发布 `v0.1.181`。

## 风险

客户端更新比较通常只看：

```text
远端版本 > 本地版本
```

所以 `v0.1.181` 对 `v0.1.185` 不是更新，用户仍然收不到新版。此时“CI 成功发布”会变成假成功。

## 正确规则

- `VERSION == Latest`：不能重复发布，生成或要求版本号提交。
- `VERSION < Latest`：拒绝发布。
- `VERSION > Latest`：发布源码里的显式版本。
- `VERSION > Latest + 1`：允许，但必须打印跳号说明。

## 这次处理

`tool/resolve_release_version.ps1` 已改为允许 `VERSION=0.1.186` 对 `Latest=v0.1.180` 直接发布 `v0.1.186`。

`tool/test_release_version.ps1` 已覆盖：

- 显式高版本发布；
- 等于 Latest 时要求 bump；
- 低于 Latest 时拒绝。

## 以后避坑

排查“用户收不到更新”时，不要只看 CI 是否成功。还要核对：

- GitHub Latest tag；
- 用户当前安装版本；
- 仓库 `VERSION`；
- Release 资产名；
- 更新器精确匹配的版本和资产名。
