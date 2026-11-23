import QtQuick
import QtQuick.Controls
import CineVault

Slider {
    id: control

    hoverEnabled: true

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 5
        radius: 3
        color: Theme.panel
        border.width: 1
        border.color: Theme.line

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: Theme.blue
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.pressed ? 18 : 16
        height: control.pressed ? 18 : 16
        radius: width / 2
        color: control.enabled ? Theme.card : Theme.panel2
        border.width: 2
        border.color: control.enabled ? Theme.blue : Theme.weak
    }
}
