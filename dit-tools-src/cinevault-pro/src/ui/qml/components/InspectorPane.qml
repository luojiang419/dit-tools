import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CineVault

Rectangle {
    property var viewModel
    property var mediaViewModel

    color: Theme.panel
    border.width: 1
    border.color: Theme.line

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14

        Text {
            Layout.fillWidth: true
            text: viewModel.title
            color: Theme.text
            font.pixelSize: 18
            font.weight: Font.Bold
            elide: Text.ElideRight
        }

        Text {
            Layout.fillWidth: true
            text: viewModel.subtitle
            color: Theme.muted
            font.pixelSize: 13
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }

        VideoPreviewPlayer {
            Layout.fillWidth: true
            Layout.preferredHeight: 210
            sourceUrl: mediaViewModel ? mediaViewModel.selectedPreviewUrl : ""
            thumbnailUrl: mediaViewModel ? mediaViewModel.selectedPreviewThumbnailUrl : ""
            title: mediaViewModel ? mediaViewModel.selectedPreviewTitle : ""
            isVideo: mediaViewModel ? mediaViewModel.selectedPreviewIsVideo : false
        }

        ScrollView {
            id: detailScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            MiddleDragScrollHandler {
                parent: detailScroll.contentItem
                flickable: detailScroll.contentItem
            }

            Column {
                width: detailScroll.availableWidth
                spacing: 10

                Repeater {
                    model: viewModel.details
                    delegate: Rectangle {
                        width: parent.width
                        radius: 14
                        color: Theme.panel2
                        border.width: 1
                        border.color: Theme.line
                        implicitHeight: contentColumn.implicitHeight + 20

                        Column {
                            id: contentColumn
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6

                            Text {
                                width: parent.width
                                text: modelData.label
                                color: Theme.muted
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }

                            Text {
                                width: parent.width
                                text: modelData.value
                                color: Theme.text
                                font.pixelSize: 13
                                wrapMode: Text.WrapAnywhere
                            }
                        }
                    }
                }
            }
        }
    }
}
