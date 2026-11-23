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

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    Layout.fillWidth: true
                    text: "项目库"
                    color: Theme.text
                    font.pixelSize: 28
                    font.weight: Font.Black
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    text: viewModel ? viewModel.statusText : "项目库未接入"
                    color: Theme.muted
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
            }

            ActionButton {
                Layout.preferredWidth: 92
                Layout.preferredHeight: 36
                text: "新建项目"
                primary: true
                enabled: !!viewModel
                onClicked: viewModel.createProject()
            }

            ActionButton {
                Layout.preferredWidth: 104
                Layout.preferredHeight: 36
                text: "打开项目"
                enabled: !!viewModel
                onClicked: viewModel.openExternalProject()
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            GridView {
                id: projectGrid

                anchors.fill: parent
                clip: true
                cellWidth: 306
                cellHeight: 224
                model: viewModel ? viewModel.model : null

                MiddleDragScrollHandler {
                    flickable: projectGrid
                }

                delegate: Rectangle {
                    width: 286
                    height: 204
                    radius: 8
                    color: current ? Theme.selectedBg : Theme.panel2
                    border.width: 1
                    border.color: current ? Theme.selectedLine : Theme.line

                    ThemedMenu {
                        id: projectContextMenu

                        ThemedMenuItem {
                            text: "打开项目"
                            enabled: available
                            onTriggered: viewModel.openProject(databasePath)
                        }
                        ThemedMenuSeparator {}
                        ThemedMenuItem {
                            text: "重命名"
                            enabled: available
                            onTriggered: viewModel.renameProject(databasePath, model.name)
                        }
                        ThemedMenuItem {
                            text: "移动位置"
                            enabled: available
                            onTriggered: viewModel.moveProject(databasePath)
                        }
                        ThemedMenuSeparator {}
                        ThemedMenuItem {
                            text: available ? "删除项目" : "移除缺失入口"
                            onTriggered: viewModel.deleteProject(databasePath, available)
                        }
                    }

                    MouseArea {
                        id: projectMouseArea
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: function(mouse) {
                            if (mouse.button === Qt.RightButton) {
                                projectContextMenu.popup(projectMouseArea, mouse.x, mouse.y)
                            } else if (available) {
                                viewModel.openProject(databasePath)
                            }
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                Layout.fillWidth: true
                                text: model.name
                                color: Theme.text
                                font.pixelSize: 17
                                font.weight: Font.Black
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }

                            StatusChip {
                                Layout.maximumWidth: 86
                                label: statusLabel
                                tint: statusColor
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 38
                            text: rootPath
                            color: Theme.muted
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideMiddle
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: Theme.line
                        }

                        Text {
                            Layout.fillWidth: true
                            text: createdAt.length > 0 ? "创建时间：" + createdAt : "项目数据库：" + databasePath
                            color: Theme.weak
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            text: available ? "点击进入素材库" : "项目文件不可访问"
                            color: available ? Theme.blue : Theme.red
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            ActionButton {
                                Layout.preferredWidth: 50
                                Layout.preferredHeight: 30
                                text: "打开"
                                textPixelSize: 12
                                enabled: available
                                onClicked: viewModel.openProject(databasePath)
                            }

                            ActionButton {
                                Layout.preferredWidth: 64
                                Layout.preferredHeight: 30
                                text: "重命名"
                                textPixelSize: 12
                                enabled: available
                                onClicked: viewModel.renameProject(databasePath, model.name)
                            }

                            ActionButton {
                                Layout.preferredWidth: 52
                                Layout.preferredHeight: 30
                                text: "移动"
                                textPixelSize: 12
                                enabled: available
                                onClicked: viewModel.moveProject(databasePath)
                            }

                            ActionButton {
                                Layout.preferredWidth: 52
                                Layout.preferredHeight: 30
                                text: available ? "删除" : "移除"
                                textPixelSize: 12
                                danger: true
                                enabled: !!viewModel
                                onClicked: viewModel.deleteProject(databasePath, available)
                            }
                        }
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                visible: projectGrid.count === 0
                color: "transparent"

                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 48, 460)
                    spacing: 12

                    Text {
                        Layout.fillWidth: true
                        text: "暂无项目"
                        color: Theme.text
                        font.pixelSize: 22
                        font.weight: Font.Black
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "新建项目后会在这里生成项目卡片"
                        color: Theme.muted
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 10

                        ActionButton {
                            Layout.preferredWidth: 92
                            Layout.preferredHeight: 36
                            text: "新建项目"
                            primary: true
                            enabled: !!viewModel
                            onClicked: viewModel.createProject()
                        }

                        ActionButton {
                            Layout.preferredWidth: 104
                            Layout.preferredHeight: 36
                            text: "打开项目"
                            enabled: !!viewModel
                            onClicked: viewModel.openExternalProject()
                        }
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: viewModel ? viewModel.lastMessage : ""
            color: Theme.muted
            font.pixelSize: 12
            elide: Text.ElideRight
            visible: text.length > 0
        }
    }
}
