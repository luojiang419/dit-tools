import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CineVault

Rectangle {
    id: libraryRoot

    property var viewModel
    property var shellVm
    property var documentPreviewVm
    readonly property bool hasOpenProject: !shellVm || shellVm.projectPath.length > 0
    readonly property real minThumbnailScale: 0.5
    readonly property real maxThumbnailScale: 1.45
    property real thumbnailScale: 1.0
    property bool thumbnailSliderOpen: false
    readonly property int gridPreviewHeight: Math.round(146 * thumbnailScale)
    readonly property int gridCardWidth: Math.round(224 * thumbnailScale)
    readonly property int gridCardHeight: Math.round(270 + gridPreviewHeight - 146)
    readonly property int gridCellWidth: gridCardWidth + 16
    readonly property int gridCellHeight: gridCardHeight + 12

    color: Theme.bg
    focus: true

    function gridColumnCount() {
        return Math.max(1, Math.floor(Math.max(1, libraryRoot.width - 40) / libraryRoot.gridCellWidth))
    }

    function adjustThumbnailScale(delta) {
        var nextScale = Math.max(minThumbnailScale, Math.min(maxThumbnailScale, thumbnailScale + delta))
        thumbnailScale = Math.round(nextScale * 100) / 100
    }

    function clampRange(value, minValue, maxValue) {
        return Math.max(minValue, Math.min(maxValue, value))
    }

    function dragScrollX(flickable, delta) {
        if (!flickable) {
            return
        }
        var maxX = Math.max(0, flickable.contentWidth - flickable.width)
        flickable.contentX = clampRange(flickable.contentX - delta, 0, maxX)
    }

    function dragScrollY(flickable, delta) {
        if (!flickable) {
            return
        }
        var maxY = Math.max(0, flickable.contentHeight - flickable.height)
        flickable.contentY = clampRange(flickable.contentY - delta, 0, maxY)
    }

    function requestMoreIfNearEnd(flickable, margin) {
        if (!viewModel || viewModel.loading || !viewModel.hasMore || !flickable) {
            return
        }
        if (flickable.contentY + flickable.height >= flickable.contentHeight - margin) {
            viewModel.loadMore()
        }
    }

    function openSelectedAssetFullscreen() {
        if (!viewModel) {
            return
        }

        if (viewModel.selectedPreviewIsVideo) {
            fullscreenPlaybackBridge.openFullscreen()
        } else if (viewModel.selectedPreviewIsImage) {
            assetPreviewOverlay.openImage(viewModel.selectedAssetUrl, viewModel.selectedPreviewTitle)
        } else if (viewModel.selectedPreviewIsDocument) {
            assetPreviewOverlay.openDocument(viewModel.selectedAssetUrl, viewModel.selectedPreviewTitle)
        }
    }

    function stepSelectedPreviewFrame(direction) {
        if (!viewModel || !viewModel.selectedPreviewIsVideo || !fullscreenPlaybackBridge.frameNavigationActive) {
            return false
        }

        return fullscreenPlaybackBridge.stepFrameFromKeyboard(direction)
    }

    Keys.onPressed: function(event) {
        if (!viewModel || !libraryRoot.hasOpenProject || assetPreviewOverlay.visible) {
            return
        }

        if (event.key === Qt.Key_Left) {
            if (libraryRoot.stepSelectedPreviewFrame(-1)) {
                event.accepted = true
                return
            }
            viewModel.moveAssetSelection(-1)
            event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            if (libraryRoot.stepSelectedPreviewFrame(1)) {
                event.accepted = true
                return
            }
            viewModel.moveAssetSelection(1)
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            viewModel.moveAssetSelection(viewModel.viewMode === 0 ? -libraryRoot.gridColumnCount() : -1)
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            viewModel.moveAssetSelection(viewModel.viewMode === 0 ? libraryRoot.gridColumnCount() : 1)
            event.accepted = true
        } else if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
            libraryRoot.openSelectedAssetFullscreen()
            event.accepted = true
        }
    }

    VideoPreviewPlayer {
        id: fullscreenPlaybackBridge

        width: 1
        height: 1
        visible: false
        sourceUrl: viewModel ? viewModel.selectedPreviewUrl : ""
        thumbnailUrl: viewModel ? viewModel.selectedPreviewThumbnailUrl : ""
        title: viewModel ? viewModel.selectedPreviewTitle : ""
        isVideo: viewModel ? viewModel.selectedPreviewIsVideo : false
    }

    AssetPreviewOverlay {
        id: assetPreviewOverlay
        previewVm: libraryRoot.documentPreviewVm
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14
        visible: libraryRoot.hasOpenProject

        RowLayout {
            Layout.fillWidth: true

            ColumnLayout {
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    text: "素材库"
                    color: Theme.text
                    font.pixelSize: 28
                    font.weight: Font.Black
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: viewModel.statusText
                    color: Theme.muted
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
            }

            Item { Layout.fillWidth: true }

            ActionButton {
                Layout.preferredWidth: 124
                Layout.preferredHeight: 36
                text: viewModel ? viewModel.sortOrderText : "修改时间倒序"
                primary: viewModel && viewModel.modifiedTimeAscending
                onClicked: if (viewModel) viewModel.toggleModifiedTimeOrder()
            }

            CheckBox {
                id: favoritesOnlyCheck

                Layout.preferredWidth: 92
                Layout.preferredHeight: 36
                text: "仅收藏"
                checked: viewModel.favoritesOnly
                onToggled: viewModel.favoritesOnly = checked
                spacing: 8

                indicator: Rectangle {
                    implicitWidth: 18
                    implicitHeight: 18
                    x: 4
                    y: parent.height / 2 - height / 2
                    radius: 5
                    color: favoritesOnlyCheck.checked ? Theme.blue : Theme.panel2
                    border.width: 1
                    border.color: favoritesOnlyCheck.checked ? Qt.rgba(0.65, 0.78, 1.0, 0.55) : Theme.line

                    Text {
                        anchors.centerIn: parent
                        text: favoritesOnlyCheck.checked ? "★" : ""
                        color: Theme.text
                        font.pixelSize: 12
                    }
                }

                contentItem: Text {
                    text: favoritesOnlyCheck.text
                    color: favoritesOnlyCheck.checked ? Theme.text : Theme.muted
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: favoritesOnlyCheck.indicator.width + favoritesOnlyCheck.spacing + 6
                    elide: Text.ElideRight
                }
            }

            ActionButton {
                Layout.preferredWidth: 88
                Layout.preferredHeight: 36
                text: "大图卡片"
                primary: viewModel.viewMode === 0
                onClicked: viewModel.viewMode = 0
            }

            ActionButton {
                Layout.preferredWidth: 88
                Layout.preferredHeight: 36
                text: "技术表格"
                primary: viewModel.viewMode === 1
                onClicked: viewModel.viewMode = 1
            }
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: viewModel.viewMode === 0 ? gridComponent : tableComponent
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 520)
        spacing: 12
        visible: !libraryRoot.hasOpenProject

        Text {
            Layout.fillWidth: true
            text: "请先选择项目"
            color: Theme.text
            font.pixelSize: 24
            font.weight: Font.Black
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }

        Text {
            Layout.fillWidth: true
            text: "在项目库中新建或打开项目后，再进入对应素材库管理素材。"
            color: Theme.muted
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
    }

    Component {
        id: gridComponent

        GridView {
            id: assetGrid

            clip: true
            cellWidth: libraryRoot.gridCellWidth
            cellHeight: libraryRoot.gridCellHeight
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true
            cacheBuffer: libraryRoot.gridCellHeight * 6
            model: viewModel.model
            currentIndex: viewModel ? viewModel.selectedAssetIndex : -1
            property real dragLastX: 0
            property real dragLastY: 0

            onCurrentIndexChanged: if (currentIndex >= 0) {
                positionViewAtIndex(currentIndex, GridView.Contain)
            }
            // qmllint disable unqualified
            onContentYChanged: libraryRoot.requestMoreIfNearEnd(this, libraryRoot.gridCellHeight * 3)
            onContentHeightChanged: libraryRoot.requestMoreIfNearEnd(this, libraryRoot.gridCellHeight * 3)
            // qmllint enable unqualified

            ScrollBar.vertical: ThemedScrollBar {
                policy: ScrollBar.AsNeeded
            }

            WheelHandler {
                acceptedModifiers: Qt.ControlModifier
                target: null
                onWheel: function(event) {
                    libraryRoot.adjustThumbnailScale(event.angleDelta.y > 0 ? 0.08 : -0.08)
                    event.accepted = true
                }
            }

            MiddleDragScrollHandler {
                flickable: assetGrid
            }

            DragHandler {
                target: null
                acceptedDevices: PointerDevice.Mouse
                acceptedButtons: Qt.LeftButton
                grabPermissions: PointerHandler.CanTakeOverFromAnything
                onActiveChanged: {
                    assetGrid.dragLastX = 0
                    assetGrid.dragLastY = 0
                }
                onTranslationChanged: {
                    var deltaX = translation.x - assetGrid.dragLastX
                    var deltaY = translation.y - assetGrid.dragLastY
                    assetGrid.dragLastX = translation.x
                    assetGrid.dragLastY = translation.y
                    libraryRoot.dragScrollX(assetGrid, deltaX)
                    libraryRoot.dragScrollY(assetGrid, deltaY)
                }
            }

            delegate: Item {
                width: libraryRoot.gridCardWidth
                height: libraryRoot.gridCardHeight

                AssetCard {
                    anchors.fill: parent
                    title: model.name
                    subtitle: model.relativePath
                    meta: model.technicalSummary.length > 0 ? model.technicalSummary + " · " + model.sizeLabel : model.sizeLabel + " · " + model.modifiedAt
                    tag: model.typeLabel
                    thumbnailPath: model.thumbnailPath
                    thumbnailLoading: model.thumbnailLoading
                    favorite: model.favorite
                    selected: viewModel.selectedAssetId === assetId
                    previewHeight: libraryRoot.gridPreviewHeight
                }

                ThemedMenu {
                    id: cardContextMenu

                    ThemedMenuItem {
                        text: "打开所在目录"
                        onTriggered: viewModel.openAssetFolder(assetId)
                    }
                    ThemedMenuItem {
                        text: "复制文件路径"
                        onTriggered: viewModel.copyAssetPath(assetId)
                    }
                    ThemedMenuSeparator {}
                    ThemedMenuItem {
                        text: favorite ? "取消收藏" : "收藏"
                        onTriggered: viewModel.toggleAssetFavorite(assetId)
                    }
                    ThemedMenuSeparator {}
                    ThemedMenuItem {
                        text: "移除文件（不删除）"
                        onTriggered: viewModel.removeAsset(assetId)
                    }
                }

                MouseArea {
                    id: cardMouseArea
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: function(mouse) {
                        libraryRoot.forceActiveFocus()
                        viewModel.selectAsset(assetId)
                        if (mouse.button === Qt.RightButton) {
                            cardContextMenu.popup(cardMouseArea, mouse.x, mouse.y)
                        }
                    }
                }
            }
        }
    }

    Component {
        id: tableComponent

        Flickable {
            id: tableFlick
            clip: true
            contentWidth: Math.max(width, 1160)
            contentHeight: height
            boundsBehavior: Flickable.StopAtBounds
            property real dragLastX: 0
            property real dragLastY: 0
            ScrollBar.horizontal: ThemedScrollBar { policy: ScrollBar.AsNeeded }

            MiddleDragScrollHandler {
                flickable: tableFlick
                horizontalFlickable: tableFlick
                verticalFlickable: tableList
            }

            DragHandler {
                target: null
                acceptedDevices: PointerDevice.Mouse
                acceptedButtons: Qt.LeftButton
                grabPermissions: PointerHandler.CanTakeOverFromAnything
                onActiveChanged: {
                    tableFlick.dragLastX = 0
                    tableFlick.dragLastY = 0
                }
                onTranslationChanged: {
                    var deltaX = translation.x - tableFlick.dragLastX
                    var deltaY = translation.y - tableFlick.dragLastY
                    tableFlick.dragLastX = translation.x
                    tableFlick.dragLastY = translation.y
                    libraryRoot.dragScrollX(tableFlick, deltaX)
                    libraryRoot.dragScrollY(tableList, deltaY)
                }
            }

            ListView {
                id: tableList
                width: tableFlick.contentWidth
                height: tableFlick.height
                clip: true
                spacing: 10
                boundsBehavior: Flickable.StopAtBounds
                reuseItems: true
                cacheBuffer: 720
                model: viewModel.model
                currentIndex: viewModel ? viewModel.selectedAssetIndex : -1

                onCurrentIndexChanged: if (currentIndex >= 0) {
                    positionViewAtIndex(currentIndex, ListView.Contain)
                }
                // qmllint disable unqualified
                onContentYChanged: libraryRoot.requestMoreIfNearEnd(this, 58 * 12)
                onContentHeightChanged: libraryRoot.requestMoreIfNearEnd(this, 58 * 12)
                // qmllint enable unqualified

                ScrollBar.vertical: ThemedScrollBar {
                    parent: tableFlick.parent
                    anchors.top: tableFlick.top
                    anchors.right: tableFlick.right
                    anchors.bottom: tableFlick.bottom
                    policy: ScrollBar.AsNeeded
                }

                header: Rectangle {
                    width: tableList.width
                    height: 46
                    color: Theme.panel2
                    border.width: 1
                    border.color: Theme.line
                    radius: 16

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12
                        Text { text: "文件名"; color: Theme.muted; Layout.preferredWidth: 260; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                        Text { text: "类型"; color: Theme.muted; Layout.preferredWidth: 96; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                        Text { text: "大小"; color: Theme.muted; Layout.preferredWidth: 104; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                        Text { text: viewModel && viewModel.modifiedTimeAscending ? "修改时间 ↑" : "修改时间 ↓"; color: Theme.muted; Layout.preferredWidth: 190; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                        Text { text: "技术信息"; color: Theme.muted; Layout.preferredWidth: 180; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                        Text { text: "相对路径"; color: Theme.muted; Layout.fillWidth: true; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                    }
                }

                delegate: Rectangle {
                    width: tableList.width
                    height: 58
                    radius: 14
                    color: viewModel.selectedAssetId === assetId ? Theme.selectedBg : Theme.panel2
                    border.width: 1
                    border.color: viewModel.selectedAssetId === assetId ? Theme.blue : Theme.line

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12
                        Text { text: favorite ? "★ " + model.name : model.name; color: Theme.text; Layout.preferredWidth: 260; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                        Text { text: model.typeLabel; color: Theme.muted; Layout.preferredWidth: 96; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                        Text { text: model.sizeLabel; color: Theme.muted; Layout.preferredWidth: 104; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                        Text { text: model.modifiedAt; color: Theme.muted; Layout.preferredWidth: 190; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                        Text { text: model.technicalSummary.length > 0 ? model.technicalSummary : model.probeStatusLabel; color: Theme.muted; Layout.preferredWidth: 180; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                        Text { text: model.relativePath; color: Theme.muted; Layout.fillWidth: true; elide: Text.ElideMiddle; verticalAlignment: Text.AlignVCenter }
                    }

                    ThemedMenu {
                        id: rowContextMenu

                        ThemedMenuItem {
                            text: "打开所在目录"
                            onTriggered: viewModel.openAssetFolder(assetId)
                        }
                        ThemedMenuItem {
                            text: "复制文件路径"
                            onTriggered: viewModel.copyAssetPath(assetId)
                        }
                        ThemedMenuSeparator {}
                        ThemedMenuItem {
                            text: favorite ? "取消收藏" : "收藏"
                            onTriggered: viewModel.toggleAssetFavorite(assetId)
                        }
                        ThemedMenuSeparator {}
                        ThemedMenuItem {
                            text: "移除文件（不删除）"
                            onTriggered: viewModel.removeAsset(assetId)
                        }
                    }

                    MouseArea {
                        id: rowMouseArea
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: function(mouse) {
                            libraryRoot.forceActiveFocus()
                            viewModel.selectAsset(assetId)
                            if (mouse.button === Qt.RightButton) {
                                rowContextMenu.popup(rowMouseArea, mouse.x, mouse.y)
                            }
                        }
                    }
                }
            }
        }
    }

    Item {
        id: thumbnailSizeControl

        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        width: thumbnailSliderPanel.visible ? 230 : 44
        height: thumbnailSliderPanel.visible ? 106 : 44
        z: 50
        visible: libraryRoot.hasOpenProject && viewModel && viewModel.viewMode === 0

        Rectangle {
            id: thumbnailSliderPanel

            width: 220
            height: 56
            anchors.right: thumbnailSizeButton.right
            anchors.bottom: thumbnailSizeButton.top
            anchors.bottomMargin: 8
            visible: libraryRoot.thumbnailSliderOpen
            radius: 14
            color: Qt.rgba(0.05, 0.07, 0.10, Theme.isDark ? 0.82 : 0.74)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, Theme.isDark ? 0.16 : 0.28)

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                Text {
                    Layout.preferredWidth: 48
                    text: "缩略图"
                    color: "white"
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                ThemedSlider {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    from: libraryRoot.minThumbnailScale
                    to: libraryRoot.maxThumbnailScale
                    value: libraryRoot.thumbnailScale
                    onMoved: libraryRoot.thumbnailScale = Math.round(value * 100) / 100
                }
            }
        }

        Rectangle {
            id: thumbnailSizeButton

            width: 44
            height: 44
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            radius: 22
            opacity: thumbnailSizeMouse.containsMouse || libraryRoot.thumbnailSliderOpen ? 0.96 : 0.42
            color: Qt.rgba(0.05, 0.07, 0.10, Theme.isDark ? 0.78 : 0.62)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, Theme.isDark ? 0.18 : 0.32)

            Grid {
                anchors.centerIn: parent
                columns: 2
                rows: 2
                spacing: 3

                Repeater {
                    model: 4

                    Rectangle {
                        width: 8
                        height: 8
                        radius: 2
                        color: "white"
                        opacity: 0.9
                    }
                }
            }

            MouseArea {
                id: thumbnailSizeMouse

                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    libraryRoot.thumbnailSliderOpen = !libraryRoot.thumbnailSliderOpen
                    libraryRoot.forceActiveFocus()
                }
            }
        }
    }
}
