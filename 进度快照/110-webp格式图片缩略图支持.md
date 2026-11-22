# 110-webp格式图片缩略图支持

日期：2026-06-27

## 已完成内容

- 已为本地图片增加统一的 URL 路由辅助器，普通图片继续走 `file:///`，`webp` 自动切换到应用内图片提供器。
- 已新增 `LocalImageProvider`，优先尝试 Qt 图片读取；如果当前运行环境无法直接解码，则在 Windows 下回退到 Shell 缩略图读取。
- 已接入素材库卡片、素材管理中心图片缩略图和全屏图片预览弹层，保证 `webp` 不只在列表里显示，也能点开预览。
- 已补充 `LocalImageUrlHelperTest` 单测。

## 当前修改到哪个模块

当前完成到新增子模块：`webp` 格式图片缩略图支持。

修改文件：
- `dit-tools-src/cinevault-pro/src/ui/imaging/LocalImageProvider.h`
- `dit-tools-src/cinevault-pro/src/ui/imaging/LocalImageProvider.cpp`
- `dit-tools-src/cinevault-pro/src/ui/imaging/LocalImageUrlHelper.h`
- `dit-tools-src/cinevault-pro/src/ui/imaging/LocalImageUrlHelper.cpp`
- `dit-tools-src/cinevault-pro/src/app/AppBootstrap.cpp`
- `dit-tools-src/cinevault-pro/src/app/AppContext.h`
- `dit-tools-src/cinevault-pro/src/app/AppContext.cpp`
- `dit-tools-src/cinevault-pro/src/ui/qml/components/AssetCard.qml`
- `dit-tools-src/cinevault-pro/src/ui/qml/components/AssetPreviewOverlay.qml`
- `dit-tools-src/cinevault-pro/src/ui/qml/workspaces/MaterialCenterWorkspace.qml`
- `dit-tools-src/cinevault-pro/CMakeLists.txt`
- `dit-tools-src/cinevault-pro/tests/unit/LocalImageUrlHelperTest.cpp`
- `backup/v0.1.93/模块10-webp缩略图支持对比.md`

## 具体修改的代码前后对比

### 素材卡片缩略图来源调整

修改前：

```qml
property string thumbnailSource: thumbnailPath.length > 0 ? "file:///" + thumbnailPath.replace(/\\/g, "/") : ""
```

修改后：

```qml
property string thumbnailSource: localImageUrlHelper ? localImageUrlHelper.sourceForInput(thumbnailPath) : ""
```

### 图片预览弹层支持 `webp`

修改前：

```qml
imageSource = source.toString()
```

修改后：

```qml
imageSource = localImageUrlHelper
    ? localImageUrlHelper.sourceForInput(source.toString())
    : source.toString()
```

### 应用启动时注册本地图片提供器

修改前：

```cpp
m_engine = std::make_unique<QQmlApplicationEngine>();
```

修改后：

```cpp
m_engine = std::make_unique<QQmlApplicationEngine>();
m_engine->addImageProvider(QStringLiteral("cinevault-local"), new LocalImageProvider);
```

## 验证结果

- 增量编译通过。
- `ctest --test-dir dit-tools-src/cinevault-pro/build/windows-msvc-release-real --output-on-failure` 通过。
- 新增测试 `LocalImageUrlHelperTest` 通过。

## 待办清单（未完成）

- 如需发布安装包版本，可在当前模块基础上继续执行真实安装包构建与手工回归。
- 如需进一步扩展到更多 Qt 不原生支持的图片格式，可复用当前本地图片提供器继续扩展。

## 下一步要做什么

- 若当前只处理 `webp` 缩略图需求，可以进入提测或继续打包验证。
- 若还有新的功能模块，下一轮直接基于本快照继续接力开发。
