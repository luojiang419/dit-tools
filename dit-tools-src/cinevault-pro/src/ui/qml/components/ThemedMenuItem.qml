import QtQuick
import QtQuick.Controls
import CineVault

MenuItem {
    id: control

    implicitWidth: Math.max(176, labelMetrics.width + 32)
    implicitHeight: 34

    TextMetrics {
        id: labelMetrics
        text: control.text
        font: control.font
    }

    contentItem: Text {
        leftPadding: 8
        rightPadding: 8
        width: control.availableWidth
        text: control.text
        color: control.enabled ? Theme.text : Theme.weak
        font: control.font
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: 6
        color: control.highlighted ? Theme.popupHover : "transparent"
    }
}
