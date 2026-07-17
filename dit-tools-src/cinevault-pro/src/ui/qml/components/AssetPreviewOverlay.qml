import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Pdf
import CineVault

Rectangle {
    id: overlay

    property var previewVm
    property string mode: ""
    property string imageSource: ""
    property string overlayTitle: ""
    property real imageScale: 1.0
    property real imageOffsetX: 0
    property real imageOffsetY: 0
    property real lastMouseX: 0
    property real lastMouseY: 0

    anchors.fill: parent
    visible: mode.length > 0
    color: Qt.rgba(0, 0, 0, 0.92)
    z: 200
    focus: visible

    function resetImageView() {
        imageScale = 1.0
        imageOffsetX = 0
        imageOffsetY = 0
    }

    function openImage(source, title) {
        if (!source || source.toString().length === 0) {
            return
        }
        if (previewVm) {
            previewVm.clear()
        }
        mode = "image"
        imageSource = localImageUrlHelper
            ? localImageUrlHelper.sourceForInput(source.toString())
            : source.toString()
        overlayTitle = title || ""
        resetImageView()
        forceActiveFocus()
    }

    function openDocument(source, title) {
        if (!previewVm || !source || source.toString().length === 0) {
            return
        }
        previewVm.loadFromFile(source, title || "")
        mode = previewVm.isPdf ? "pdf" : "document"
        imageSource = ""
        overlayTitle = title || ""
        forceActiveFocus()
        if (mode === "pdf") {
            Qt.callLater(function() {
                if (pdfDocument.status === PdfDocument.Ready) {
                    pdfView.scaleToPage(pdfViewport.width - 24, pdfViewport.height - 24)
                }
            })
        }
    }

    function close() {
        mode = ""
        imageSource = ""
        overlayTitle = ""
        resetImageView()
        if (previewVm) {
            previewVm.clear()
        }
    }

    onVisibleChanged: if (visible) forceActiveFocus()
    Keys.onEscapePressed: close()

    PdfDocument {
        id: pdfDocument
        source: previewVm ? previewVm.sourceUrl : ""

        onStatusChanged: function() {
            if (overlay.mode === "pdf" && status === PdfDocument.Ready) {
                Qt.callLater(function() {
                    pdfView.scaleToPage(pdfViewport.width - 24, pdfViewport.height - 24)
                })
            }
        }
    }

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 18
        height: 56
        radius: 18
        color: Qt.rgba(0.05, 0.07, 0.10, 0.72)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.10)

        RowLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: overlayTitle.length > 0 ? overlayTitle : (previewVm ? previewVm.title : "")
                color: "white"
                font.pixelSize: 16
                font.weight: Font.DemiBold
                elide: Text.ElideMiddle
            }

            Rectangle {
                visible: previewVm && previewVm.truncated
                radius: 11
                color: Qt.rgba(0.97, 0.63, 0.16, 0.18)
                border.width: 1
                border.color: Qt.rgba(0.97, 0.63, 0.16, 0.42)
                implicitHeight: 26
                implicitWidth: truncatedLabel.implicitWidth + 18

                Text {
                    id: truncatedLabel
                    anchors.centerIn: parent
                    text: "预览已截断"
                    color: "#FED7AA"
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
            }

            Rectangle {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                radius: 16
                color: closeArea.pressed ? "#334155" : "#1F2937"
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.16)

                Text {
                    anchors.centerIn: parent
                    text: "X"
                    color: "white"
                    font.pixelSize: 16
                    font.weight: Font.Black
                }

                MouseArea {
                    id: closeArea
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: overlay.close()
                }
            }
        }
    }

    Item {
        anchors.fill: parent
        anchors.topMargin: 86
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        anchors.bottomMargin: 24

        visible: overlay.mode === "image"
        clip: true

        readonly property real fitScale: imagePreview.status === Image.Ready
            && imagePreview.implicitWidth > 0
            && imagePreview.implicitHeight > 0
                ? Math.min(width / imagePreview.implicitWidth,
                           height / imagePreview.implicitHeight,
                           1.0)
                : 1.0

        Image {
            id: imagePreview
            source: overlay.imageSource
            sourceSize.width: Math.max(1, Math.round(parent.width))
            sourceSize.height: Math.max(1, Math.round(parent.height))
            asynchronous: true
            cache: false
            smooth: true
            fillMode: Image.Stretch
            width: implicitWidth * parent.fitScale * overlay.imageScale
            height: implicitHeight * parent.fitScale * overlay.imageScale
            x: (parent.width - width) / 2 + overlay.imageOffsetX
            y: (parent.height - height) / 2 + overlay.imageOffsetY
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor

            onPressed: function(mouse) {
                overlay.forceActiveFocus()
                overlay.lastMouseX = mouse.x
                overlay.lastMouseY = mouse.y
            }

            onPositionChanged: function(mouse) {
                if (!pressed) {
                    return
                }
                overlay.imageOffsetX += mouse.x - overlay.lastMouseX
                overlay.imageOffsetY += mouse.y - overlay.lastMouseY
                overlay.lastMouseX = mouse.x
                overlay.lastMouseY = mouse.y
            }

            onDoubleClicked: overlay.resetImageView()

            onWheel: function(wheel) {
                var nextScale = overlay.imageScale * (wheel.angleDelta.y > 0 ? 1.15 : 0.87)
                overlay.imageScale = Math.max(0.2, Math.min(12.0, nextScale))
                wheel.accepted = true
            }
        }
    }

    Rectangle {
        id: pdfViewport
        anchors.fill: parent
        anchors.topMargin: 86
        anchors.leftMargin: 48
        anchors.rightMargin: 48
        anchors.bottomMargin: 24
        visible: overlay.mode === "pdf"
        radius: 20
        color: "#F8FAFC"
        border.width: 1
        border.color: "#D9E3F1"
        clip: true

        PdfMultiPageView {
            id: pdfView
            anchors.fill: parent
            anchors.margins: 12
            document: pdfDocument
        }
    }

    ScrollView {
        id: documentScroll
        anchors.fill: parent
        anchors.topMargin: 86
        anchors.leftMargin: 48
        anchors.rightMargin: 48
        anchors.bottomMargin: 24
        visible: overlay.mode === "document"
        clip: true

        MiddleDragScrollHandler {
            parent: documentScroll.contentItem
            flickable: documentScroll.contentItem
        }

        ScrollBar.vertical: ThemedScrollBar { policy: ScrollBar.AsNeeded }
        ScrollBar.horizontal: ThemedScrollBar { policy: ScrollBar.AsNeeded }

        Item {
            width: Math.max(documentScroll.availableWidth, 760)
            implicitHeight: contentCard.implicitHeight + 32

            Rectangle {
                id: contentCard
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 8
                width: Math.min(parent.width, 920)
                radius: 22
                color: "#FBFCFE"
                border.width: 1
                border.color: "#D9E3F1"
                implicitHeight: Math.max(220, previewColumn.implicitHeight + 36)

                ColumnLayout {
                    id: previewColumn
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 14

                    Text {
                        Layout.fillWidth: true
                        visible: previewVm && previewVm.hasError
                        text: previewVm ? previewVm.errorMessage : ""
                        color: "#9A3412"
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }

                    TextEdit {
                        id: documentText
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(contentHeight, paintedHeight)
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        textFormat: previewVm && previewVm.isMarkdown
                            ? TextEdit.MarkdownText
                            : (previewVm && previewVm.isRichText ? TextEdit.RichText : TextEdit.PlainText)
                        text: previewVm ? previewVm.content : ""
                        color: "#0F172A"
                        selectedTextColor: "#0F172A"
                        selectionColor: "#BFDBFE"
                        font.pixelSize: previewVm && previewVm.isMarkdown ? 16 : 14
                        font.family: previewVm && !previewVm.isRichText && !previewVm.isMarkdown
                            ? "Cascadia Mono"
                            : "Microsoft YaHei UI"
                        renderType: Text.NativeRendering
                        leftPadding: 0
                        rightPadding: 0
                        topPadding: 0
                        bottomPadding: 0
                    }
                }
            }
        }
    }
}
