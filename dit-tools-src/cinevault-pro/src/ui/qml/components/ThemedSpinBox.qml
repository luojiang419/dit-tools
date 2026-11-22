import QtQuick
import QtQuick.Controls
import CineVault

SpinBox {
    id: control

    property int indicatorWidth: 40

    editable: true
    hoverEnabled: true
    implicitWidth: 132
    implicitHeight: 42
    font.pixelSize: 15

    contentItem: TextInput {
        z: 2
        text: control.textFromValue(control.value, control.locale)
        font: control.font
        color: control.enabled ? Theme.text : Theme.weak
        selectionColor: Theme.blue
        selectedTextColor: Theme.primaryText
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        leftPadding: control.mirrored ? control.indicatorWidth + 6 : 8
        rightPadding: control.mirrored ? 8 : control.indicatorWidth + 6
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
    }

    up.indicator: Rectangle {
        x: control.mirrored ? 0 : control.width - width
        y: 1
        width: control.indicatorWidth
        height: Math.floor((control.height - 2) / 2)
        radius: 6
        color: control.up.pressed ? Theme.buttonPressed : "transparent"

        Text {
            anchors.centerIn: parent
            text: "+"
            color: control.enabled ? Theme.muted : Theme.weak
            font.pixelSize: Math.max(14, control.font.pixelSize)
        }
    }

    down.indicator: Rectangle {
        x: control.mirrored ? 0 : control.width - width
        y: Math.ceil(control.height / 2)
        width: control.indicatorWidth
        height: Math.floor((control.height - 2) / 2)
        radius: 6
        color: control.down.pressed ? Theme.buttonPressed : "transparent"

        Text {
            anchors.centerIn: parent
            text: "-"
            color: control.enabled ? Theme.muted : Theme.weak
            font.pixelSize: Math.max(14, control.font.pixelSize)
        }
    }

    background: Rectangle {
        radius: 8
        color: !control.enabled ? Theme.panel2 : (control.hovered ? Theme.inputHover : Theme.inputBg)
        border.width: 1
        border.color: control.activeFocus ? Theme.blue : Theme.line
    }
}
