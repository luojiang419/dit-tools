import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CineVault

Rectangle {
    id: root

    property var viewModel
    property var shellVm
    property string imageViewerSource: ""
    property real imageViewerScale: 1.0
    property real imageViewerOffsetX: 0
    property real imageViewerOffsetY: 0
    property bool quickSearchRevealActive: false
    property int handledQuickSearchRevealRevision: -1

    function imageFileSource(path) {
        return path && path.length > 0 && localImageUrlHelper
            ? localImageUrlHelper.sourceForInput(path)
            : ""
    }

    function openImageViewer(source) {
        var normalizedSource = String(source || "")
        if (normalizedSource.length === 0) {
            return
        }
        imageViewerSource = normalizedSource
        imageViewerScale = 1.0
        imageViewerOffsetX = 0
        imageViewerOffsetY = 0
        imageViewerOverlay.forceActiveFocus()
    }

    function closeImageViewer() {
        imageViewerSource = ""
    }

    function optionIndex(options, value) {
        for (var index = 0; index < options.length; ++index) {
            if (String(options[index].value) === String(value)) {
                return index
            }
        }
        return 0
    }

    function searchNoticeText() {
        if (!viewModel) {
            return ""
        }
        var notices = []
        if (viewModel.searchInterpretationText.length > 0) {
            notices.push("已理解：" + viewModel.searchInterpretationText)
        }
        if (viewModel.searchAssistantStatusText.length > 0) {
            notices.push(viewModel.searchAssistantStatusText)
        }
        if (viewModel.searchWarningMessage.length > 0) {
            notices.push(viewModel.searchWarningMessage)
        }
        if (viewModel.excludedPartialCount > 0) {
            notices.push("另有 " + viewModel.excludedPartialCount + " 条候选因结构化视觉事实不完整而未纳入结果")
        }
        return notices.join(" · ")
    }

    function revealQuickSearchResult() {
        if (!viewModel) {
            return
        }
        if (viewModel.quickSearchRevealVideoKey.length === 0) {
            quickSearchRevealActive = false
            return
        }
        var targetIndex = viewModel.quickSearchRevealIndex
        if (targetIndex < 0) {
            return
        }
        if (viewModel.quickSearchRevealRevision !== handledQuickSearchRevealRevision) {
            handledQuickSearchRevealRevision = viewModel.quickSearchRevealRevision
            quickSearchRevealActive = true
            quickSearchRevealTimer.restart()
        }
        if (viewModel.frameSearchMode) {
            frameResultGrid.currentIndex = targetIndex
            frameResultGrid.positionViewAtIndex(targetIndex, GridView.Center)
        } else {
            resultList.positionViewAtIndex(targetIndex, ListView.Center)
        }
    }

    color: Theme.bg
    focus: true

    Keys.onPressed: function(event) {
        if (!viewModel || root.imageViewerSource.length > 0) {
            return
        }
        if (event.key === Qt.Key_Up || event.key === Qt.Key_Left) {
            viewModel.moveVideoSelection(-1)
            event.accepted = true
        } else if (event.key === Qt.Key_Down || event.key === Qt.Key_Right) {
            viewModel.moveVideoSelection(1)
            event.accepted = true
        }
    }

    Component.onCompleted: {
        if (viewModel) {
            viewModel.reload()
        }
        Qt.callLater(root.revealQuickSearchResult)
    }
    onVisibleChanged: if (visible) Qt.callLater(root.revealQuickSearchResult)

    Connections {
        target: root.viewModel
        function onQuickSearchRevealChanged() {
            Qt.callLater(root.revealQuickSearchResult)
        }
        function onSearchStateChanged() {
            if (root.quickSearchRevealActive) {
                Qt.callLater(root.revealQuickSearchResult)
            }
        }
    }

    Timer {
        id: quickSearchRevealTimer
        interval: 3200
        onTriggered: root.quickSearchRevealActive = false
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        Rectangle {
            Layout.fillWidth: true
            radius: 20
            color: Theme.panel2
            border.width: 1
            border.color: Theme.line
            implicitHeight: 138

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true

                    ColumnLayout {
                        spacing: 6

                        Text {
                            text: "素材管理中心"
                            color: Theme.text
                            font.pixelSize: 28
                            font.weight: Font.Black
                        }

                        Text {
                            text: viewModel ? viewModel.statusText : ""
                            color: Theme.muted
                            font.pixelSize: 13
                        }
                    }

                    Item { Layout.fillWidth: true }

                    ActionButton {
                        Layout.preferredWidth: 100
                        Layout.preferredHeight: 36
                        text: "同步当前项目"
                        onClicked: if (viewModel) viewModel.syncCurrentProject()
                    }

                    ActionButton {
                        Layout.preferredWidth: 112
                        Layout.preferredHeight: 36
                        text: "多维度解析"
                        enabled: viewModel
                        onClicked: multiDimensionAnalyzeDialog.open()
                    }

                    ActionButton {
                        Layout.preferredWidth: 100
                        Layout.preferredHeight: 36
                        text: "重建全局索引"
                        primary: true
                        onClicked: if (viewModel) viewModel.rebuildGlobalIndex()
                    }

                    ActionButton {
                        Layout.preferredWidth: 132
                        Layout.preferredHeight: 36
                        text: "立即更新语义索引"
                        enabled: viewModel && !viewModel.semanticIndexing
                        onClicked: if (viewModel) viewModel.updateSemanticIndexNow()
                    }

                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    ThemedTextField {
                        id: searchInput
                        Layout.preferredWidth: 240
                        Layout.fillWidth: true
                        placeholderText: "用自然语言搜索文件夹、素材、视觉帧、日期、类型或画面内容"
                        text: shellVm ? shellVm.globalSearchText : ""
                        onTextChanged: if (shellVm) shellVm.globalSearchText = text
                    }

                    ActionButton {
                        Layout.preferredWidth: 88
                        Layout.preferredHeight: 36
                        text: "清空输入"
                        enabled: searchInput.text.length > 0
                        onClicked: {
                            if (shellVm) {
                                shellVm.globalSearchText = ""
                            } else {
                                searchInput.clear()
                            }
                            searchInput.forceActiveFocus()
                        }
                    }

                    ThemedComboBox {
                        Layout.preferredWidth: 160
                        model: viewModel ? viewModel.projectOptions : []
                        textRole: "label"
                        currentIndex: viewModel ? root.optionIndex(viewModel.projectOptions, viewModel.projectFilter) : 0
                        onActivated: if (viewModel) viewModel.setProjectFilter(model[index].value)
                    }

                    ThemedComboBox {
                        Layout.preferredWidth: 160
                        model: viewModel ? viewModel.sourceOptions : []
                        textRole: "label"
                        currentIndex: viewModel ? root.optionIndex(viewModel.sourceOptions, viewModel.sourceFilter) : 0
                        onActivated: if (viewModel) viewModel.setSourceFilter(model[index].value)
                    }

                    ThemedComboBox {
                        Layout.preferredWidth: 136
                        model: viewModel ? viewModel.assetTypeOptions : []
                        textRole: "label"
                        currentIndex: viewModel ? root.optionIndex(viewModel.assetTypeOptions, viewModel.assetTypeFilter) : 0
                        onActivated: if (viewModel) viewModel.setAssetTypeFilter(model[index].value)
                    }

                    ThemedComboBox {
                        Layout.preferredWidth: 120
                        model: viewModel ? viewModel.analysisStatusOptions : []
                        textRole: "label"
                        currentIndex: viewModel ? root.optionIndex(viewModel.analysisStatusOptions, viewModel.analysisStatusFilter) : 0
                        onActivated: if (viewModel) viewModel.setAnalysisStatusFilter(model[index].value)
                    }

                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: visible ? 42 : 0
            visible: viewModel && viewModel.hasActiveSearch
            radius: 14
            color: viewModel && viewModel.semanticSearchAvailable
                ? Qt.rgba(0.22, 0.66, 0.46, 0.12)
                : Qt.rgba(0.94, 0.63, 0.20, 0.12)
            border.width: 1
            border.color: viewModel && viewModel.semanticSearchAvailable
                ? Qt.rgba(0.22, 0.66, 0.46, 0.42)
                : Qt.rgba(0.94, 0.63, 0.20, 0.42)

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 10

                Text {
                    text: viewModel ? viewModel.semanticSearchStatusText : ""
                    color: viewModel && viewModel.semanticSearchAvailable ? Theme.green : Theme.orange
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 18
                    visible: root.searchNoticeText().length > 0
                    color: Theme.line
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.searchNoticeText().length > 0
                    text: root.searchNoticeText()
                    color: Theme.muted
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: viewModel && viewModel.searchEmptyReason.length > 0
            text: viewModel ? viewModel.searchEmptyReason : ""
            color: Theme.orange
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }

        Text {
            Layout.fillWidth: true
            visible: viewModel && viewModel.message.length > 0
            text: viewModel ? viewModel.message : ""
            color: Theme.muted
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 20
                color: Theme.panel2
                border.width: 1
                border.color: Theme.line

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: "搜索命中"
                            color: Theme.text
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                        }

                        Text {
                            text: viewModel
                                ? "· " + (viewModel.folderCount + viewModel.assetCount + viewModel.frameCount)
                                : ""
                            color: Theme.muted
                            font.pixelSize: 12
                        }

                        Item { Layout.fillWidth: true }

                        ActionButton {
                            objectName: "searchResultFilterSmart"
                            Layout.preferredWidth: 68
                            Layout.preferredHeight: 32
                            text: "智能"
                            primary: viewModel && viewModel.searchResultFilter === 0
                            onClicked: if (viewModel) viewModel.setSearchResultFilter(0)
                        }

                        ActionButton {
                            objectName: "searchResultFilterVideo"
                            Layout.preferredWidth: 68
                            Layout.preferredHeight: 32
                            text: "视频"
                            primary: viewModel && viewModel.searchResultFilter === 1
                            onClicked: if (viewModel) viewModel.setSearchResultFilter(1)
                        }

                        ActionButton {
                            objectName: "searchResultFilterFrames"
                            Layout.preferredWidth: 78
                            Layout.preferredHeight: 32
                            text: "帧画面"
                            primary: viewModel && viewModel.searchResultFilter === 2
                            onClicked: if (viewModel) viewModel.setSearchResultFilter(2)
                        }

                        ActionButton {
                            objectName: "searchResultFilterImage"
                            Layout.preferredWidth: 68
                            Layout.preferredHeight: 32
                            text: "图片"
                            primary: viewModel && viewModel.searchResultFilter === 3
                            onClicked: if (viewModel) viewModel.setSearchResultFilter(3)
                        }

                        ActionButton {
                            objectName: "searchResultFilterDocument"
                            Layout.preferredWidth: 68
                            Layout.preferredHeight: 32
                            text: "文档"
                            primary: viewModel && viewModel.searchResultFilter === 4
                            onClicked: if (viewModel) viewModel.setSearchResultFilter(4)
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.line
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: viewModel && viewModel.frameSearchMode

                        Text {
                            text: "视觉帧命中"
                            color: Theme.text
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                        }

                        Text {
                            text: viewModel ? "· " + viewModel.frameCount : ""
                            color: Theme.muted
                            font.pixelSize: 12
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: "结果显示在中央操作区 · 按相关度排序"
                            color: Theme.blue
                            font.pixelSize: 11
                        }
                    }

                    GridView {
                        id: frameResultGrid
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: viewModel && viewModel.frameSearchMode
                        clip: true
                        reuseItems: true
                        cacheBuffer: 900
                        property real cardWidth: width >= 1120 ? (width - 10) / 2 : width
                        cellWidth: cardWidth
                        cellHeight: 310
                        model: viewModel ? viewModel.frameModel : null

                        MiddleDragScrollHandler {
                            flickable: frameResultGrid
                        }

                        ScrollBar.vertical: ThemedScrollBar {
                            policy: ScrollBar.AsNeeded
                        }

                        delegate: Rectangle {
                            id: frameResultCard
                            readonly property bool quickSearchReveal: root.quickSearchRevealActive
                                && viewModel
                                && viewModel.quickSearchRevealVideoKey === videoKey
                                && (viewModel.quickSearchRevealFrameNumber < 0
                                    || viewModel.quickSearchRevealFrameNumber === frameNumber)

                            width: frameResultGrid.cardWidth - 10
                            height: 298
                            radius: 18
                            color: quickSearchReveal ? Qt.rgba(0.20, 0.48, 0.95, 0.18) : Theme.bg
                            border.width: quickSearchReveal ? 3 : 1
                            border.color: quickSearchReveal ? Theme.blue : Theme.line

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 14

                                Rectangle {
                                    Layout.preferredWidth: 184
                                    Layout.fillHeight: true
                                    radius: 14
                                    color: Theme.mediaSurface
                                    border.width: 1
                                    border.color: Theme.line
                                    clip: true

                                    Image {
                                        anchors.fill: parent
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                        cache: true
                                        source: root.imageFileSource(imagePath)
                                    }

                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        anchors.margins: 8
                                        width: frameRankText.implicitWidth + 16
                                        height: 26
                                        radius: 13
                                        color: Qt.rgba(0.02, 0.03, 0.05, 0.82)
                                        border.width: 1
                                        border.color: Qt.rgba(1, 1, 1, 0.24)

                                        Text {
                                            id: frameRankText
                                            anchors.centerIn: parent
                                            text: "#" + resultRank
                                            color: "white"
                                            font.pixelSize: 12
                                            font.weight: Font.Bold
                                        }
                                    }

                                    Text {
                                        anchors.centerIn: parent
                                        visible: imagePath.length === 0
                                        text: "暂无帧缩略图"
                                        color: Theme.muted
                                        font.pixelSize: 12
                                    }

                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        height: 34
                                        color: Qt.rgba(0.02, 0.03, 0.05, 0.78)

                                        Text {
                                            anchors.centerIn: parent
                                            text: "第 " + frameNumber + " 帧 · " + timestampLabel
                                            color: "white"
                                            font.pixelSize: 12
                                            font.weight: Font.DemiBold
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        enabled: imagePath.length > 0
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.openImageViewer(root.imageFileSource(imagePath))
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    spacing: 5

                                    RowLayout {
                                        Layout.fillWidth: true

                                        Text {
                                            Layout.fillWidth: true
                                            text: "#" + resultRank + " · 第 " + frameNumber + " 帧 · " + timestampLabel
                                            color: Theme.text
                                            font.pixelSize: 16
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            text: "置信 " + Math.round(confidence * 100) + "%"
                                            color: Theme.blue
                                            font.pixelSize: 11
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: fileName + " · " + assetTypeLabel + " · " + projectName + " · " + sourceName
                                        color: Theme.muted
                                        font.pixelSize: 11
                                        elide: Text.ElideMiddle
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: caption.length > 0 ? caption : "该帧暂无画面描述"
                                        color: Theme.text
                                        font.pixelSize: 13
                                        wrapMode: Text.Wrap
                                        maximumLineCount: 3
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        visible: tags.length > 0
                                        text: "标签：" + tags
                                        color: Theme.muted
                                        font.pixelSize: 11
                                        maximumLineCount: 1
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        visible: objects.length > 0
                                        text: "对象：" + objects
                                        color: Theme.muted
                                        font.pixelSize: 11
                                        maximumLineCount: 1
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        visible: entitySummary.length > 0
                                        text: "实体属性：" + entitySummary
                                        color: Theme.blue
                                        font.pixelSize: 11
                                        maximumLineCount: 1
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        visible: setting.length > 0 || actions.length > 0
                                        text: "场景/动作：" + setting + (setting.length > 0 && actions.length > 0 ? " · " : "") + actions
                                        color: Theme.muted
                                        font.pixelSize: 11
                                        maximumLineCount: 1
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        visible: ocrText.length > 0
                                        text: "OCR：" + ocrText
                                        color: Theme.orange
                                        font.pixelSize: 11
                                        maximumLineCount: 1
                                        elide: Text.ElideRight
                                    }

                                    Item { Layout.fillHeight: true }

                                    Text {
                                        Layout.fillWidth: true
                                        text: reasons
                                        color: Theme.blue
                                        font.pixelSize: 11
                                        maximumLineCount: 2
                                        wrapMode: Text.Wrap
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: relativePath
                                        color: Theme.muted
                                        font.pixelSize: 10
                                        elide: Text.ElideMiddle
                                    }
                                }
                            }

                            Rectangle {
                                id: frameRevealRing
                                anchors.fill: parent
                                anchors.margins: 5
                                radius: parent.radius - 4
                                color: "transparent"
                                border.width: 2
                                border.color: Theme.blue
                                visible: frameResultCard.quickSearchReveal
                                z: 20

                                SequentialAnimation on opacity {
                                    running: frameRevealRing.visible
                                    loops: Animation.Infinite
                                    NumberAnimation { from: 1.0; to: 0.28; duration: 480; easing.type: Easing.InOutQuad }
                                    NumberAnimation { from: 0.28; to: 1.0; duration: 480; easing.type: Easing.InOutQuad }
                                }
                            }

                            ThemedMenu {
                                id: frameContextMenu
                                property string targetVideoKey: ""

                                ThemedMenuItem {
                                    text: "打开所在目录"
                                    onTriggered: if (viewModel) {
                                        viewModel.openAssetFolder(frameContextMenu.targetVideoKey)
                                    }
                                }
                                ThemedMenuItem {
                                    text: "复制文件路径"
                                    onTriggered: if (viewModel) {
                                        viewModel.copyAssetPath(frameContextMenu.targetVideoKey)
                                    }
                                }
                            }

                            MouseArea {
                                id: frameContextArea
                                anchors.fill: parent
                                acceptedButtons: Qt.RightButton
                                onClicked: function(mouse) {
                                    if (viewModel) {
                                        viewModel.selectVideo(videoKey)
                                    }
                                    frameContextMenu.targetVideoKey = videoKey
                                    frameContextMenu.popup(frameContextArea, mouse.x, mouse.y)
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: viewModel && viewModel.folderCount > 0

                        Text {
                            text: "文件夹命中"
                            color: Theme.text
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }

                        Text {
                            text: viewModel ? "· " + viewModel.folderCount : ""
                            color: Theme.muted
                            font.pixelSize: 12
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: "按混合检索相关度排序"
                            color: Theme.muted
                            font.pixelSize: 11
                        }
                    }

                    ListView {
                        id: folderResultList
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? Math.min(220, contentHeight) : 0
                        visible: viewModel && viewModel.folderCount > 0
                        clip: true
                        spacing: 8
                        reuseItems: true
                        cacheBuffer: 360
                        model: viewModel ? viewModel.folderModel : null

                        MiddleDragScrollHandler {
                            flickable: folderResultList
                        }

                        ScrollBar.vertical: ThemedScrollBar {
                            policy: ScrollBar.AsNeeded
                        }

                        delegate: Rectangle {
                            id: folderResultCard

                            width: ListView.view.width
                            height: reasons.length > 0 ? 100 : 82
                            radius: 16
                            color: Theme.bg
                            border.width: 1
                            border.color: Theme.line

                            MouseArea {
                                anchors.fill: parent
                                anchors.rightMargin: 206
                                cursorShape: available ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onDoubleClicked: if (viewModel && available) viewModel.locateFolder(folderKey)
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 12

                                Rectangle {
                                    Layout.preferredWidth: 54
                                    Layout.preferredHeight: 54
                                    radius: 14
                                    color: Qt.rgba(0.31, 0.55, 1.0, 0.14)
                                    border.width: 1
                                    border.color: Qt.rgba(0.31, 0.55, 1.0, 0.34)

                                    Text {
                                        anchors.centerIn: parent
                                        text: "目录"
                                        color: Theme.blue
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    RowLayout {
                                        Layout.fillWidth: true

                                        Text {
                                            Layout.fillWidth: true
                                            text: name
                                            color: Theme.text
                                            font.pixelSize: 15
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            text: "#" + resultRank + " · 理解置信 " + Math.round(confidence * 100) + "%"
                                            color: Theme.blue
                                            font.pixelSize: 11
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: projectName + " · " + sourceName
                                            + " · 直属 " + directFileCount + " / 共 " + recursiveFileCount + " 个文件"
                                        color: Theme.muted
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: relativePath.length > 0 ? relativePath : absolutePath
                                        color: available ? Theme.text : Theme.orange
                                        font.pixelSize: 12
                                        elide: Text.ElideMiddle
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        visible: reasons.length > 0
                                        text: reasons
                                        color: Theme.blue
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                }

                                ActionButton {
                                    Layout.preferredWidth: 88
                                    Layout.preferredHeight: 32
                                    text: "打开项目"
                                    enabled: viewModel
                                    onClicked: if (viewModel) viewModel.openFolderProject(folderKey)
                                }

                                ActionButton {
                                    Layout.preferredWidth: 88
                                    Layout.preferredHeight: 32
                                    text: available ? "定位目录" : "目录不可用"
                                    enabled: viewModel && available
                                    primary: available
                                    onClicked: if (viewModel) viewModel.locateFolder(folderKey)
                                }
                            }

                            ThemedMenu {
                                id: folderContextMenu

                                ThemedMenuItem {
                                    text: "打开所在目录"
                                    enabled: available
                                    onTriggered: if (viewModel) viewModel.locateFolder(folderKey)
                                }
                                ThemedMenuItem {
                                    text: "复制文件路径"
                                    onTriggered: if (viewModel) viewModel.copyFolderPath(folderKey)
                                }
                            }

                            MouseArea {
                                id: folderContextArea
                                anchors.fill: parent
                                acceptedButtons: Qt.RightButton
                                onClicked: function(mouse) {
                                    folderContextMenu.popup(folderContextArea, mouse.x, mouse.y)
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        visible: viewModel && viewModel.folderCount > 0
                        color: Theme.line
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: !viewModel || !viewModel.frameSearchMode

                        Text {
                            text: "素材命中"
                            color: Theme.text
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }

                        Text {
                            text: viewModel ? "· " + viewModel.assetCount : ""
                            color: Theme.muted
                            font.pixelSize: 12
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            visible: viewModel && viewModel.hasActiveSearch
                            text: "按混合检索相关度排序"
                            color: Theme.muted
                            font.pixelSize: 11
                        }
                    }

                    ListView {
                        id: resultList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: !viewModel || !viewModel.frameSearchMode
                        clip: true
                        spacing: 10
                        reuseItems: true
                        cacheBuffer: 720
                        model: viewModel ? viewModel.assetModel : null
                        currentIndex: viewModel ? viewModel.selectedVideoIndex : -1

                    MiddleDragScrollHandler {
                        flickable: resultList
                    }

                    ScrollBar.vertical: ThemedScrollBar {
                        policy: ScrollBar.AsNeeded
                    }

                    onCurrentIndexChanged: if (currentIndex >= 0) {
                        positionViewAtIndex(currentIndex, ListView.Contain)
                    }

                    delegate: Rectangle {
                        id: assetResultCard
                        readonly property bool quickSearchReveal: root.quickSearchRevealActive
                            && viewModel
                            && viewModel.quickSearchRevealVideoKey === videoKey

                        width: ListView.view.width
                        height: 118 + (searchReasons.length > 0 ? 20 : 0) + (errorMessage.length > 0 ? 20 : 0)
                        radius: 18
                        color: quickSearchReveal
                            ? Qt.rgba(0.20, 0.48, 0.95, 0.18)
                            : (viewModel && viewModel.selectedAssetKey === assetKey ? Theme.selectedBg : Theme.bg)
                        border.width: quickSearchReveal ? 3 : 1
                        border.color: quickSearchReveal
                            ? Theme.blue
                            : (viewModel && viewModel.selectedAssetKey === assetKey ? Theme.blue : Theme.line)

                        MouseArea {
                            anchors.fill: parent
                            anchors.rightMargin: 0
                            onClicked: {
                                root.forceActiveFocus()
                                if (viewModel) {
                                    viewModel.selectVideo(assetKey)
                                }
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 12

                            Rectangle {
                                Layout.preferredWidth: 164
                                Layout.preferredHeight: 92
                                radius: 14
                                color: Theme.mediaSurface
                                border.width: 1
                                border.color: Theme.line
                                clip: true

                                Image {
                                    anchors.fill: parent
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    cache: true
                                    source: root.imageFileSource(thumbnailPath)
                                    sourceSize.width: Math.max(1, Math.round(width))
                                    sourceSize.height: Math.max(1, Math.round(height))
                                }

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.margins: 8
                                    width: assetRankText.implicitWidth + 16
                                    height: 26
                                    radius: 13
                                    color: Qt.rgba(0.02, 0.03, 0.05, 0.82)
                                    border.width: 1
                                    border.color: Qt.rgba(1, 1, 1, 0.24)

                                    Text {
                                        id: assetRankText
                                        anchors.centerIn: parent
                                        text: "#" + resultRank
                                        color: "white"
                                        font.pixelSize: 12
                                        font.weight: Font.Bold
                                    }
                                }

                                Text {
                                    anchors.centerIn: parent
                                    visible: thumbnailPath.length === 0 && !thumbnailLoading
                                    text: "暂无缩略图"
                                    color: Theme.muted
                                    font.pixelSize: 12
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    visible: thumbnailLoading && thumbnailPath.length === 0
                                    color: Qt.rgba(0.03, 0.04, 0.06, 0.62)

                                    Column {
                                        anchors.centerIn: parent
                                        spacing: 8

                                        BusyIndicator {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            running: parent.parent.visible
                                            width: 28
                                            height: 28
                                        }

                                        Text {
                                            text: "缩略图生成中"
                                            color: Theme.text
                                            font.pixelSize: 12
                                            font.weight: Font.DemiBold
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    enabled: thumbnailPath.length > 0
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.openImageViewer(root.imageFileSource(thumbnailPath))
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Text {
                                    Layout.fillWidth: true
                                    text: fileName
                                    color: Theme.text
                                    font.pixelSize: 16
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: "#" + resultRank + " · " + assetTypeLabel
                                        + (extension.length > 0 ? " · " + extension : "")
                                        + " · " + projectName + " · " + sourceName
                                    color: Theme.muted
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: summary.length > 0 ? summary : "尚未生成内容摘要"
                                    color: Theme.text
                                    font.pixelSize: 13
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }

                                Text {
                                    Layout.fillWidth: true
                                    visible: searchReasons.length > 0
                                    text: "理解置信 " + Math.round(searchConfidence * 100) + "% · " + searchReasons
                                    color: Theme.blue
                                    font.pixelSize: 11
                                    maximumLineCount: 1
                                    elide: Text.ElideRight
                                }

                                Text {
                                    Layout.fillWidth: true
                                    visible: errorMessage.length > 0
                                    text: "失败原因：" + errorMessage
                                    color: Theme.orange
                                    font.pixelSize: 12
                                    maximumLineCount: 1
                                    elide: Text.ElideRight
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10

                                    Text {
                                        text: keywords.length > 0 ? keywords : "无关键词"
                                        color: Theme.muted
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: analysisStatusLabel
                                        color: Theme.text
                                        font.pixelSize: 12
                                    }

                                }
                            }
                        }

                        Rectangle {
                            id: assetRevealRing
                            anchors.fill: parent
                            anchors.margins: 5
                            radius: parent.radius - 4
                            color: "transparent"
                            border.width: 2
                            border.color: Theme.blue
                            visible: assetResultCard.quickSearchReveal
                            z: 20

                            SequentialAnimation on opacity {
                                running: assetRevealRing.visible
                                loops: Animation.Infinite
                                NumberAnimation { from: 1.0; to: 0.28; duration: 480; easing.type: Easing.InOutQuad }
                                NumberAnimation { from: 0.28; to: 1.0; duration: 480; easing.type: Easing.InOutQuad }
                            }
                        }

                        ThemedMenu {
                            id: assetContextMenu
                            property string targetVideoKey: ""

                            ThemedMenuItem {
                                text: "打开所在目录"
                                onTriggered: if (viewModel) {
                                    viewModel.openAssetFolder(assetContextMenu.targetVideoKey)
                                }
                            }
                            ThemedMenuItem {
                                text: "复制文件路径"
                                onTriggered: if (viewModel) {
                                    viewModel.copyAssetPath(assetContextMenu.targetVideoKey)
                                }
                            }
                        }

                        MouseArea {
                            id: assetContextArea
                            anchors.fill: parent
                            acceptedButtons: Qt.RightButton
                            onClicked: function(mouse) {
                                if (viewModel) {
                                    viewModel.selectVideo(videoKey)
                                }
                                assetContextMenu.targetVideoKey = videoKey
                                assetContextMenu.popup(assetContextArea, mouse.x, mouse.y)
                            }
                        }

                    }

                    Text {
                        anchors.centerIn: parent
                        visible: (!viewModel || !viewModel.frameSearchMode) && resultList.count === 0
                        text: "当前筛选条件下没有素材。"
                        color: Theme.muted
                        font.pixelSize: 14
                    }
                }
            }
            }

            Rectangle {
                Layout.preferredWidth: visible ? 420 : 0
                Layout.fillHeight: true
                visible: viewModel && !viewModel.frameSearchMode
                radius: 20
                color: Theme.panel2
                border.width: 1
                border.color: Theme.line

                ScrollView {
                    id: detailScroll

                    anchors.fill: parent
                    anchors.margins: 14
                    clip: true
                    contentWidth: availableWidth
                    contentHeight: detailColumn.implicitHeight
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    ScrollBar.vertical: ThemedScrollBar {
                        policy: ScrollBar.AsNeeded
                    }

                    MiddleDragScrollHandler {
                        parent: detailScroll.contentItem
                        flickable: detailScroll.contentItem
                    }

                    ColumnLayout {
                        id: detailColumn

                        width: detailScroll.availableWidth
                        spacing: 12

                        Text {
                            Layout.fillWidth: true
                            text: viewModel && viewModel.hasSelection ? viewModel.selectedTitle : "选择左侧素材查看详情"
                            color: Theme.text
                            font.pixelSize: 20
                            font.weight: Font.Black
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 210
                            radius: 18
                            color: Theme.mediaSurface
                            border.width: 1
                            border.color: Theme.line
                            clip: true

                            Image {
                                anchors.fill: parent
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: true
                                source: viewModel ? viewModel.selectedThumbnailUrl : ""
                            }

                            Text {
                                anchors.centerIn: parent
                                visible: !viewModel || !viewModel.hasSelection || viewModel.selectedThumbnailUrl.toString().length === 0
                                text: viewModel && viewModel.selectedThumbnailLoading
                                    ? "缩略图生成中..."
                                    : (viewModel && viewModel.selectedFramesLoading
                                        ? "正在加载预览..."
                                        : (viewModel && viewModel.selectedIsVideo ? "暂无多宫格拼图" : "暂无预览"))
                                color: Theme.muted
                            }

                            MouseArea {
                                anchors.fill: parent
                                enabled: viewModel && viewModel.hasSelection && viewModel.selectedThumbnailUrl.toString().length > 0
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.openImageViewer(viewModel.selectedThumbnailUrl.toString())
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: viewModel && viewModel.hasSelection
                            text: (viewModel ? viewModel.selectedProjectName : "") + " · " + (viewModel ? viewModel.selectedSourceName : "")
                            color: Theme.muted
                            font.pixelSize: 12
                            wrapMode: Text.WrapAnywhere
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: viewModel && viewModel.hasSelection
                            text: "类型：" + (viewModel ? viewModel.selectedAssetTypeLabel : "")
                                + ((viewModel && viewModel.selectedExtension.length > 0) ? " · " + viewModel.selectedExtension : "")
                            color: Theme.muted
                            font.pixelSize: 12
                            wrapMode: Text.WrapAnywhere
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            ActionButton {
                                Layout.preferredWidth: 122
                                Layout.preferredHeight: 34
                                text: viewModel ? viewModel.analyzeButtonText : "开始解析"
                                primary: true
                                enabled: viewModel && viewModel.canAnalyzeSelected
                                onClicked: if (viewModel) viewModel.analyzeSelected()
                            }

                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            ActionButton {
                                Layout.preferredWidth: 98
                                Layout.preferredHeight: 34
                                text: "打开所属项目"
                                enabled: viewModel && viewModel.hasSelection
                                onClicked: if (viewModel) viewModel.openSelectedProject()
                            }

                            ActionButton {
                                Layout.preferredWidth: 88
                                Layout.preferredHeight: 34
                                text: "定位文件夹"
                                enabled: viewModel && viewModel.hasSelection
                                onClicked: if (viewModel) viewModel.locateSelectedSource()
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            visible: viewModel && viewModel.hasSelection
                            radius: 16
                            color: Theme.bg
                            border.width: 1
                            border.color: Theme.line
                            implicitHeight: analysisError.visible ? 118 : 92

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Text {
                                        Layout.fillWidth: true
                                        text: viewModel ? viewModel.selectedAnalysisProgressText : ""
                                        color: Theme.text
                                        font.pixelSize: 13
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        text: (viewModel ? viewModel.selectedAnalysisProgress : 0) + "%"
                                        color: Theme.muted
                                        font.pixelSize: 12
                                    }
                                }

                                ThemedProgressBar {
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 100
                                    value: viewModel ? viewModel.selectedAnalysisProgress : 0
                                    indeterminate: viewModel && viewModel.selectedAnalysisBusy && viewModel.selectedAnalysisProgress <= 0
                                }

                                Text {
                                    id: analysisError
                                    Layout.fillWidth: true
                                    visible: viewModel && viewModel.selectedAnalysisError.length > 0
                                    text: "失败原因：" + (viewModel ? viewModel.selectedAnalysisError : "")
                                    color: Theme.orange
                                    font.pixelSize: 12
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        Rectangle {
                            id: summaryCard

                            Layout.fillWidth: true
                            radius: 16
                            color: Theme.bg
                            border.width: 1
                            border.color: Theme.line
                            clip: true
                            implicitHeight: Math.max(140, summaryColumn.implicitHeight + 28)

                            ColumnLayout {
                                id: summaryColumn

                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 10

                                Text {
                                    text: "内容摘要"
                                    color: Theme.text
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: viewModel && viewModel.selectedSummary.length > 0
                                        ? viewModel.selectedSummary
                                        : "当前还没有摘要。先执行解析任务，完成后这里会显示内容概述。"
                                    color: Theme.muted
                                    wrapMode: Text.Wrap
                                }

                                Text {
                                    Layout.fillWidth: true
                                    visible: viewModel && viewModel.selectedSourceTextPreview.length > 0
                                    text: "正文预览：" + (viewModel ? viewModel.selectedSourceTextPreview : "")
                                    color: Theme.muted
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 6
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        Rectangle {
                            id: keywordCard

                            Layout.fillWidth: true
                            radius: 16
                            color: Theme.bg
                            border.width: 1
                            border.color: Theme.line
                            clip: true
                            implicitHeight: Math.max(150, keywordColumn.implicitHeight + 28)

                            ColumnLayout {
                                id: keywordColumn

                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 10

                                Text {
                                    text: "关键词与状态"
                                    color: Theme.text
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                }

                                Flow {
                                    id: keywordFlow

                                    Layout.fillWidth: true
                                    spacing: 8

                                    Repeater {
                                        model: viewModel ? viewModel.selectedKeywords : []
                                        delegate: Rectangle {
                                            readonly property real maxChipWidth: keywordFlow.width > 0 ? Math.min(keywordFlow.width, 180) : 180

                                            radius: 12
                                            color: Qt.rgba(0.31, 0.55, 1.0, 0.18)
                                            border.width: 1
                                            border.color: Qt.rgba(0.31, 0.55, 1.0, 0.32)
                                            implicitHeight: 28
                                            implicitWidth: Math.min(maxChipWidth, chipText.implicitWidth + 18)
                                            clip: true

                                            Text {
                                                id: chipText
                                                anchors.centerIn: parent
                                                width: parent.width - 16
                                                text: modelData
                                                color: Theme.text
                                                font.pixelSize: 12
                                                elide: Text.ElideRight
                                                horizontalAlignment: Text.AlignHCenter
                                            }
                                        }
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: "解析状态：" + (viewModel ? viewModel.selectedAnalysisStatusLabel : "")
                                    color: Theme.muted
                                    wrapMode: Text.WrapAnywhere
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }

                            }
                        }

                        Rectangle {
                            id: dimensionCard

                            Layout.fillWidth: true
                            visible: viewModel
                                && (viewModel.selectedDimensionAnalyses.length > 0
                                    || viewModel.selectedDimensionAnalysisBusy
                                    || viewModel.selectedDimensionAnalysisError.length > 0)
                            radius: 16
                            color: Theme.bg
                            border.width: 1
                            border.color: Theme.line
                            clip: true
                            implicitHeight: Math.max(130, dimensionColumn.implicitHeight + 28)

                            ColumnLayout {
                                id: dimensionColumn

                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 10

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Text {
                                        Layout.fillWidth: true
                                        text: "自定义维度分析"
                                        color: Theme.text
                                        font.pixelSize: 15
                                        font.weight: Font.DemiBold
                                    }

                                    BusyIndicator {
                                        visible: viewModel && viewModel.selectedDimensionAnalysisBusy
                                        running: visible
                                        Layout.preferredWidth: 22
                                        Layout.preferredHeight: 22
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    visible: viewModel && viewModel.selectedDimensionAnalysisText.length > 0
                                    text: viewModel ? viewModel.selectedDimensionAnalysisText : ""
                                    color: Theme.muted
                                    wrapMode: Text.WrapAnywhere
                                }

                                Text {
                                    Layout.fillWidth: true
                                    visible: viewModel && viewModel.selectedDimensionAnalysisError.length > 0
                                    text: viewModel ? viewModel.selectedDimensionAnalysisError : ""
                                    color: Theme.orange
                                    wrapMode: Text.WrapAnywhere
                                }

                                Repeater {
                                    model: viewModel ? viewModel.selectedDimensionAnalyses : []

                                    delegate: ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 5

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: index === 0 ? 0 : 1
                                            visible: index > 0
                                            color: Theme.line
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.name
                                            color: Theme.text
                                            font.pixelSize: 13
                                            font.weight: Font.DemiBold
                                            wrapMode: Text.WrapAnywhere
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.detail
                                            color: Theme.muted
                                            wrapMode: Text.Wrap
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            id: pathCard

                            Layout.fillWidth: true
                            radius: 16
                            color: Theme.bg
                            border.width: 1
                            border.color: Theme.line
                            clip: true
                            implicitHeight: Math.max(120, pathColumn.implicitHeight + 28)

                            ColumnLayout {
                                id: pathColumn

                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 8

                                Text {
                                    text: "路径与素材信息"
                                    color: Theme.text
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: "源文件：" + (viewModel ? viewModel.selectedFilePath : "")
                                    color: Theme.muted
                                    wrapMode: Text.WrapAnywhere
                                }

                                Text {
                                    Layout.fillWidth: true
                                    visible: viewModel && viewModel.selectedTechnicalSummary.length > 0
                                    text: "技术摘要：" + (viewModel ? viewModel.selectedTechnicalSummary : "")
                                    color: Theme.muted
                                    wrapMode: Text.WrapAnywhere
                                }

                                Text {
                                    Layout.fillWidth: true
                                    visible: viewModel && viewModel.selectedIsVideo
                                    text: "解析图片目录：" + (viewModel ? viewModel.selectedCachePath : "")
                                    color: Theme.muted
                                    wrapMode: Text.WrapAnywhere
                                }
                            }
                        }

                        Text {
                            text: "抽帧解析"
                            visible: viewModel && viewModel.selectedIsVideo
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: viewModel && viewModel.selectedIsVideo && viewModel.selectedFrameSamplingText.length > 0
                            text: viewModel ? viewModel.selectedFrameSamplingText : ""
                            color: Theme.text
                            font.pixelSize: 12
                            wrapMode: Text.WrapAnywhere
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: viewModel && viewModel.selectedIsVideo && viewModel.selectedFrameSearchStatus.length > 0
                            text: viewModel ? viewModel.selectedFrameSearchStatus : ""
                            color: Theme.muted
                            font.pixelSize: 12
                            wrapMode: Text.WrapAnywhere
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: viewModel
                                && viewModel.selectedIsVideo
                                && !viewModel.selectedFramesLoading
                                && viewModel.selectedFrameCount > 0
                            spacing: 10

                            Text {
                                Layout.fillWidth: true
                                text: !viewModel ? ""
                                    : (viewModel.selectedRemainingFrameCount > 0
                                        ? "当前显示 " + viewModel.selectedVisibleFrameCount + " / " + viewModel.selectedFrameCount + " 帧，剩余 " + viewModel.selectedRemainingFrameCount + " 帧"
                                        : "当前显示 " + viewModel.selectedVisibleFrameCount + " 帧")
                                color: Theme.muted
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }

                            ActionButton {
                                Layout.preferredWidth: 106
                                Layout.preferredHeight: 30
                                visible: viewModel && viewModel.canLoadMoreSelectedFrames
                                text: "再加载一批"
                                onClicked: if (viewModel) viewModel.loadMoreSelectedFrames()
                            }

                        }

                        ListView {
                            id: detailFrameList

                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(contentHeight, 620)
                            Layout.minimumHeight: count > 0 ? Math.min(contentHeight, 132) : 0
                            clip: true
                            spacing: 8
                            reuseItems: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: viewModel && viewModel.selectedIsVideo ? viewModel.selectedFrames : []

                            delegate: Rectangle {
                                id: frameCard

                                width: ListView.view ? ListView.view.width : 0
                                radius: 14
                                color: Theme.bg
                                border.width: 1
                                border.color: Theme.line
                                clip: true
                                height: Math.max(132, frameTextColumn.implicitHeight + 20)

                                RowLayout {
                                    id: frameRow

                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 10

                                    Rectangle {
                                        Layout.preferredWidth: 120
                                        Layout.preferredHeight: 108
                                        radius: 12
                                        color: Theme.mediaSurface
                                        border.width: 1
                                        border.color: Theme.line
                                        clip: true

                                        Image {
                                            anchors.fill: parent
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                            cache: true
                                            source: modelData.imagePath.length > 0 ? "file:///" + modelData.imagePath.replace(/\\/g, "/") : ""
                                        }

                                        Text {
                                            anchors.centerIn: parent
                                            visible: modelData.imagePath.length === 0
                                            text: "暂无帧图"
                                            color: Theme.muted
                                            font.pixelSize: 11
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            enabled: modelData.imagePath.length > 0
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.openImageViewer(root.imageFileSource(modelData.imagePath))
                                        }
                                    }

                                    ColumnLayout {
                                        id: frameTextColumn

                                        Layout.fillWidth: true
                                        Layout.alignment: Qt.AlignTop
                                        spacing: 4

                                        Text {
                                            Layout.fillWidth: true
                                            text: "第 " + modelData.frameNumber + " 帧 · " + modelData.timestampLabel
                                            color: Theme.text
                                            font.pixelSize: 13
                                            font.weight: Font.DemiBold
                                            maximumLineCount: 1
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.caption.length > 0 ? modelData.caption : (modelData.errorMessage.length > 0 ? modelData.errorMessage : "暂无描述")
                                            color: Theme.muted
                                            wrapMode: Text.WrapAnywhere
                                            maximumLineCount: 3
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.matchText.length > 0 ? "命中：" + modelData.matchText : ""
                                            color: Theme.blue
                                            visible: text.length > 0
                                            wrapMode: Text.WrapAnywhere
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.tags.length > 0 ? "标签：" + modelData.tags : ""
                                            color: Theme.muted
                                            visible: text.length > 0
                                            wrapMode: Text.WrapAnywhere
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.objects.length > 0 ? "对象：" + modelData.objects : ""
                                            color: Theme.muted
                                            visible: text.length > 0
                                            wrapMode: Text.WrapAnywhere
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.actions.length > 0 ? "动作：" + modelData.actions : ""
                                            color: Theme.muted
                                            visible: text.length > 0
                                            wrapMode: Text.WrapAnywhere
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.setting.length > 0 ? "场景：" + modelData.setting : ""
                                            color: Theme.muted
                                            visible: text.length > 0
                                            wrapMode: Text.WrapAnywhere
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            visible: modelData.canRetry || modelData.retryLabel.length > 0
                                            spacing: 8

                                            Text {
                                                text: modelData.analysisState === 3 ? "状态：已跳过" : (modelData.analysisState === 2 ? "状态：失败" : "")
                                                visible: text.length > 0
                                                color: Theme.orange
                                                font.pixelSize: 11
                                            }

                                            Text {
                                                text: modelData.retryLabel
                                                visible: text.length > 0
                                                color: Theme.muted
                                                font.pixelSize: 11
                                            }

                                            Item {
                                                Layout.fillWidth: true
                                            }

                                            ActionButton {
                                                Layout.preferredWidth: 96
                                                Layout.preferredHeight: 30
                                                visible: modelData.canRetry
                                                text: "重解析该帧"
                                                onClicked: if (viewModel) viewModel.retrySelectedFrame(modelData.frameNumber)
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: viewModel
                                && viewModel.selectedIsVideo
                                && !viewModel.selectedFramesLoading
                                && viewModel.selectedFrames.length === 0
                            text: shellVm && shellVm.globalSearchText.length > 0
                                ? "当前关键词没有命中该素材的逐帧解析。"
                                : "当前还没有逐帧解析结果。"
                            color: Theme.muted
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: multiDimensionAnalyzeDialog

        modal: true
        width: Math.max(1, Math.min(620, root.width - 24))
        height: Math.max(1, Math.min(430, root.height - 24))
        x: Math.max(0, Math.round((root.width - width) / 2))
        y: Math.max(0, Math.round((root.height - height) / 2))
        padding: 0
        leftPadding: 0
        rightPadding: 0
        topPadding: 0
        bottomPadding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            radius: 18
            color: Theme.bg
            border.width: 1
            border.color: Theme.line
        }

        ScrollView {
            id: multiDimensionAnalyzeScroll

            anchors.fill: parent
            anchors.margins: Math.min(20, Math.max(4, multiDimensionAnalyzeDialog.width / 10))
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            MiddleDragScrollHandler {
                parent: multiDimensionAnalyzeScroll.contentItem
                flickable: multiDimensionAnalyzeScroll.contentItem
            }

            ColumnLayout {
                width: multiDimensionAnalyzeScroll.availableWidth
                spacing: 14

                Text {
                    Layout.fillWidth: true
                    text: "选择多维度解析方式"
                    color: Theme.text
                    font.pixelSize: 20
                    font.weight: Font.Black
                }

                Text {
                    Layout.fillWidth: true
                    text: viewModel && viewModel.hasAnalyzedVisible
                        ? "检测到当前结果中已有解析数据。常规解析已包含色彩、构图、品牌文字、适用场景和情绪氛围；设置中的自定义维度也会自动加入任务。建议优先补充缺失内容。"
                        : "当前结果尚无完整解析数据。补充解析会处理待解析和失败素材，并自动执行常规维度及设置中的自定义维度。"
                    color: Theme.muted
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: supplementOptionContent.implicitHeight + 28
                    radius: 12
                    color: Theme.panel2
                    border.width: 1
                    border.color: Theme.selectedLine

                    ColumnLayout {
                        id: supplementOptionContent

                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: "补充解析"
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.Bold
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "只补齐当前结构化版本缺失的颜色、材质、属性、品牌/文字等字段；完整帧直接跳过，待解析或失败素材继续处理。"
                            color: Theme.muted
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }

                        ActionButton {
                            Layout.alignment: Qt.AlignRight
                            Layout.preferredWidth: 148
                            Layout.preferredHeight: 38
                            text: "补充解析（推荐）"
                            primary: true
                            onClicked: {
                                multiDimensionAnalyzeDialog.close()
                                if (viewModel) {
                                    viewModel.analyzeVisibleSupplement()
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: rebuildOptionContent.implicitHeight + 28
                    radius: 12
                    color: Theme.panel2
                    border.width: 1
                    border.color: Theme.line

                    ColumnLayout {
                        id: rebuildOptionContent

                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: "全部重新解析"
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.Bold
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "从头读取素材、重新抽帧并调用模型。耗时和接口费用较高，适合源文件或解析规则整体变化时使用。"
                            color: Theme.muted
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }

                        ActionButton {
                            Layout.alignment: Qt.AlignRight
                            Layout.preferredWidth: 128
                            Layout.preferredHeight: 38
                            text: "全部重新解析"
                            danger: true
                            onClicked: {
                                multiDimensionAnalyzeDialog.close()
                                if (viewModel) {
                                    viewModel.analyzeVisibleAll()
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Item { Layout.fillWidth: true }

                    ActionButton {
                        Layout.preferredWidth: 74
                        Layout.preferredHeight: 36
                        text: "取消"
                        onClicked: multiDimensionAnalyzeDialog.close()
                    }
                }
            }
        }
    }

    Rectangle {
        id: imageViewerOverlay
        anchors.fill: parent
        visible: root.imageViewerSource.length > 0
        z: 1000
        color: Qt.rgba(0, 0, 0, 0.92)
        focus: visible

        property real lastMouseX: 0
        property real lastMouseY: 0

        onVisibleChanged: if (visible) forceActiveFocus()
        Keys.onEscapePressed: root.closeImageViewer()

        Item {
            id: imageViewerViewport
            anchors.fill: parent
            anchors.margins: 24
            clip: true

            readonly property real fitScale: imageViewerImage.status === Image.Ready
                && imageViewerImage.implicitWidth > 0
                && imageViewerImage.implicitHeight > 0
                    ? Math.min(width / imageViewerImage.implicitWidth,
                               height / imageViewerImage.implicitHeight,
                               1.0)
                    : 1.0

            Image {
                id: imageViewerImage
                source: root.imageViewerSource
                asynchronous: true
                cache: false
                smooth: true
                width: implicitWidth * imageViewerViewport.fitScale * root.imageViewerScale
                height: implicitHeight * imageViewerViewport.fitScale * root.imageViewerScale
                x: (imageViewerViewport.width - width) / 2 + root.imageViewerOffsetX
                y: (imageViewerViewport.height - height) / 2 + root.imageViewerOffsetY
                fillMode: Image.Stretch
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                onPressed: function(mouse) {
                    imageViewerOverlay.forceActiveFocus()
                    imageViewerOverlay.lastMouseX = mouse.x
                    imageViewerOverlay.lastMouseY = mouse.y
                }
                onPositionChanged: function(mouse) {
                    if (!pressed) {
                        return
                    }
                    root.imageViewerOffsetX += mouse.x - imageViewerOverlay.lastMouseX
                    root.imageViewerOffsetY += mouse.y - imageViewerOverlay.lastMouseY
                    imageViewerOverlay.lastMouseX = mouse.x
                    imageViewerOverlay.lastMouseY = mouse.y
                }
                onDoubleClicked: {
                    root.imageViewerScale = 1.0
                    root.imageViewerOffsetX = 0
                    root.imageViewerOffsetY = 0
                }
                onWheel: function(wheel) {
                    imageViewerOverlay.forceActiveFocus()
                    var nextScale = root.imageViewerScale * (wheel.angleDelta.y > 0 ? 1.15 : 0.87)
                    root.imageViewerScale = Math.max(0.2, Math.min(12.0, nextScale))
                    wheel.accepted = true
                }
            }
        }

        Rectangle {
            width: 40
            height: 40
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 18
            radius: 20
            color: closeArea.pressed ? "#334155" : "#1F2937"
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.18)

            Text {
                anchors.centerIn: parent
                text: "X"
                color: "white"
                font.pixelSize: 18
                font.weight: Font.Bold
            }

            MouseArea {
                id: closeArea
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.closeImageViewer()
            }
        }
    }
}
