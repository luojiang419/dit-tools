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
    property string dimensionDialogMessage: ""

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
            if (Number(options[index].value) === Number(value)) {
                return index
            }
        }
        return 0
    }

    function resetDimensionDrafts() {
        dimensionDraftModel.clear()
        var defaults = ["色彩风格", "构图与镜头语言", "品牌/文字线索", "适用场景", "情绪氛围"]
        for (var index = 0; index < defaults.length; ++index) {
            dimensionDraftModel.append({ name: defaults[index], selected: true, custom: false })
        }
        dimensionDialogMessage = ""
        if (dimensionNameField) {
            dimensionNameField.text = ""
        }
    }

    function addDimensionDraft() {
        var name = dimensionNameField.text.trim()
        if (name.length === 0) {
            dimensionDialogMessage = "请输入维度名称。"
            return
        }
        for (var index = 0; index < dimensionDraftModel.count; ++index) {
            if (String(dimensionDraftModel.get(index).name).toLowerCase() === name.toLowerCase()) {
                dimensionDraftModel.setProperty(index, "selected", true)
                dimensionDialogMessage = "该维度已在面板中。"
                dimensionNameField.text = ""
                return
            }
        }
        dimensionDraftModel.append({ name: name, selected: true, custom: true })
        dimensionNameField.text = ""
        dimensionDialogMessage = ""
    }

    function selectedDimensionDrafts() {
        var values = []
        for (var index = 0; index < dimensionDraftModel.count; ++index) {
            var item = dimensionDraftModel.get(index)
            if (item.selected && String(item.name).trim().length > 0) {
                values.push(String(item.name).trim())
            }
        }
        return values
    }

    function hasSelectedDimensionDrafts() {
        return selectedDimensionDrafts().length > 0
    }

    ListModel {
        id: dimensionDraftModel
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

    Component.onCompleted: if (viewModel) viewModel.reload()

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
                        Layout.preferredWidth: 96
                        Layout.preferredHeight: 36
                        text: "批量解析"
                        enabled: viewModel
                        onClicked: {
                            if (viewModel) {
                                if (viewModel.hasAnalyzedVisible) {
                                    batchAnalyzeDialog.open()
                                } else {
                                    viewModel.analyzeVisiblePending()
                                }
                            }
                        }
                    }

                    ActionButton {
                        Layout.preferredWidth: 128
                        Layout.preferredHeight: 36
                        text: "全局多维度解析"
                        enabled: viewModel && viewModel.canAnalyzeVisibleDimensions
                        onClicked: {
                            root.resetDimensionDrafts()
                            dimensionAnalyzeDialog.open()
                        }
                    }

                    ActionButton {
                        Layout.preferredWidth: 100
                        Layout.preferredHeight: 36
                        text: "重建全局索引"
                        primary: true
                        onClicked: if (viewModel) viewModel.rebuildGlobalIndex()
                    }

                    ActionButton {
                        Layout.preferredWidth: 88
                        Layout.preferredHeight: 36
                        text: "全部确认"
                        enabled: viewModel && viewModel.canConfirmVisible
                        onClicked: if (viewModel) viewModel.confirmVisible()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    ThemedTextField {
                        Layout.preferredWidth: 240
                        Layout.fillWidth: true
                        placeholderText: "搜索文件名、路径、摘要、关键词或文本内容"
                        text: shellVm ? shellVm.globalSearchText : ""
                        onTextChanged: if (shellVm) shellVm.globalSearchText = text
                    }

                    ThemedComboBox {
                        Layout.preferredWidth: 160
                        model: viewModel ? viewModel.projectOptions : []
                        textRole: "label"
                        onActivated: if (viewModel) viewModel.setProjectFilter(model[index].value)
                    }

                    ThemedComboBox {
                        Layout.preferredWidth: 160
                        model: viewModel ? viewModel.sourceOptions : []
                        textRole: "label"
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
                        onActivated: if (viewModel) viewModel.setAnalysisStatusFilter(model[index].value)
                    }

                    ThemedComboBox {
                        Layout.preferredWidth: 140
                        model: viewModel ? viewModel.confirmationStatusOptions : []
                        textRole: "label"
                        onActivated: if (viewModel) viewModel.setConfirmationStatusFilter(model[index].value)
                    }
                }
            }
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

                ListView {
                    id: resultList
                    anchors.fill: parent
                    anchors.margins: 12
                    clip: true
                    spacing: 10
                    reuseItems: true
                    cacheBuffer: 720
                    model: viewModel ? viewModel.model : null
                    currentIndex: viewModel ? viewModel.selectedVideoIndex : -1

                    ScrollBar.vertical: ThemedScrollBar {
                        policy: ScrollBar.AsNeeded
                    }

                    onCurrentIndexChanged: if (currentIndex >= 0) {
                        positionViewAtIndex(currentIndex, ListView.Contain)
                    }

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: errorMessage.length > 0 ? 138 : 118
                        radius: 18
                        color: viewModel && viewModel.selectedAssetKey === assetKey ? Theme.selectedBg : Theme.bg
                        border.width: 1
                        border.color: viewModel && viewModel.selectedAssetKey === assetKey ? Theme.blue : Theme.line

                        MouseArea {
                            anchors.fill: parent
                            anchors.rightMargin: 112
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
                                    text: assetTypeLabel + (extension.length > 0 ? " · " + extension : "") + " · " + projectName + " · " + sourceName
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

                                    Text {
                                        text: confirmationStatusLabel
                                        color: Theme.muted
                                        font.pixelSize: 12
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.preferredWidth: 88
                                Layout.fillHeight: true
                                spacing: 6

                                Item { Layout.fillHeight: true }

                                ActionButton {
                                    Layout.preferredWidth: 82
                                    Layout.preferredHeight: 32
                                    visible: isConfirmed || canConfirm
                                    text: isConfirmed ? "已确认" : "确认"
                                    primary: canConfirm
                                    enabled: viewModel && canConfirm
                                    onClicked: if (viewModel) viewModel.confirmVideo(assetKey)
                                }

                                Item { Layout.fillHeight: true }
                            }
                        }

                    }

                    Text {
                        anchors.centerIn: parent
                        visible: resultList.count === 0
                        text: "当前筛选条件下没有素材。"
                        color: Theme.muted
                        font.pixelSize: 14
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 420
                Layout.fillHeight: true
                radius: 20
                color: Theme.panel2
                border.width: 1
                border.color: Theme.line

                ScrollView {
                    id: detailScroll

                    anchors.fill: parent
                    anchors.margins: 14
                    clip: true

                    ColumnLayout {
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

                            ActionButton {
                                Layout.preferredWidth: 98
                                Layout.preferredHeight: 34
                                text: viewModel && viewModel.selectedConfirmationStatusLabel === "已确认" ? "已确认" : "确认结果"
                                enabled: viewModel && viewModel.canConfirmSelected
                                onClicked: if (viewModel) viewModel.confirmSelected()
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

                                Text {
                                    Layout.fillWidth: true
                                    text: "确认状态：" + (viewModel ? viewModel.selectedConfirmationStatusLabel : "")
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
                                        text: "多维度分析"
                                        color: Theme.text
                                        font.pixelSize: 15
                                        font.weight: Font.DemiBold
                                    }

                                    BusyIndicator {
                                        visible: viewModel && viewModel.selectedDimensionAnalysisBusy
                                        running: visible
                                        width: 22
                                        height: 22
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
                            text: "逐帧解析"
                            visible: viewModel && viewModel.selectedIsVideo
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
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

                            ActionButton {
                                Layout.preferredWidth: 96
                                Layout.preferredHeight: 30
                                visible: viewModel && viewModel.canLoadMoreSelectedFrames
                                text: "全部展开"
                                onClicked: if (viewModel) viewModel.showAllSelectedFrames()
                            }

                            ActionButton {
                                Layout.preferredWidth: 84
                                Layout.preferredHeight: 30
                                visible: viewModel && viewModel.canExpandSelectedFrames && viewModel.selectedFramesExpanded
                                text: "收起"
                                onClicked: if (viewModel) viewModel.collapseSelectedFrames()
                            }
                        }

                        Repeater {
                            model: viewModel && viewModel.selectedIsVideo ? viewModel.selectedFrames : []

                            delegate: Rectangle {
                                id: frameCard

                                Layout.fillWidth: true
                                radius: 14
                                color: Theme.bg
                                border.width: 1
                                border.color: Theme.line
                                clip: true
                                implicitHeight: Math.max(132, frameTextColumn.implicitHeight + 20)

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
        id: batchAnalyzeDialog

        modal: true
        width: 520
        height: 220
        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)
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

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14

            Text {
                Layout.fillWidth: true
                text: "当前结果中已有解析完成素材"
                color: Theme.text
                font.pixelSize: 20
                font.weight: Font.Black
            }

            Text {
                Layout.fillWidth: true
                text: "请选择只解析待解析和失败素材，或将当前搜索/筛选结果全部重新解析。"
                color: Theme.muted
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Item { Layout.fillWidth: true }

                ActionButton {
                    Layout.preferredWidth: 74
                    Layout.preferredHeight: 36
                    text: "取消"
                    onClicked: batchAnalyzeDialog.close()
                }

                ActionButton {
                    Layout.preferredWidth: 132
                    Layout.preferredHeight: 36
                    text: "解析未完成素材"
                    primary: true
                    onClicked: {
                        batchAnalyzeDialog.close()
                        if (viewModel) {
                            viewModel.analyzeVisiblePending()
                        }
                    }
                }

                ActionButton {
                    Layout.preferredWidth: 118
                    Layout.preferredHeight: 36
                    text: "全部重新解析"
                    onClicked: {
                        batchAnalyzeDialog.close()
                        if (viewModel) {
                            viewModel.analyzeVisibleAll()
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: dimensionAnalyzeDialog

        modal: true
        width: Math.max(280, Math.min(560, root.width - 48))
        height: Math.max(360, Math.min(560, root.height - 48))
        x: Math.max(12, Math.round((root.width - width) / 2))
        y: Math.max(12, Math.round((root.height - height) / 2))
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

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14

            Text {
                Layout.fillWidth: true
                text: "全局多维度解析"
                color: Theme.text
                font.pixelSize: 20
                font.weight: Font.Black
            }

            Text {
                Layout.fillWidth: true
                text: viewModel ? viewModel.statusText : ""
                color: Theme.muted
                font.pixelSize: 13
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 14
                color: Theme.panel2
                border.width: 1
                border.color: Theme.line
                clip: true

                ScrollView {
                    id: dimensionDraftScroll

                    anchors.fill: parent
                    anchors.margins: 10
                    clip: true

                    ColumnLayout {
                        width: dimensionDraftScroll.availableWidth
                        spacing: 6

                        Repeater {
                            model: dimensionDraftModel

                            delegate: RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                CheckBox {
                                    checked: selected
                                    onToggled: dimensionDraftModel.setProperty(index, "selected", checked)
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: name
                                    color: Theme.text
                                    font.pixelSize: 14
                                    elide: Text.ElideRight
                                }

                                ActionButton {
                                    Layout.preferredWidth: 58
                                    Layout.preferredHeight: 28
                                    visible: custom
                                    text: "删除"
                                    textPixelSize: 12
                                    onClicked: dimensionDraftModel.remove(index)
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                ThemedTextField {
                    id: dimensionNameField

                    Layout.fillWidth: true
                    placeholderText: "添加自定义维度"
                    onAccepted: root.addDimensionDraft()
                }

                ActionButton {
                    Layout.preferredWidth: 72
                    Layout.preferredHeight: 34
                    text: "添加"
                    onClicked: root.addDimensionDraft()
                }
            }

            Text {
                Layout.fillWidth: true
                visible: root.dimensionDialogMessage.length > 0
                text: root.dimensionDialogMessage
                color: Theme.orange
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Item { Layout.fillWidth: true }

                ActionButton {
                    Layout.preferredWidth: 74
                    Layout.preferredHeight: 36
                    text: "取消"
                    onClicked: dimensionAnalyzeDialog.close()
                }

                ActionButton {
                    Layout.preferredWidth: 104
                    Layout.preferredHeight: 36
                    text: "开始解析"
                    primary: true
                    enabled: viewModel && viewModel.canAnalyzeVisibleDimensions && root.hasSelectedDimensionDrafts()
                    onClicked: {
                        var dimensions = root.selectedDimensionDrafts()
                        if (dimensions.length === 0) {
                            root.dimensionDialogMessage = "请选择至少一个维度。"
                            return
                        }
                        dimensionAnalyzeDialog.close()
                        viewModel.analyzeVisibleDimensions(dimensions)
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
