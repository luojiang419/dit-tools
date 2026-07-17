import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import CineVault

Window {
    id: root
    objectName: "quickSearchWindow"

    property var controller
    property var materialCenterViewModel
    property var shellViewModel
    property var mainWindow
    property bool pinned: false
    property int selectedFlatIndex: 0
    readonly property bool hasQuery: searchField.text.trim().length > 0
    readonly property int folderResultCount: hasQuery && materialCenterViewModel
        ? materialCenterViewModel.folderCount : 0
    readonly property int primaryResultCount: hasQuery && materialCenterViewModel
        ? (materialCenterViewModel.frameSearchMode
            ? materialCenterViewModel.frameCount
            : materialCenterViewModel.assetCount)
        : 0
    readonly property int totalResultCount: folderResultCount + primaryResultCount
    readonly property color quickBg: "#0E1014"
    readonly property color quickHeader: "#171A20"
    readonly property color quickSurface: "#20242C"
    readonly property color quickSurfaceHover: "#252B35"
    readonly property color quickLine: "#303640"
    readonly property color quickSelected: "#2A3039"
    readonly property color quickSelectedLine: "#4B5563"
    readonly property color quickText: "#F4F7FB"
    readonly property color quickMuted: "#A0A8B4"
    readonly property color quickWeak: "#737D8A"
    readonly property color quickAccent: "#C6CDD7"

    width: 820
    height: 650
    visible: false
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    title: "影资管家快捷搜索"

    function imageSource(path) {
        return path && path.length > 0 && localImageUrlHelper
            ? localImageUrlHelper.sourceForInput(path)
            : ""
    }

    function openSearch() {
        selectedFlatIndex = 0
        searchField.text = ""
        if (materialCenterViewModel) {
            materialCenterViewModel.prepareGlobalQuickSearch()
        }
        if (controller) {
            var restoredPosition = controller.restoredWindowPosition(width, height)
            x = restoredPosition.x
            y = restoredPosition.y
        } else {
            x = Screen.virtualX + Math.round((Screen.width - width) / 2)
            y = Screen.virtualY + Math.round(Screen.height * 0.12)
        }
        show()
        raise()
        requestActivate()
        Qt.callLater(function() {
            searchField.forceActiveFocus()
            searchField.selectAll()
        })
    }

    function rememberPosition() {
        if (!controller) {
            return
        }
        var constrainedPosition = controller.rememberWindowPosition(x, y, width, height)
        x = constrainedPosition.x
        y = constrainedPosition.y
    }

    function hideSearch() {
        searchDebounce.stop()
        hide()
    }

    function clampSelection() {
        if (totalResultCount <= 0) {
            selectedFlatIndex = 0
        } else {
            selectedFlatIndex = Math.max(0, Math.min(selectedFlatIndex, totalResultCount - 1))
        }
        if (selectedFlatIndex < folderResultCount) {
            folderList.currentIndex = selectedFlatIndex
            resultList.currentIndex = -1
            folderList.positionViewAtIndex(folderList.currentIndex, ListView.Contain)
        } else {
            folderList.currentIndex = -1
            resultList.currentIndex = selectedFlatIndex - folderResultCount
            resultList.positionViewAtIndex(resultList.currentIndex, ListView.Contain)
        }
    }

    function moveSelection(delta) {
        if (totalResultCount <= 0) {
            return
        }
        selectedFlatIndex = (selectedFlatIndex + delta + totalResultCount) % totalResultCount
        clampSelection()
    }

    function activateMainWindow() {
        if (!mainWindow) {
            return
        }
        if (controller
                && typeof controller.restoreMainWindow === "function"
                && controller.restoreMainWindow(mainWindow)) {
            return
        }
        if (typeof mainWindow.restoreToForeground === "function") {
            mainWindow.restoreToForeground()
        } else {
            mainWindow.showNormal()
            mainWindow.raise()
            mainWindow.requestActivate()
        }
    }

    function showMainWindow(forceCloseQuickSearch) {
        var shouldCloseQuickSearch = forceCloseQuickSearch || !pinned

        // 快捷搜索仍持有前台权限时先恢复主窗口，避免隐藏到托盘后激活请求被 Windows 拒绝。
        root.activateMainWindow()
        if (shouldCloseQuickSearch) {
            hideSearch()
        }

        // 快捷搜索隐藏完成后再次确认主窗口为最终前台窗口。
        Qt.callLater(function() {
            root.activateMainWindow()
        })
    }

    function activateFolderResult(folderKeyValue, locateOnly) {
        var viewModel = materialCenterViewModel
        if (!viewModel || !folderKeyValue || folderKeyValue.length === 0) {
            return
        }
        if (locateOnly) {
            viewModel.locateFolder(folderKeyValue)
            hideSearch()
            return
        }

        // 打开工程会同步重建搜索模型，必须在不会随 delegate 销毁的根窗口上下文中继续恢复主窗口。
        viewModel.openQuickSearchFolderResult(folderKeyValue)
        showMainWindow(true)
    }

    function activatePrimaryResult(videoKeyValue, resultIndex, locateOnly) {
        var viewModel = materialCenterViewModel
        if (!viewModel || !videoKeyValue || videoKeyValue.length === 0) {
            return
        }
        if (locateOnly) {
            viewModel.selectVideo(videoKeyValue)
            viewModel.locateSelectedSource()
            hideSearch()
            return
        }

        // 该调用可能同步销毁当前结果 delegate；后续动作只能依赖仍然存活的根窗口。
        viewModel.openQuickSearchResultAtIndex(videoKeyValue, resultIndex)
        showMainWindow(true)
    }

    function activateCurrent(locateOnly) {
        clampSelection()
        var viewModel = materialCenterViewModel
        if (totalResultCount <= 0 || !viewModel) {
            showMainWindow()
            return
        }
        if (selectedFlatIndex < folderResultCount) {
            activateFolderResult(
                        viewModel.quickSearchFolderKeyAt(selectedFlatIndex),
                        locateOnly)
            return
        }
        var resultIndex = selectedFlatIndex - folderResultCount
        activatePrimaryResult(viewModel.quickSearchVideoKeyAt(resultIndex),
                              resultIndex,
                              locateOnly)
    }

    onTotalResultCountChanged: clampSelection()
    onActiveChanged: if (visible && !active && !pinned) focusLossTimer.restart()
    onPinnedChanged: {
        focusLossTimer.stop()
        if (!pinned && visible && !active) {
            focusLossTimer.restart()
        }
    }

    Timer {
        id: focusLossTimer
        interval: 350
        onTriggered: if (root.visible && !root.active && !root.pinned) root.hideSearch()
    }

    Timer {
        id: searchDebounce
        interval: 80
        onTriggered: {
            root.selectedFlatIndex = 0
            if (root.materialCenterViewModel) {
                root.materialCenterViewModel.setSearchText(searchField.text)
            }
        }
    }

    Connections {
        target: root.controller
        function onQuickSearchRequested() { root.openSearch() }
        function onShowMainWindowRequested() { root.showMainWindow() }
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        onActivated: root.hideSearch()
    }

    Rectangle {
        id: panel
        anchors.fill: parent
        anchors.margins: 8
        radius: 20
        color: root.quickBg
        border.width: 1
        border.color: root.quickLine

        Item {
            id: dragStrip
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 16
            z: 20

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 5
                width: 44
                height: 4
                radius: 2
                color: root.quickLine
            }

            DragHandler {
                id: windowDragHandler
                target: null
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.SizeAllCursor
                onActiveChanged: {
                    if (active) {
                        root.startSystemMove()
                    } else {
                        Qt.callLater(root.rememberPosition)
                    }
                }
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 86
                radius: 20
                color: root.quickHeader

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 20
                    color: parent.color
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 12

                    Rectangle {
                        Layout.preferredWidth: 46
                        Layout.preferredHeight: 46
                        radius: 14
                        color: root.quickSurface
                        border.width: 1
                        border.color: root.quickLine

                        Text {
                            anchors.centerIn: parent
                            text: "⌕"
                            color: root.quickAccent
                            font.pixelSize: 28
                            font.weight: Font.Bold
                        }
                    }

                    ThemedTextField {
                        id: searchField
                        objectName: "quickSearchField"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 52
                        font.pixelSize: 19
                        placeholderText: "描述你要找的画面、日期、文件夹或素材…"
                        color: root.quickText
                        placeholderTextColor: root.quickWeak
                        selectionColor: root.quickSelectedLine
                        selectedTextColor: root.quickText
                        background: Rectangle {
                            radius: 12
                            color: searchField.hovered ? root.quickSurfaceHover : root.quickSurface
                            border.width: 1
                            border.color: searchField.activeFocus
                                ? root.quickSelectedLine : root.quickLine
                        }
                        onTextEdited: searchDebounce.restart()
                        onAccepted: root.activateCurrent(false)
                        Keys.onDownPressed: function(event) {
                            root.moveSelection(1)
                            event.accepted = true
                        }
                        Keys.onUpPressed: function(event) {
                            root.moveSelection(-1)
                            event.accepted = true
                        }
                        Keys.onTabPressed: function(event) {
                            root.moveSelection(1)
                            event.accepted = true
                        }
                        Keys.onBacktabPressed: function(event) {
                            root.moveSelection(-1)
                            event.accepted = true
                        }
                        Keys.onPressed: function(event) {
                            if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Return) {
                                root.activateCurrent(true)
                                event.accepted = true
                            } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_R) {
                                searchDebounce.restart()
                                event.accepted = true
                            } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_I) {
                                root.showMainWindow()
                                if (root.shellViewModel) root.shellViewModel.openSettings()
                                event.accepted = true
                            }
                        }
                    }

                    Button {
                        id: pinButton
                        objectName: "quickSearchPinButton"
                        Layout.preferredWidth: 106
                        Layout.preferredHeight: 38
                        padding: 0
                        hoverEnabled: true
                        ToolTip.visible: hovered
                        ToolTip.text: root.pinned
                            ? "取消固定后，快捷搜索会在失去焦点时自动收起"
                            : "固定后切换到主窗口，快捷搜索仍会保持显示"

                        background: Rectangle {
                            radius: 10
                            color: root.pinned
                                ? root.quickSelected
                                : (pinButton.hovered ? root.quickSurfaceHover : root.quickSurface)
                            border.width: 1
                            border.color: root.pinned ? root.quickSelectedLine : root.quickLine
                        }

                        contentItem: RowLayout {
                            spacing: 6
                            Text {
                                text: "📌"
                                color: root.quickAccent
                                font.pixelSize: 14
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.pinned ? "已固定" : "固定显示"
                                color: root.pinned ? root.quickText : root.quickMuted
                                font.pixelSize: 12
                                font.weight: root.pinned ? Font.DemiBold : Font.Normal
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }

                        onClicked: {
                            root.pinned = !root.pinned
                            searchField.forceActiveFocus()
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: shortcutText.implicitWidth + 20
                        Layout.preferredHeight: 30
                        radius: 8
                        color: root.quickSurface
                        border.width: 1
                        border.color: root.quickLine

                        Text {
                            id: shortcutText
                            anchors.centerIn: parent
                            text: root.controller ? root.controller.shortcut : "Alt+Space"
                            color: root.quickMuted
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "transparent"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    anchors.topMargin: 10
                    anchors.bottomMargin: 8
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 8
                        Layout.rightMargin: 8
                        spacing: 7

                        Text {
                            text: "搜索命中"
                            color: root.quickText
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }

                        Item { Layout.fillWidth: true }

                        Repeater {
                            model: [
                                { label: "智能", value: 0 },
                                { label: "视频", value: 1 },
                                { label: "帧画面", value: 2 },
                                { label: "图片", value: 3 },
                                { label: "文档", value: 4 }
                            ]

                            delegate: Button {
                                id: filterButton
                                required property var modelData
                                readonly property bool selected: root.materialCenterViewModel
                                    && root.materialCenterViewModel.searchResultFilter === modelData.value

                                objectName: "quickSearchResultFilter" + modelData.value
                                Layout.preferredWidth: modelData.value === 2 ? 72 : 62
                                Layout.preferredHeight: 30
                                padding: 0

                                background: Rectangle {
                                    radius: 9
                                    color: filterButton.selected
                                        ? root.quickSelected
                                        : (filterButton.hovered ? root.quickSurfaceHover : root.quickSurface)
                                    border.width: 1
                                    border.color: filterButton.selected ? root.quickSelectedLine : root.quickLine
                                }

                                contentItem: Text {
                                    text: modelData.label
                                    color: filterButton.selected ? root.quickText : root.quickMuted
                                    font.pixelSize: 12
                                    font.weight: filterButton.selected ? Font.DemiBold : Font.Normal
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }

                                onClicked: {
                                    root.selectedFlatIndex = 0
                                    if (root.materialCenterViewModel) {
                                        root.materialCenterViewModel.setSearchResultFilter(modelData.value)
                                    }
                                    searchField.forceActiveFocus()
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: root.quickLine
                    }

                    Text {
                        visible: !root.hasQuery
                        Layout.fillWidth: true
                        Layout.topMargin: 44
                        text: "输入自然语言开始全局素材搜索\n例如：上周三拍摄的红色汽车、海边日落的航拍画面"
                        color: root.quickMuted
                        font.pixelSize: 16
                        horizontalAlignment: Text.AlignHCenter
                        lineHeight: 1.55
                    }

                    RowLayout {
                        visible: root.hasQuery && root.folderResultCount > 0
                        Layout.fillWidth: true
                        Layout.leftMargin: 8
                        Layout.rightMargin: 8

                        Text {
                            text: "文件夹"
                            color: root.quickText
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: root.folderResultCount + " 项"
                            color: root.quickWeak
                            font.pixelSize: 12
                        }
                    }

                    ListView {
                        id: folderList
                        visible: root.hasQuery && count > 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(count * 58, 132)
                        clip: true
                        spacing: 4
                        model: root.materialCenterViewModel ? root.materialCenterViewModel.folderModel : null
                        currentIndex: root.folderResultCount > 0 ? 0 : -1

                        MiddleDragScrollHandler {
                            flickable: folderList
                        }

                        delegate: Rectangle {
                            id: folderDelegate
                            required property int index
                            required property string folderKey
                            required property string name
                            required property string relativePath
                            required property string absolutePath
                            required property string projectName
                            required property int recursiveFileCount
                            property string folderKeyValue: folderKey
                            property string folderNameValue: name.length > 0 ? name : "未命名文件夹"
                            property string folderPathValue: relativePath.length > 0 ? relativePath : absolutePath

                            width: ListView.view.width
                            height: 54
                            radius: 12
                            color: root.selectedFlatIndex === index ? root.quickSelected : "transparent"
                            border.width: root.selectedFlatIndex === index ? 1 : 0
                            border.color: root.quickSelectedLine

                            function activate(locateOnly) {
                                root.activateFolderResult(folderKeyValue, locateOnly)
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 12

                                Text {
                                    text: "▰"
                                    color: root.quickAccent
                                    font.pixelSize: 22
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        Layout.fillWidth: true
                                        text: folderDelegate.folderNameValue
                                        color: root.quickText
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: folderDelegate.projectName + "  ·  " + folderDelegate.folderPathValue
                                        color: root.quickMuted
                                        font.pixelSize: 12
                                        elide: Text.ElideMiddle
                                    }
                                }
                                Text {
                                    text: folderDelegate.recursiveFileCount + " 个文件"
                                    color: root.quickWeak
                                    font.pixelSize: 12
                                }
                            }

                            ThemedMenu {
                                id: folderContextMenu

                                ThemedMenuItem {
                                    text: "打开所在目录"
                                    onTriggered: if (root.materialCenterViewModel) {
                                        root.materialCenterViewModel.locateFolder(folderDelegate.folderKeyValue)
                                    }
                                }
                                ThemedMenuItem {
                                    text: "复制文件路径"
                                    onTriggered: if (root.materialCenterViewModel) {
                                        root.materialCenterViewModel.copyFolderPath(folderDelegate.folderKeyValue)
                                    }
                                }
                            }

                            MouseArea {
                                id: folderMouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onEntered: root.selectedFlatIndex = index
                                onClicked: function(mouse) {
                                    root.selectedFlatIndex = index
                                    if (mouse.button === Qt.RightButton) {
                                        folderContextMenu.popup(folderMouseArea, mouse.x, mouse.y)
                                    }
                                }
                                onDoubleClicked: folderDelegate.activate(mouse.modifiers & Qt.ControlModifier)
                            }
                        }
                    }

                    RowLayout {
                        visible: root.hasQuery && root.primaryResultCount > 0
                        Layout.fillWidth: true
                        Layout.leftMargin: 8
                        Layout.rightMargin: 8

                        Text {
                            text: root.materialCenterViewModel && root.materialCenterViewModel.frameSearchMode
                                ? "画面" : "素材"
                            color: root.quickText
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: root.primaryResultCount + " 项"
                            color: root.quickWeak
                            font.pixelSize: 12
                        }
                    }

                    ListView {
                        id: resultList
                        visible: root.hasQuery && count > 0
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 5
                        currentIndex: root.folderResultCount === 0 && count > 0 ? 0 : -1
                        model: !root.materialCenterViewModel
                            ? null
                            : (root.materialCenterViewModel.frameSearchMode
                                ? root.materialCenterViewModel.frameModel
                                : root.materialCenterViewModel.assetModel)

                        MiddleDragScrollHandler {
                            flickable: resultList
                        }

                        delegate: Rectangle {
                            id: resultDelegate
                            required property int index
                            required property string videoKey
                            required property string fileName
                            required property string projectName
                            required property string sourceName
                            required property string relativePath
                            required property string assetTypeLabel
                            required property int resultRank
                            required property string quickPreviewPath
                            required property string quickDetail
                            required property string quickMeta
                            required property string quickReasons
                            property bool frameResult: root.materialCenterViewModel
                                && root.materialCenterViewModel.frameSearchMode
                            property string videoKeyValue: videoKey
                            property string fileNameValue: fileName.length > 0 ? fileName : "未命名素材"
                            property string previewPathValue: quickPreviewPath
                            readonly property int flatIndex: root.folderResultCount + index

                            width: ListView.view.width
                            height: 104
                            radius: 13
                            color: root.selectedFlatIndex === flatIndex ? root.quickSelected : "transparent"
                            border.width: root.selectedFlatIndex === flatIndex ? 1 : 0
                            border.color: root.quickSelectedLine

                            function activate(locateOnly) {
                                root.activatePrimaryResult(videoKeyValue, index, locateOnly)
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 12

                                Rectangle {
                                    Layout.preferredWidth: 124
                                    Layout.fillHeight: true
                                    radius: 10
                                    color: root.quickSurface
                                    clip: true

                                    Image {
                                        id: previewImage
                                        anchors.fill: parent
                                        source: root.imageSource(resultDelegate.previewPathValue)
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        cache: true
                                        smooth: true
                                    }

                                    Text {
                                        anchors.centerIn: parent
                                        visible: previewImage.status !== Image.Ready
                                        text: resultDelegate.assetTypeLabel.length > 0
                                            ? resultDelegate.assetTypeLabel
                                            : (resultDelegate.frameResult ? "画面" : "素材")
                                        color: root.quickWeak
                                        font.pixelSize: 12
                                    }

                                    Rectangle {
                                        visible: previewImage.status === Image.Ready
                                            && resultDelegate.frameResult
                                            && resultDelegate.quickMeta.length > 0
                                        anchors.left: parent.left
                                        anchors.bottom: parent.bottom
                                        anchors.margins: 5
                                        width: previewTime.implicitWidth + 10
                                        height: 21
                                        radius: 6
                                        color: "#B80E1014"

                                        Text {
                                            id: previewTime
                                            anchors.centerIn: parent
                                            text: resultDelegate.quickMeta.split(" · ")[0]
                                            color: root.quickText
                                            font.pixelSize: 10
                                        }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text {
                                            Layout.fillWidth: true
                                            text: resultDelegate.fileNameValue
                                            color: root.quickText
                                            font.pixelSize: 15
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            text: "#" + resultDelegate.resultRank
                                            color: root.quickAccent
                                            font.pixelSize: 12
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: resultDelegate.projectName + "  ·  " + resultDelegate.sourceName
                                        color: root.quickMuted
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: resultDelegate.quickDetail.length > 0
                                            ? resultDelegate.quickDetail
                                            : resultDelegate.relativePath
                                        color: root.quickText
                                        font.pixelSize: 13
                                        elide: Text.ElideRight
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 10

                                        Text {
                                            Layout.fillWidth: true
                                            text: resultDelegate.quickReasons.length > 0
                                                ? resultDelegate.quickMeta + "  ·  " + resultDelegate.quickReasons
                                                : resultDelegate.quickMeta
                                            color: root.quickWeak
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            visible: root.selectedFlatIndex === resultDelegate.flatIndex
                                            text: "打开详情  →"
                                            color: root.quickAccent
                                            font.pixelSize: 11
                                            font.weight: Font.DemiBold
                                        }
                                    }
                                }
                            }

                            ThemedMenu {
                                id: resultContextMenu

                                ThemedMenuItem {
                                    text: "打开所在目录"
                                    onTriggered: if (root.materialCenterViewModel) {
                                        root.materialCenterViewModel.openAssetFolder(resultDelegate.videoKeyValue)
                                    }
                                }
                                ThemedMenuItem {
                                    text: "复制文件路径"
                                    onTriggered: if (root.materialCenterViewModel) {
                                        root.materialCenterViewModel.copyAssetPath(resultDelegate.videoKeyValue)
                                    }
                                }
                            }

                            MouseArea {
                                id: resultMouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onEntered: root.selectedFlatIndex = resultDelegate.flatIndex
                                onClicked: function(mouse) {
                                    root.selectedFlatIndex = resultDelegate.flatIndex
                                    if (root.materialCenterViewModel) {
                                        root.materialCenterViewModel.selectVideo(resultDelegate.videoKeyValue)
                                    }
                                    if (mouse.button === Qt.RightButton) {
                                        resultContextMenu.popup(resultMouseArea, mouse.x, mouse.y)
                                    }
                                }
                                onDoubleClicked: resultDelegate.activate(mouse.modifiers & Qt.ControlModifier)
                            }
                        }
                    }

                    Item {
                        visible: root.hasQuery && root.totalResultCount === 0
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        Column {
                            anchors.centerIn: parent
                            width: parent.width - 40
                            spacing: 10
                            Text {
                                width: parent.width
                                text: root.materialCenterViewModel && root.materialCenterViewModel.searchAssistantBusy
                                    ? "正在理解并搜索…"
                                    : "没有找到匹配结果"
                                color: root.quickText
                                font.pixelSize: 17
                                font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                            }
                            Text {
                                width: parent.width
                                text: root.materialCenterViewModel
                                    ? (root.materialCenterViewModel.searchEmptyReason || root.materialCenterViewModel.searchAssistantStatusText)
                                    : "搜索服务尚未初始化"
                                color: root.quickMuted
                                font.pixelSize: 13
                                wrapMode: Text.Wrap
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        color: "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            spacing: 16

                            Text { text: "↑↓ / Tab 选择"; color: root.quickWeak; font.pixelSize: 11 }
                            Text { text: "双击 / Enter 打开"; color: root.quickWeak; font.pixelSize: 11 }
                            Text { text: "Ctrl+Enter 定位"; color: root.quickWeak; font.pixelSize: 11 }
                            Text { text: "Esc 收起"; color: root.quickWeak; font.pixelSize: 11 }
                            Item { Layout.fillWidth: true }
                            Text {
                                visible: root.materialCenterViewModel && root.materialCenterViewModel.searchAssistantBusy
                                text: "内置文本模型增强中…"
                                color: root.quickAccent
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }
        }
    }
}
