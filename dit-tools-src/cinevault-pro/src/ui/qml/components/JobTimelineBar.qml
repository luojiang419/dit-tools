import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CineVault

Rectangle {
    property var viewModel
    property var libraryViewModel

    color: Theme.topBar
    border.width: 1
    border.color: Theme.line
    implicitHeight: 62

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        spacing: 14

        ColumnLayout {
            Layout.preferredWidth: 118
            Layout.fillHeight: true
            spacing: 1

            Text {
                text: "任务中心"
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.Black
            }

            Text {
                text: viewModel ? viewModel.footerProgressText : "暂无后台任务"
                color: Theme.muted
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: 680
            Layout.fillHeight: true
            spacing: 5

            ThemedProgressBar {
                id: totalProgress
                Layout.fillWidth: true
                Layout.preferredHeight: 8
                from: 0
                to: 100
                value: viewModel ? viewModel.footerProgress : 0

                Behavior on value {
                    NumberAnimation {
                        duration: 220
                        easing.type: Easing.OutCubic
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: viewModel ? viewModel.footerSummary : "任务状态加载中…"
                color: Theme.muted
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }

        RowLayout {
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            spacing: 8

            component TaskCount: Rectangle {
                id: taskCount
                required property string label
                required property int count
                required property color tint

                Layout.fillWidth: true
                Layout.preferredHeight: 36
                radius: 10
                color: Theme.panel2
                border.width: 1
                border.color: Theme.line

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 9
                    anchors.rightMargin: 9
                    spacing: 5

                    Rectangle {
                        Layout.preferredWidth: 7
                        Layout.preferredHeight: 7
                        radius: 4
                        color: taskCount.tint
                    }

                    Text {
                        Layout.fillWidth: true
                        text: taskCount.label
                        color: Theme.muted
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }

                    Text {
                        text: taskCount.count
                        color: Theme.text
                        font.pixelSize: 13
                        font.weight: Font.Black
                    }
                }
            }

            TaskCount {
                label: "运行"
                count: viewModel ? viewModel.footerRunningCount : 0
                tint: Theme.blue
            }

            TaskCount {
                label: "等待"
                count: viewModel ? viewModel.footerPendingCount : 0
                tint: Theme.orange
            }

            TaskCount {
                label: "异常"
                count: viewModel ? viewModel.footerFailedCount : 0
                tint: Theme.red
            }
        }
    }
}
