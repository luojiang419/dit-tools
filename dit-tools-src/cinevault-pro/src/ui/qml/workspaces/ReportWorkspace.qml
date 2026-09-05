import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CineVault

Rectangle {
    id: root

    property var viewModel
    property var lastObservedSelectedSourceId: 0

    color: Theme.bg

    function triggerPreview() {
        if (!viewModel || !viewModel.canPreview) {
            return
        }
        viewModel.refreshPreview(timeField.text, locationField.text, directorField.text, cinematographerField.text, ditField.text)
    }

    function scheduleAutoPreview() {
        if (!viewModel || !viewModel.canPreview) {
            return
        }
        previewRefreshTimer.restart()
    }

    component ReportField: ColumnLayout {
        property string label
        property string placeholder
        property alias text: input.text

        spacing: 6

        Text {
            Layout.fillWidth: true
            text: label
            color: Theme.muted
            font.pixelSize: 12
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        ThemedTextField {
            id: input
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            placeholderText: placeholder
        }
    }

    component ReportSectionOption: CheckBox {
        id: control

        property string section

        Layout.fillWidth: true
        Layout.preferredHeight: 34
        leftPadding: 8
        rightPadding: 8
        spacing: 10
        checked: viewModel && viewModel.reportSectionEnabled(section)
        enabled: viewModel && !viewModel.isPreviewing && !viewModel.isExporting

        onToggled: if (viewModel) {
            viewModel.setReportSectionEnabled(section, checked)
            root.scheduleAutoPreview()
        }

        background: Rectangle {
            radius: 6
            color: control.checked ? Theme.selectedBg : Theme.inputBg
            border.width: 1
            border.color: control.checked ? Theme.selectedLine : Theme.line
            opacity: control.enabled ? 1.0 : 0.52
        }

        indicator: Rectangle {
            implicitWidth: 18
            implicitHeight: 18
            x: control.leftPadding
            y: (control.height - height) / 2
            radius: 4
            color: control.checked ? Theme.primaryBg : Theme.panel2
            border.width: 1
            border.color: control.checked ? Theme.primaryHover : Theme.line

            Text {
                anchors.centerIn: parent
                text: control.checked ? "✓" : ""
                color: Theme.primaryText
                font.pixelSize: 13
                font.weight: Font.Black
            }
        }

        contentItem: Text {
            leftPadding: control.leftPadding + control.indicator.width + control.spacing
            rightPadding: control.rightPadding
            text: control.text
            color: control.enabled ? Theme.text : Theme.weak
            font.pixelSize: 13
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    Component.onCompleted: {
        if (viewModel) {
            timeField.text = viewModel.defaultShootTime
            lastObservedSelectedSourceId = viewModel.selectedSourceId
            scheduleAutoPreview()
        }
    }

    Connections {
        target: root.viewModel
        function onStateChanged() {
            if (!root.viewModel) {
                return
            }
            if (timeField.text.length === 0) {
                timeField.text = root.viewModel.defaultShootTime
                root.scheduleAutoPreview()
            }
            if (root.lastObservedSelectedSourceId !== root.viewModel.selectedSourceId) {
                root.lastObservedSelectedSourceId = root.viewModel.selectedSourceId
                if (root.viewModel.hasSelectedSource) {
                    root.scheduleAutoPreview()
                } else {
                    previewRefreshTimer.stop()
                }
            }
        }
    }

    Timer {
        id: previewRefreshTimer
        interval: 350
        repeat: false
        onTriggered: root.triggerPreview()
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        Rectangle {
            Layout.preferredWidth: 420
            Layout.fillHeight: true
            radius: 10
            color: Theme.panel2
            border.width: 1
            border.color: Theme.line

            Flickable {
                id: reportFormFlick

                anchors.fill: parent
                anchors.margins: 16
                contentWidth: width
                contentHeight: formColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ThemedScrollBar { policy: ScrollBar.AsNeeded }

                MiddleDragScrollHandler {
                    flickable: reportFormFlick
                }

                ColumnLayout {
                    id: formColumn
                    width: parent.width
                    spacing: 14

                    Text {
                        Layout.fillWidth: true
                        text: "数据报表"
                        color: Theme.text
                        font.pixelSize: 28
                        font.weight: Font.Black
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        text: viewModel ? viewModel.statusText : "报表模块未初始化。"
                        color: Theme.muted
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 96
                        radius: 8
                        color: Theme.card
                        border.width: 1
                        border.color: Theme.line

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 6

                            Text {
                                Layout.fillWidth: true
                                text: viewModel ? viewModel.projectName : "未打开项目"
                                color: Theme.text
                                font.pixelSize: 18
                                font.weight: Font.Black
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.fillWidth: true
                                text: viewModel && viewModel.projectPath.length > 0 ? viewModel.projectPath : "打开真实项目后可生成预览和导出 PDF。"
                                color: Theme.muted
                                font.pixelSize: 12
                                elide: Text.ElideMiddle
                            }

                            Text {
                                Layout.fillWidth: true
                                text: viewModel ? viewModel.scopeText : "范围：报表模块未初始化。"
                                color: Theme.weak
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "表头信息"
                        color: Theme.text
                        font.pixelSize: 18
                        font.weight: Font.Black
                        elide: Text.ElideRight
                    }

                    ReportField {
                        id: timeField
                        Layout.fillWidth: true
                        label: "时间"
                        placeholder: "例如 2026-06-13 18:30"
                        onTextChanged: root.scheduleAutoPreview()
                    }

                    ReportField {
                        id: locationField
                        Layout.fillWidth: true
                        label: "地点"
                        placeholder: "例如 上海 / 影棚 A"
                        onTextChanged: root.scheduleAutoPreview()
                    }

                    ReportField {
                        id: directorField
                        Layout.fillWidth: true
                        label: "导演"
                        placeholder: "导演姓名"
                        onTextChanged: root.scheduleAutoPreview()
                    }

                    ReportField {
                        id: cinematographerField
                        Layout.fillWidth: true
                        label: "摄影"
                        placeholder: "摄影指导 / 摄影师"
                        onTextChanged: root.scheduleAutoPreview()
                    }

                    ReportField {
                        id: ditField
                        Layout.fillWidth: true
                        label: "DIT"
                        placeholder: "DIT / 数据管理员"
                        onTextChanged: root.scheduleAutoPreview()
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        ActionButton {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 38
                            text: viewModel && viewModel.isPreviewing ? "生成中" : "刷新预览"
                            primary: true
                            enabled: viewModel && viewModel.canPreview
                            onClicked: root.triggerPreview()
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 38
                            text: viewModel && viewModel.isExporting ? "导出中" : "导出 PDF"
                            enabled: viewModel && viewModel.canExport
                            onClicked: viewModel.exportPdf(timeField.text, locationField.text, directorField.text, cinematographerField.text, ditField.text)
                        }
                    }

                    ActionButton {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        text: "打开导出目录"
                        enabled: viewModel && viewModel.lastExportPath.length > 0
                        onClicked: viewModel.openLastExportFolder()
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: reportOptionsColumn.implicitHeight + 28
                        radius: 8
                        color: Theme.panel
                        border.width: 1
                        border.color: Theme.line

                        ColumnLayout {
                            id: reportOptionsColumn
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 4

                            Text {
                                Layout.fillWidth: true
                                text: "PDF 内容（勾选后导出）"
                                color: Theme.text
                                font.pixelSize: 15
                                font.weight: Font.Black
                                elide: Text.ElideRight
                            }

                            ReportSectionOption {
                                text: "封面与项目信息"
                                section: "cover"
                            }

                            ReportSectionOption {
                                text: "项目摘要"
                                section: "summary"
                            }

                            ReportSectionOption {
                                text: "素材源与扫描概览"
                                section: "sourceOverview"
                            }

                            ReportSectionOption {
                                text: "格式分布"
                                section: "formatDistribution"
                            }

                            ReportSectionOption {
                                text: "视频帧缩略图索引"
                                section: "thumbnailIndex"
                            }

                            ReportSectionOption {
                                text: "视频元数据明细"
                                section: "videoMetadata"
                            }

                            ReportSectionOption {
                                text: "音频元数据明细"
                                section: "audioMetadata"
                            }

                            ReportSectionOption {
                                text: "文件夹树状图"
                                section: "folderTree"
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 10
            color: Theme.panel2
            border.width: 1
            border.color: Theme.line

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: "报表预览"
                        color: Theme.text
                        font.pixelSize: 20
                        font.weight: Font.Black
                        elide: Text.ElideRight
                    }

                    ActionButton {
                        Layout.preferredWidth: 72
                        Layout.preferredHeight: 34
                        text: "上一页"
                        enabled: viewModel && viewModel.hasPreview && viewModel.previewPageIndex > 0
                        onClicked: viewModel.previousPreviewPage()
                    }

                    Text {
                        Layout.preferredWidth: 86
                        text: viewModel && viewModel.hasPreview ? (viewModel.previewPageIndex + 1) + " / " + viewModel.previewPageCount : "0 / 0"
                        color: Theme.muted
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    ActionButton {
                        Layout.preferredWidth: 72
                        Layout.preferredHeight: 34
                        text: "下一页"
                        enabled: viewModel && viewModel.hasPreview && viewModel.previewPageIndex + 1 < viewModel.previewPageCount
                        onClicked: viewModel.nextPreviewPage()
                    }

                    ActionButton {
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 34
                        text: "-"
                        enabled: viewModel && viewModel.hasPreview
                        onClicked: viewModel.zoomPreviewOut()
                    }

                    Text {
                        Layout.preferredWidth: 58
                        text: viewModel ? Math.round(viewModel.previewZoom * 100) + "%" : "75%"
                        color: Theme.muted
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    ActionButton {
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 34
                        text: "+"
                        enabled: viewModel && viewModel.hasPreview
                        onClicked: viewModel.zoomPreviewIn()
                    }

                    ActionButton {
                        Layout.preferredWidth: 76
                        Layout.preferredHeight: 34
                        text: "重置"
                        enabled: viewModel && viewModel.hasPreview
                        onClicked: viewModel.resetPreviewZoom()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 8
                    color: Theme.mediaSurface
                    border.width: 1
                    border.color: Theme.line
                    clip: true

                    Flickable {
                        id: previewFlick
                        anchors.fill: parent
                        anchors.margins: 12
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        contentWidth: Math.max(width, previewImage.width + 24)
                        contentHeight: Math.max(height, previewImage.height + 24)
                        ScrollBar.horizontal: ThemedScrollBar { policy: ScrollBar.AsNeeded }
                        ScrollBar.vertical: ThemedScrollBar { policy: ScrollBar.AsNeeded }

                        MiddleDragScrollHandler {
                            flickable: previewFlick
                        }

                        Image {
                            id: previewImage
                            visible: viewModel && viewModel.hasPreview
                            source: viewModel && viewModel.hasPreview ? viewModel.previewPageUrl : ""
                            cache: false
                            asynchronous: true
                            fillMode: Image.PreserveAspectFit
                            width: sourceSize.width > 0 ? sourceSize.width * viewModel.previewZoom : previewFlick.width
                            height: sourceSize.height > 0 ? sourceSize.height * viewModel.previewZoom : previewFlick.height
                            x: Math.max(12, (previewFlick.width - width) / 2)
                            y: 12
                        }
                    }

                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 48, 420)
                        spacing: 10
                        visible: !viewModel || !viewModel.hasPreview

                        Text {
                            Layout.fillWidth: true
                            text: viewModel && viewModel.isPreviewing
                                ? "正在生成报表预览..."
                                : (viewModel && !viewModel.hasSelectedSource ? "请先选择素材目录" : "还没有预览")
                            color: Theme.text
                            font.pixelSize: 20
                            font.weight: Font.Black
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                        }

                        Text {
                            Layout.fillWidth: true
                            text: viewModel && !viewModel.hasSelectedSource
                                ? "先在左侧素材源栏选择需要导出的素材目录；选中后，这里会自动生成该目录的报表预览。"
                                : "进入页面后会自动生成当前素材目录预览；填写表头信息时，这里也会自动同步刷新与最终 PDF 同源渲染的页面图片。"
                            color: Theme.muted
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }
}
