import QtQuick
import QtQuick.Controls
import CineVault

ScrollBar {
    id: control

    policy: ScrollBar.AsNeeded
    minimumSize: 0.08
    padding: 3

    background: Rectangle {
        implicitWidth: control.orientation === Qt.Vertical ? 12 : 100
        implicitHeight: control.orientation === Qt.Vertical ? 100 : 12
        radius: control.orientation === Qt.Vertical ? width / 2 : height / 2
        color: control.size < 1.0
               ? Qt.rgba(Theme.line.r, Theme.line.g, Theme.line.b, control.hovered || control.pressed ? 0.34 : 0.18)
               : "transparent"
    }

    contentItem: Rectangle {
        implicitWidth: control.orientation === Qt.Vertical ? 10 : 100
        implicitHeight: control.orientation === Qt.Vertical ? 100 : 10
        radius: control.orientation === Qt.Vertical ? width / 2 : height / 2
        color: control.pressed
               ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.72)
               : Qt.rgba(Theme.muted.r, Theme.muted.g, Theme.muted.b, control.hovered ? 0.66 : 0.44)
    }
}
