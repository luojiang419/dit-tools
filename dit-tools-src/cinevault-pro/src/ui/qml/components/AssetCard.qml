import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CineVault

Rectangle {
    id: assetCard

    property string title: ""
    property string subtitle: ""
    property string meta: ""
    property string tag: ""
    property string thumbnailPath: ""
    property bool thumbnailLoading: false
    property string thumbnailSource: localImageUrlHelper ? localImageUrlHelper.sourceForInput(thumbnailPath) : ""
    property bool selected: false
    property bool favorite: false
    property int previewHeight: 146
    readonly property string extensionLabel: {
        var dot = title.lastIndexOf(".")
        var raw = dot >= 0 ? title.substring(dot + 1) : tag
        raw = raw.length > 0 ? raw.toUpperCase() : "FILE"
        return raw.substring(0, 5)
    }

    function fileIconKind() {
        if (tag.indexOf("视频") >= 0) return "VID"
        if (tag.indexOf("音频") >= 0) return "AUD"
        if (tag.indexOf("图片") >= 0) return "IMG"
        if (tag.indexOf("字幕") >= 0) return "SUB"
        if (tag.indexOf("工程") >= 0) return "PRJ"
        if (tag.indexOf("文档") >= 0) return "DOC"
        if (tag.indexOf("压缩包") >= 0) return "ZIP"
        return "FILE"
    }

    function fileIconAccent() {
        var kind = fileIconKind()
        if (kind === "VID") return Qt.rgba(0.31, 0.55, 1.0, 1.0)
        if (kind === "AUD") return Qt.rgba(0.13, 0.77, 0.37, 1.0)
        if (kind === "IMG") return Qt.rgba(0.08, 0.72, 0.65, 1.0)
        if (kind === "SUB") return Qt.rgba(0.66, 0.33, 0.97, 1.0)
        if (kind === "PRJ") return Qt.rgba(0.96, 0.62, 0.04, 1.0)
        if (kind === "DOC") return Qt.rgba(0.22, 0.74, 0.97, 1.0)
        if (kind === "ZIP") return Qt.rgba(0.98, 0.45, 0.09, 1.0)
        return Theme.isDark ? Qt.rgba(0.58, 0.64, 0.72, 1.0) : Qt.rgba(0.39, 0.45, 0.55, 1.0)
    }

    function fileIconTint() {
        var kind = fileIconKind()
        if (kind === "VID") return Qt.rgba(0.11, 0.25, 0.48, 1.0)
        if (kind === "AUD") return Qt.rgba(0.08, 0.33, 0.18, 1.0)
        if (kind === "IMG") return Qt.rgba(0.07, 0.37, 0.35, 1.0)
        if (kind === "SUB") return Qt.rgba(0.30, 0.11, 0.58, 1.0)
        if (kind === "PRJ") return Qt.rgba(0.49, 0.18, 0.07, 1.0)
        if (kind === "DOC") return Qt.rgba(0.03, 0.35, 0.52, 1.0)
        if (kind === "ZIP") return Qt.rgba(0.49, 0.18, 0.07, 1.0)
        return Theme.isDark ? Qt.rgba(0.20, 0.25, 0.33, 1.0) : Qt.rgba(0.80, 0.84, 0.88, 1.0)
    }

    radius: 18
    color: Theme.panel2
    border.width: 1
    border.color: selected ? Theme.blue : Theme.line

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: previewHeight
            radius: 18
            clip: true
            color: Theme.mediaSurface
            border.width: 1
            border.color: Theme.line

            Rectangle {
                id: fallbackPreview
                anchors.fill: parent
                visible: previewImage.status !== Image.Ready
                radius: 18
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(fileIconTint().r, fileIconTint().g, fileIconTint().b, Theme.isDark ? 0.72 : 0.28) }
                    GradientStop { position: 1.0; color: Theme.isDark ? "#11151D" : "#F7FAFF" }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 10
                    radius: 12
                    color: Qt.rgba(0.0, 0.0, 0.0, Theme.isDark ? 0.18 : 0.04)
                    border.width: 1
                    border.color: Qt.rgba(1.0, 1.0, 1.0, Theme.isDark ? 0.08 : 0.44)
                }

                Rectangle {
                    id: typeIcon
                    width: Math.min(92, Math.max(70, fallbackPreview.width * 0.40))
                    height: Math.min(108, Math.max(82, fallbackPreview.height * 0.68))
                    anchors.centerIn: parent
                    radius: 12
                    color: Theme.isDark ? Qt.rgba(0.07, 0.09, 0.13, 0.86) : Qt.rgba(1.0, 1.0, 1.0, 0.92)
                    border.width: 1
                    border.color: Qt.rgba(fileIconAccent().r, fileIconAccent().g, fileIconAccent().b, 0.68)

                    Rectangle {
                        width: parent.width
                        height: 8
                        radius: 4
                        anchors.top: parent.top
                        color: fileIconAccent()
                    }

                    Rectangle {
                        width: 24
                        height: 24
                        anchors.top: parent.top
                        anchors.right: parent.right
                        color: Qt.rgba(fileIconAccent().r, fileIconAccent().g, fileIconAccent().b, 0.28)
                        border.width: 1
                        border.color: Qt.rgba(fileIconAccent().r, fileIconAccent().g, fileIconAccent().b, 0.52)
                    }

                    Column {
                        anchors.fill: parent
                        anchors.topMargin: 20
                        anchors.bottomMargin: 12
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8

                        Text {
                            width: parent.width
                            height: 32
                            text: assetCard.fileIconKind()
                            color: fileIconAccent()
                            font.pixelSize: 26
                            font.weight: Font.Black
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        Row {
                            width: parent.width
                            height: 18
                            spacing: 4
                            visible: assetCard.fileIconKind() === "AUD"
                            Repeater {
                                model: [9, 15, 11, 18, 7]
                                Rectangle {
                                    width: 7
                                    height: modelData
                                    radius: 3
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: fileIconAccent()
                                    opacity: 0.82
                                }
                            }
                        }

                        Row {
                            width: parent.width
                            height: 18
                            spacing: 5
                            visible: assetCard.fileIconKind() === "VID"
                            Repeater {
                                model: 5
                                Rectangle {
                                    width: 8
                                    height: 12
                                    radius: 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: fileIconAccent()
                                    opacity: 0.82
                                }
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: 18
                            radius: 9
                            visible: assetCard.fileIconKind() !== "AUD" && assetCard.fileIconKind() !== "VID"
                            color: Qt.rgba(fileIconAccent().r, fileIconAccent().g, fileIconAccent().b, 0.16)
                            border.width: 1
                            border.color: Qt.rgba(fileIconAccent().r, fileIconAccent().g, fileIconAccent().b, 0.34)
                        }
                    }
                }

                Rectangle {
                    height: 24
                    width: Math.min(76, Math.max(46, extensionText.implicitWidth + 22))
                    radius: 12
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 12
                    color: Qt.rgba(0.04, 0.05, 0.07, Theme.isDark ? 0.78 : 0.56)
                    border.width: 1
                    border.color: Qt.rgba(fileIconAccent().r, fileIconAccent().g, fileIconAccent().b, 0.58)

                    Text {
                        id: extensionText
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        text: assetCard.extensionLabel
                        color: "#FFFFFF"
                        font.pixelSize: 11
                        font.weight: Font.Bold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
            }

            Image {
                id: previewImage
                anchors.fill: parent
                source: thumbnailSource
                sourceSize.width: Math.max(1, Math.round(width))
                sourceSize.height: Math.max(1, Math.round(height))
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
                visible: status === Image.Ready
            }

            Rectangle {
                anchors.fill: parent
                visible: assetCard.thumbnailLoading && previewImage.status !== Image.Ready
                color: Qt.rgba(0.03, 0.04, 0.06, 0.62)

                Column {
                    anchors.centerIn: parent
                    spacing: 8

                    BusyIndicator {
                        anchors.horizontalCenter: parent.horizontalCenter
                        running: parent.parent.visible
                        width: 30
                        height: 30
                    }

                    Text {
                        text: "缩略图生成中"
                        color: "#F3F7FF"
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }

            Rectangle {
                width: 30
                height: 30
                radius: 15
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 10
                visible: favorite
                color: Qt.rgba(0.05, 0.06, 0.08, 0.72)
                border.width: 1
                border.color: Qt.rgba(1.0, 0.82, 0.32, 0.72)

                Text {
                    anchors.centerIn: parent
                    text: "★"
                    color: "#FFD15A"
                    font.pixelSize: 17
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            spacing: 6

            Text {
                Layout.fillWidth: true
                text: title
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                text: subtitle
                color: Theme.muted
                font.pixelSize: 12
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                text: meta
                color: Theme.muted
                font.pixelSize: 12
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            StatusChip {
                Layout.maximumWidth: parent.width
                label: tag
                tint: fileIconAccent()
            }
        }
    }
}
