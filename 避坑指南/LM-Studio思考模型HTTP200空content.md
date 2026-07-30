# LM Studio 思考模型 HTTP 200 但 content 为空

## 典型现象

- HTTP 状态为 200。
- 客户端却报告“返回内容不是有效 JSON”。
- `choices[0].message.content` 是空字符串。
- `reasoning_content` 或 `reasoning` 有内容，或者 `finish_reason=length`。
- 同一模型的大部分请求成功，少数复杂图片失败。

## 判断

这通常不是输入上下文简单超限。更常见的链路是：

1. 服务或模型默认开启思考；
2. 推理内容消耗 completion token 预算；
3. 推理与最终正文被放在不同字段；
4. 达到输出上限时尚未生成最终 `content`；
5. 客户端只读取 `content`，把空字符串误报成普通坏 JSON。

## 正确处理

1. 对需要严格 JSON 的视觉请求显式设置：

```json
{
  "temperature": 0,
  "chat_template_kwargs": {
    "enable_thinking": false
  }
}
```

2. 给复杂 JSON 足够的输出预算。
3. 同时读取 `content`、reasoning 是否存在、`finish_reason` 和 usage。
4. 日志只记录 reasoning 长度，不记录 reasoning 正文。
5. 空正文时重试一次原始请求；不要把空字符串交给“JSON 修复器”。
6. 连续两次为空后停止并返回准确原因，防止请求风暴。

## 回归测试要点

- 首次 reasoning-only、第二次有效 JSON：应成功且总请求数为 2。
- 连续两次 reasoning-only：应失败且总请求数仍为 2。
- 第二次请求的 messages 和图片必须与第一次一致。
- 两次请求都必须关闭思考。
- 最终错误不得包含“自动修复失败：原始内容为空”等误导链条。

