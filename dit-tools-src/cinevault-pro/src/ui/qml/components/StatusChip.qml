import QtQuick
import QtQuick.Controls
import CineVault

Rectangle {
    property string label: ""
    property color tint: Theme.blue

    implicitWidth: Math.max(64, chipText.implicitWidth + 20)
    implicitHeight: 28
    radius: 14
    color: Qt.rgba(tint.r, tint.g, tint.b, 0.16)
    border.width: 1
    border.color: Qt.rgba(tint.r, tint.g, tint.b, 0.40)

    Text {
        id: chipText
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        text: parent.label
        color: Theme.text
        font.pixelSize: 12
        font.weight: Font.Medium
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
