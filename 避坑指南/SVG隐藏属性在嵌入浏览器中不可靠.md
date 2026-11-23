# SVG 隐藏属性在嵌入浏览器中不可靠

## 问题

主题切换按钮使用两个 SVG 图标，并通过 JavaScript 设置 `element.hidden = true/false`。在嵌入式浏览器中，SVG 元素的 `hidden` 属性没有按普通 HTML 元素的布尔属性正确反射，出现主题颜色已切换但图标仍显示旧状态的情况。

## 处理方式

对 SVG 使用显式属性操作：

```javascript
themeMoon.toggleAttribute("hidden", nextTheme === "dark");
themeSun.toggleAttribute("hidden", nextTheme !== "dark");
```

同时保留全局：

```css
[hidden] {
  display: none !important;
}
```

## 后续避坑

涉及 SVG 图标显隐时，优先使用 `toggleAttribute("hidden", condition)` 或显式 class，不要只依赖 SVG 元素的 `.hidden` 属性反射结果。
