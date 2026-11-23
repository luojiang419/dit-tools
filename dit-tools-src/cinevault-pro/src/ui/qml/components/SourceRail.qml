import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CineVault

Rectangle {
    id: root

    property var shellVm
    property var viewModel
    property var pendingRemoveSourceId: 0
    property string pendingRemoveSourceName: ""
    property string pendingRemoveSourcePath: ""
    property string removeErrorText: ""
    property bool collapsed: false

    signal collapseRequested(bool collapsed)

    color: Theme.panel
    border.width: 1
    border.color: Theme.line

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            color: Theme.panel

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                ActionButton {
                    Layout.preferredWidth: 34
                    Layout.preferredHeight: 34
                    text: root.collapsed ? ">" : "<"
                    textPixelSize: 16
                    onClicked: root.collapseRequested(!root.collapsed)
                }

                Text {
                    Layout.fillWidth: true
                    visible: !root.collapsed
                    text: "源素材"
                    color: Theme.muted
                    font.pixelSize: 15
                    elide: Text.ElideRight
                }

                Item { Layout.fillWidth: true }

                ActionButton {
                    visible: !root.collapsed
                    Layout.preferredWidth: 62
                    Layout.preferredHeight: 34
                    text: "清除"
                    onClicked: viewModel.clearSelection()
                }
            }
        }

        ListView {
            id: sourceList

            Layout.fillWidth: true
            Layout.fillHeight: !root.collapsed
            visible: !root.collapsed
            clip: true
            spacing: 10
            model: viewModel.model

            MiddleDragScrollHandler {
                flickable: sourceList
            }

            delegate: Item {
                width: ListView.view.width
                height: sourceCard.implicitHeight + 28

                SourceCard {
                    id: sourceCard
                    anchors.fill: parent
                    anchors.margins: 14
                    active: viewModel.selectedSourceId === sourceId
                    name: model.name
                    metaText: totalFiles + " 个文件 · " + totalSize + "\n" + videoCount + " 个视频 · " + warningCount + " 个警告"
                    statusLabel: model.statusLabel
                    statusColor: model.statusColor
                    onRemoveRequested: {
                        root.pendingRemoveSourceId = sourceId
                        root.pendingRemoveSourceName = model.name
                        root.pendingRemoveSourcePath = model.path
                        root.removeErrorText = ""
                        confirmRemoveDialog.open()
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    z: -1
                    onClicked: viewModel.selectSource(sourceId)
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.collapsed

            Rectangle {
                width: 28
                height: 24
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 24
                radius: 6
                color: Theme.panel2
                border.width: 1
                border.color: Theme.line

                Rectangle {
                    width: 13
                    height: 5
                    anchors.left: parent.left
                    anchors.leftMargin: 4
                    anchors.top: parent.top
                    anchors.topMargin: -4
                    radius: 3
                    color: Theme.panel2
                    border.width: 1
                    border.color: Theme.line
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.collapsed ? 0 : 76
            visible: !root.collapsed
            color: Theme.panel
            border.width: 1
            border.color: Theme.line

            ActionButton {
                anchors.fill: parent
                anchors.margins: 16
                text: "添加目录 / 磁盘卷"
                primary: true
                onClicked: shellVm.addSourceDirectory()
            }
        }
    }

    Dialog {
        id: confirmRemoveDialog

        modal: true
        parent: Overlay.overlay
        width: 460
        height: root.removeErrorText.length > 0 ? 248 : 224
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
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
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: "移除素材目录"
                color: Theme.text
                font.pixelSize: 20
                font.weight: Font.Black
            }

            Text {
                Layout.fillWidth: true
                text: "确定从当前项目中移除“" + root.pendingRemoveSourceName + "”吗？原始磁盘文件不会被删除。"
                color: Theme.muted
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }

            Text {
                Layout.fillWidth: true
                text: root.pendingRemoveSourcePath
                color: Theme.weak
                font.pixelSize: 12
                elide: Text.ElideMiddle
            }

            Text {
                Layout.fillWidth: true
                visible: root.removeErrorText.length > 0
                text: root.removeErrorText
                color: Theme.red
                font.pixelSize: 12
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
                    onClicked: confirmRemoveDialog.close()
                }

                ActionButton {
                    Layout.preferredWidth: 94
                    Layout.preferredHeight: 36
                    text: "确认移除"
                    danger: true
                    onClicked: {
                        if (viewModel && viewModel.removeSource(root.pendingRemoveSourceId)) {
                            confirmRemoveDialog.close()
                        } else {
                            root.removeErrorText = "移除失败，请确认项目数据库仍可访问。"
                        }
                    }
                }
            }
        }
    }
}
