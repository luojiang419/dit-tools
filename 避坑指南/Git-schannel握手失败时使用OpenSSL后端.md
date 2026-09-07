# Git schannel握手失败时使用OpenSSL后端

## 问题表现

HTTPS推送返回`schannel: failed to receive handshake, SSL/TLS connection failed`。

## 触发条件

本次Windows环境使用schannel访问GitHub；直接访问以及使用项目约定代理均失败。

## 当前根因判断

问题与schannel连接路径有关；相同代理下切换OpenSSL后`ls-remote`和`push`均成功。未进一步定位到证书链或系统TLS组件，不应宣称已证明某一底层原因。

## 无效尝试

直接推送；保持schannel并仅设置代理。

## 正确解决方案

先验证远端连接，再正常推送，参数仅对单次命令生效：

```powershell
git -c http.sslBackend=openssl -c http.proxy=http://127.0.0.1:7890 ls-remote --heads origin
git -c http.sslBackend=openssl -c http.proxy=http://127.0.0.1:7890 push origin HEAD
```

不要关闭证书验证，不要无必要修改全局Git配置。仅在实际网络故障发生时使用代理。

## 验证方法与影响模块

确认推送成功并比对远端分支与本地HEAD；只涉及Git网络传输，不涉及业务代码。
