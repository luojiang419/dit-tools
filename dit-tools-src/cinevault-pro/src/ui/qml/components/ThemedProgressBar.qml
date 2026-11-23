import QtQuick
import QtQuick.Controls
import CineVault

ProgressBar {
    id: control
    property real indeterminatePhase: 0

    NumberAnimation {
        target: control
        property: "indeterminatePhase"
        from: 0
        to: 1
        duration: 900
        loops: Animation.Infinite
        running: control.indeterminate && control.visible
        easing.type: Easing.InOutSine
    }

    onIndeterminateChanged: if (!control.indeterminate) control.indeterminatePhase = 0

    background: Rectangle {
        implicitHeight: 8
        radius: 4
        color: Theme.panel
        border.width: 1
        border.color: Theme.line
    }

    contentItem: Item {
        implicitHeight: 8
        clip: true

        Rectangle {
            width: control.indeterminate ? Math.max(24, parent.width * 0.32) : control.visualPosition * parent.width
            x: control.indeterminate
                ? -width + control.indeterminatePhase * (parent.width + width)
                : 0
            height: parent.height
            radius: height / 2
            color: Theme.blue
        }
    }
}
