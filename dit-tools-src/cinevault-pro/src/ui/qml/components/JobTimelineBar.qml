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

    component StatusLamp: Item {
        property string label
        property int count: 0
        property color tint: Theme.blue

        Layout.preferredWidth: 90
        Layout.fillHeight: true

        RowLayout {
            anchors.fill: parent
            spacing: 7

            Item {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24

                Rectangle {
                    anchors.centerIn: parent
                    width: 24
                    height: 24
                    radius: 12
                    color: Qt.rgba(tint.r, tint.g, tint.b, count > 0 ? 0.36 : 0.24)
                    opacity: count > 0 ? 0.95 : 0.82

                    SequentialAnimation on opacity {
                        running: count > 0
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.58; duration: 900; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 0.95; duration: 900; easing.type: Easing.InOutSine }
                    }
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: 15
                    height: 15
                    radius: 8
                    color: Qt.rgba(tint.r, tint.g, tint.b, 0.30)
                    border.width: 1
                    border.color: Qt.rgba(tint.r, tint.g, tint.b, 0.95)
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: 7
                    height: 7
                    radius: 4
                    color: tint
                    opacity: 1.0
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Text {
                    Layout.fillWidth: true
                    text: label
                    color: Theme.muted
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    text: count
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.Black
                    elide: Text.ElideRight
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 12

        StatusLamp {
            label: "总量"
            count: libraryViewModel ? libraryViewModel.totalAssetCount : 0
            tint: Theme.blue
        }

        StatusLamp {
            label: "就绪"
            count: libraryViewModel ? libraryViewModel.readyAssetCount : 0
            tint: Theme.green
        }

        StatusLamp {
            label: "待解析"
            count: libraryViewModel ? libraryViewModel.pendingAssetCount : 0
            tint: Theme.blue
        }

        StatusLamp {
            label: "需关注"
            count: libraryViewModel ? libraryViewModel.issueAssetCount : 0
            tint: Theme.orange
        }

        Rectangle {
            Layout.preferredWidth: 150
            Layout.preferredHeight: 9
            radius: 5
            color: Theme.panel
            border.width: 1
            border.color: Theme.line
            clip: true

            RowLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillHeight: true
                    Layout.preferredWidth: (libraryViewModel && libraryViewModel.totalAssetCount > 0) ? parent.width * libraryViewModel.readyAssetCount / libraryViewModel.totalAssetCount : 0
                    color: Theme.green
                }

                Rectangle {
                    Layout.fillHeight: true
                    Layout.preferredWidth: (libraryViewModel && libraryViewModel.totalAssetCount > 0) ? parent.width * libraryViewModel.pendingAssetCount / libraryViewModel.totalAssetCount : 0
                    color: Theme.blue
                }

                Rectangle {
                    Layout.fillHeight: true
                    Layout.preferredWidth: (libraryViewModel && libraryViewModel.totalAssetCount > 0) ? parent.width * libraryViewModel.issueAssetCount / libraryViewModel.totalAssetCount : 0
                    color: Theme.orange
                }

                Item { Layout.fillWidth: true }
            }
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.fillHeight: true
            color: Theme.line
        }

        Flickable {
            id: timelineFlick
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: timelineRow.implicitWidth
            contentHeight: timelineRow.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.horizontal: ThemedScrollBar { policy: ScrollBar.AsNeeded }

            Row {
                id: timelineRow
                height: parent.height
                spacing: 10

                Repeater {
                    model: viewModel ? viewModel.timelineItems : []
                    delegate: Rectangle {
                        height: 28
                        width: Math.min(260, Math.max(160, detailText.implicitWidth + 24))
                        radius: 14
                        color: Theme.panel2
                        border.width: 1
                        border.color: Theme.line

                        Text {
                            id: detailText
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            text: modelData.subjectName + " " + modelData.progress + "% · "
                                + (modelData.progressLabel && modelData.progressLabel.length > 0 ? modelData.progressLabel : modelData.stateLabel)
                            color: Theme.text
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }
    }
}
