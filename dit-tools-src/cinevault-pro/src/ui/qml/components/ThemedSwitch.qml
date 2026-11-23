import QtQuick
import QtQuick.Controls
import CineVault

Switch {
    id: control

    spacing: 10
    font.pixelSize: 15
    palette.text: control.enabled ? Theme.text : Theme.weak
    palette.windowText: control.enabled ? Theme.text : Theme.weak
    palette.buttonText: control.enabled ? Theme.text : Theme.weak

    indicator: Rectangle {
        implicitWidth: 46
        implicitHeight: 26
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: height / 2
        color: control.checked ? Theme.primaryBg : Theme.inputPressed
        border.width: 1
        border.color: control.checked ? Theme.primaryHover : Theme.line
        opacity: control.enabled ? 1.0 : 0.58

        Rectangle {
            width: 20
            height: 20
            x: control.checked ? parent.width - width - 3 : 3
            y: 3
            radius: width / 2
            color: control.checked ? Theme.primaryText : Theme.text

            Behavior on x {
                NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
            }
        }
    }

    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        color: control.enabled ? Theme.text : Theme.weak
        font: control.font
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
    }
}
