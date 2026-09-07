# Qt PDF打印原点与文字DPI一致性

## 问题表现

PDF内容整体向右下偏移，右侧与页脚可能越界；长文字在PDF和预览中的换行、分页不一致，固定行高可导致音频内容越出边框。

## 触发条件与根本原因

- QPdfWriter已把QPainter原点平移到可打印区域；直接把`paintRectPixels()`的左上角再次用于绘制会重复施加页边距。
- `QFontMetricsF(font)`默认采用屏幕设备指标，与144 DPI PDF不一致。
- QImage预览未设置144 DPI；补齐DPI后，原先按屏幕指标设置的多行单元格需要重新核算行高。

## 正确解决方案

- PDF内容区域使用`QRectF(QPointF(0, 0), paintRectPixels(resolution).size())`。
- 使用`QFontMetricsF(font, painter.device())`测量实际绘制宽高。
- QImage的dotsPerMeter按`144 / 0.0254`设置。
- 多行行高按实际字体高度、行距、上下内边距计算；名称与路径分别限行并显示省略号。

## 验证方法

运行`ReportRenderEngineTest`，用包含长名称、长路径、缺失元数据和音频流的素材覆盖分页；使用Poppler渲染PDF，并与应用生成的预览对比。不能仅凭文本提取判断布局正常。

## 影响模块

`core/report/ReportRenderEngine.cpp`、报表预览与PDF导出。
