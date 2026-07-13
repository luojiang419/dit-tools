import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CineVault

Rectangle {
    property var viewModel
    readonly property bool hasSelectedTask: viewModel && viewModel.hasSelection
    readonly property bool hasCurrentItem: viewModel && viewModel.hasActiveBatchItem
    readonly property int currentProgress: viewModel ? viewModel.batchCurrentProgress : 0
    readonly property int batchProgress: viewModel ? viewModel.batchProgress : 0
    readonly property string selectedThumbnailPath: hasSelectedTask ? viewModel.selectedSubjectThumbnailPath : ""
    readonly property string selectedThumbnailSource: selectedThumbnailPath.length > 0 && localImageUrlHelper
        ? localImageUrlHelper.sourceForInput(selectedThumbnailPath)
        : ""

    function metricValue(value) {
        return viewModel ? String(value) : "0"
    }

    function fallbackText(value, fallback) {
        return value && value.length > 0 ? value : fallback
    }

    color: Theme.panel
    border.width: 1
    border.color: Theme.line

    component StatTile: Rectangle {
        property string label
        property string value
        property color valueColor: Theme.text

        Layout.fillWidth: true
        Layout.preferredHeight: 64
        radius: 12
        color: Theme.panel2
        border.width: 1
        border.color: Theme.line

        Column {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 4

            Text {
                width: parent.width
                text: label
                color: Theme.muted
                font.pixelSize: 11
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: value
                color: valueColor
                font.pixelSize: 17
                font.weight: Font.Black
                elide: Text.ElideRight
            }
        }
    }

    component InfoRow: RowLayout {
        property string label
        property string value

        Layout.fillWidth: true
        spacing: 10

        Text {
            Layout.preferredWidth: 68
            text: label
            color: Theme.muted
            font.pixelSize: 12
            elide: Text.ElideRight
        }

        Text {
            Layout.fillWidth: true
            text: value
            color: Theme.text
            font.pixelSize: 12
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Text {
                    Layout.fillWidth: true
                    text: "任务详情"
                    color: Theme.text
                    font.pixelSize: 18
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }

                Text {
                    text: hasSelectedTask ? viewModel.selectedJobStateLabel : ""
                    color: Theme.muted
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }

            Text {
                Layout.fillWidth: true
                text: hasSelectedTask ? fallbackText(viewModel.selectedSubjectName, viewModel.selectedJobTitle) : "选择左侧任务查看详情"
                color: Theme.muted
                font.pixelSize: 13
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 226
            radius: 16
            color: Theme.mediaSurface
            border.width: 1
            border.color: Theme.line
            clip: true

            Image {
                anchors.fill: parent
                anchors.margins: 1
                source: selectedThumbnailSource
                visible: selectedThumbnailSource.length > 0
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
            }

            Rectangle {
                anchors.fill: parent
                visible: selectedThumbnailSource.length === 0
                color: "transparent"

                Column {
                    anchors.centerIn: parent
                    width: parent.width - 32
                    spacing: 8

                    Text {
                        width: parent.width
                        text: hasSelectedTask ? fallbackText(viewModel.selectedSubjectTypeLabel, viewModel.selectedJobTypeLabel) : "等待任务"
                        color: Theme.text
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        text: hasSelectedTask ? viewModel.selectedSubjectThumbnailStatusLabel : "点击任务卡片后显示素材或对象详情"
                        color: Theme.muted
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 74
                color: Qt.rgba(0, 0, 0, Theme.isDark ? 0.52 : 0.38)
                visible: hasSelectedTask

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6

                    Text {
                        Layout.fillWidth: true
                        text: fallbackText(viewModel.selectedSubjectName, viewModel.selectedJobTitle)
                        color: "white"
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ThemedProgressBar {
                            Layout.fillWidth: true
                            from: 0
                            to: 100
                            value: viewModel ? viewModel.selectedJobProgress : 0
                        }

                        Text {
                            text: (viewModel ? viewModel.selectedJobProgress : 0) + "%"
                            color: "white"
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
        }

        ScrollView {
            id: detailScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ScrollBar.vertical: ThemedScrollBar {
                policy: ScrollBar.AsNeeded
            }

            ColumnLayout {
                width: detailScroll.availableWidth
                spacing: 10

                Rectangle {
                    Layout.fillWidth: true
                    radius: 14
                    color: Theme.panel2
                    border.width: 1
                    border.color: Theme.line
                    implicitHeight: selectedProgressColumn.implicitHeight + 22

                    ColumnLayout {
                        id: selectedProgressColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 11
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Text {
                                Layout.fillWidth: true
                                text: hasSelectedTask ? viewModel.selectedJobTitle : "未选中任务"
                                color: Theme.text
                                font.pixelSize: 14
                                font.weight: Font.Black
                                elide: Text.ElideRight
                            }

                            Text {
                                text: hasSelectedTask ? viewModel.selectedJobProgress + "%" : "0%"
                                color: Theme.text
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                        }

                        ThemedProgressBar {
                            Layout.fillWidth: true
                            from: 0
                            to: 100
                            value: hasSelectedTask ? viewModel.selectedJobProgress : 0
                        }

                        Text {
                            Layout.fillWidth: true
                            text: hasSelectedTask ? fallbackText(viewModel.selectedProgressLabel, "等待进度更新") : "选择左侧任务后显示详细进度。"
                            color: Theme.text
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                        }

                        Text {
                            Layout.fillWidth: true
                            text: hasSelectedTask ? viewModel.selectedJobDetail : ""
                            visible: hasSelectedTask
                            color: Theme.muted
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 14
                    color: Theme.panel2
                    border.width: 1
                    border.color: Theme.line
                    implicitHeight: subjectColumn.implicitHeight + 22

                    ColumnLayout {
                        id: subjectColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 11
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: "对象信息"
                            color: Theme.text
                            font.pixelSize: 14
                            font.weight: Font.Black
                            elide: Text.ElideRight
                        }

                        InfoRow {
                            label: "类型"
                            value: hasSelectedTask ? fallbackText(viewModel.selectedSubjectTypeLabel, viewModel.selectedJobTypeLabel) : "-"
                        }

                        InfoRow {
                            label: "名称"
                            value: hasSelectedTask ? fallbackText(viewModel.selectedSubjectName, viewModel.selectedJobTitle) : "-"
                        }

                        InfoRow {
                            label: "路径"
                            value: hasSelectedTask ? fallbackText(viewModel.selectedSubjectPath, "暂无路径") : "-"
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 14
                    color: Theme.panel2
                    border.width: 1
                    border.color: Theme.line
                    implicitHeight: statusColumn.implicitHeight + 22
                    visible: hasSelectedTask

                    ColumnLayout {
                        id: statusColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 11
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: "状态记录"
                            color: Theme.text
                            font.pixelSize: 14
                            font.weight: Font.Black
                            elide: Text.ElideRight
                        }

                        InfoRow {
                            label: "开始"
                            value: viewModel ? fallbackText(viewModel.selectedJobStartedAt, "-") : "-"
                        }

                        InfoRow {
                            label: "更新"
                            value: viewModel ? fallbackText(viewModel.selectedJobUpdatedAt, "-") : "-"
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: viewModel && viewModel.selectedJobError.length > 0
                            text: viewModel ? viewModel.selectedJobError : ""
                            color: Theme.orange
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 14
                    color: Theme.panel2
                    border.width: 1
                    border.color: Theme.line
                    implicitHeight: batchCardColumn.implicitHeight + 22

                    ColumnLayout {
                        id: batchCardColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 11
                        spacing: 9

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Text {
                                Layout.fillWidth: true
                                text: "素材解析总量"
                                color: Theme.text
                                font.pixelSize: 14
                                font.weight: Font.Black
                                elide: Text.ElideRight
                            }

                            Text {
                                text: batchProgress + "%"
                                color: Theme.text
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                        }

                        ThemedProgressBar {
                            Layout.fillWidth: true
                            from: 0
                            to: 100
                            value: batchProgress
                        }

                        Text {
                            Layout.fillWidth: true
                            text: viewModel ? viewModel.batchStatusText : "暂无素材解析批次。"
                            color: Theme.muted
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }

                        Text {
                            Layout.fillWidth: true
                            text: hasCurrentItem ? viewModel.batchCurrentDetail : "当前没有正在处理的素材。"
                            color: Theme.muted
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 10

                    StatTile {
                        label: "已处理"
                        value: viewModel ? viewModel.batchFinishedCount + "/" + viewModel.batchTotalCount : "0/0"
                    }

                    StatTile {
                        label: "成功"
                        value: metricValue(viewModel ? viewModel.batchSuccessfulCount : 0)
                    }

                    StatTile {
                        label: "失败"
                        value: metricValue(viewModel ? viewModel.batchFailedCount : 0)
                        valueColor: Theme.orange
                    }

                    StatTile {
                        label: "排队中"
                        value: metricValue(viewModel ? viewModel.batchQueuedCount : 0)
                    }
                }
            }
        }
    }
}
