import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CineVault

Rectangle {
    property var viewModel

    color: Theme.bg

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: "任务"
                color: Theme.text
                font.pixelSize: 28
                font.weight: Font.Black
                elide: Text.ElideRight
            }

            ActionButton {
                Layout.preferredWidth: 126
                Layout.preferredHeight: 36
                text: "清理已完成"
                enabled: viewModel && viewModel.canClearCompletedJobs
                onClicked: if (viewModel) viewModel.clearCompletedJobs()
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 14

                Rectangle {
                    Layout.fillWidth: true
                    radius: 20
                    color: Theme.panel2
                    border.width: 1
                    border.color: Theme.line
                    implicitHeight: 182

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4

                                Text {
                                    text: viewModel ? viewModel.batchTitle : "视频解析总量进度"
                                    color: Theme.text
                                    font.pixelSize: 18
                                    font.weight: Font.Black
                                }

                                Text {
                                    text: viewModel ? viewModel.batchProgressText : "暂无批次"
                                    color: Theme.muted
                                    font.pixelSize: 13
                                }
                            }

                            Rectangle {
                                Layout.preferredWidth: 112
                                Layout.preferredHeight: 42
                                radius: 12
                                color: Theme.bg
                                border.width: 1
                                border.color: Theme.line

                                Text {
                                    anchors.centerIn: parent
                                    text: (viewModel ? viewModel.batchProgress : 0) + "%"
                                    color: Theme.text
                                    font.pixelSize: 20
                                    font.weight: Font.Black
                                }
                            }
                        }

                        ThemedProgressBar {
                            Layout.fillWidth: true
                            from: 0
                            to: 100
                            value: viewModel ? viewModel.batchProgress : 0
                        }

                        Text {
                            Layout.fillWidth: true
                            text: viewModel ? viewModel.batchStatusText : "暂无视频解析批次。"
                            color: Theme.text
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 58
                                radius: 14
                                color: Theme.bg
                                border.width: 1
                                border.color: Theme.line

                                Column {
                                    anchors.centerIn: parent
                                    spacing: 4

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: viewModel ? viewModel.batchFinishedCount + "/" + viewModel.batchTotalCount : "0/0"
                                        color: Theme.text
                                        font.pixelSize: 16
                                        font.weight: Font.Black
                                    }

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "已处理"
                                        color: Theme.muted
                                        font.pixelSize: 11
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 58
                                radius: 14
                                color: Theme.bg
                                border.width: 1
                                border.color: Theme.line

                                Column {
                                    anchors.centerIn: parent
                                    spacing: 4

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: viewModel ? String(viewModel.batchSuccessfulCount) : "0"
                                        color: Theme.text
                                        font.pixelSize: 16
                                        font.weight: Font.Black
                                    }

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "成功"
                                        color: Theme.muted
                                        font.pixelSize: 11
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 58
                                radius: 14
                                color: Theme.bg
                                border.width: 1
                                border.color: Theme.line

                                Column {
                                    anchors.centerIn: parent
                                    spacing: 4

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: viewModel ? String(viewModel.batchFailedCount) : "0"
                                        color: Theme.orange
                                        font.pixelSize: 16
                                        font.weight: Font.Black
                                    }

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "失败"
                                        color: Theme.muted
                                        font.pixelSize: 11
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 58
                                radius: 14
                                color: Theme.bg
                                border.width: 1
                                border.color: Theme.line

                                Column {
                                    anchors.centerIn: parent
                                    spacing: 4

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: viewModel ? String(viewModel.batchQueuedCount) : "0"
                                        color: Theme.text
                                        font.pixelSize: 16
                                        font.weight: Font.Black
                                    }

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "排队中"
                                        color: Theme.muted
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 20
                    color: Theme.panel2
                    border.width: 1
                    border.color: Theme.line

                    ListView {
                        id: jobList
                        anchors.fill: parent
                        anchors.margins: 12
                        clip: true
                        spacing: 10
                        model: viewModel ? viewModel.model : null

                        ScrollBar.vertical: ThemedScrollBar {
                            policy: ScrollBar.AsNeeded
                        }

                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 124
                            radius: 16
                            color: viewModel && viewModel.selectedJobId === model.jobId ? Theme.selectedBg : Theme.bg
                            border.width: 1
                            border.color: viewModel && viewModel.selectedJobId === model.jobId ? Theme.blue : Theme.line

                            MouseArea {
                                anchors.fill: parent
                                onClicked: if (viewModel) viewModel.selectJob(model.jobId)
                            }

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 7

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10

                                    Text {
                                        Layout.fillWidth: true
                                        text: model.title
                                        color: Theme.text
                                        font.pixelSize: 15
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        text: model.stateLabel
                                        color: Theme.muted
                                        font.pixelSize: 12
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: model.subjectName && model.subjectName.length > 0 ? model.subjectName : model.jobTypeLabel
                                    color: Theme.text
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: model.detail
                                    color: Theme.muted
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 1
                                    elide: Text.ElideRight
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: model.progressLabel && model.progressLabel.length > 0 ? model.progressLabel : "等待进度更新"
                                    color: Theme.muted
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10

                                    ThemedProgressBar {
                                        Layout.fillWidth: true
                                        from: 0
                                        to: 100
                                        value: model.progress
                                    }

                                    Text {
                                        text: model.progress + "%"
                                        color: Theme.text
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: jobList.count === 0
                            text: "当前还没有任务。"
                            color: Theme.muted
                            font.pixelSize: 14
                        }
                    }
                }
            }
        }
    }
}
