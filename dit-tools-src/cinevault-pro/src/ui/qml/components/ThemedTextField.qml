import QtQuick
import QtQuick.Controls
import CineVault

TextField {
    id: control

    color: enabled ? Theme.text : Theme.weak
    placeholderTextColor: Theme.weak
    selectionColor: Theme.blue
    selectedTextColor: Theme.primaryText
    selectByMouse: true

    background: Rectangle {
        radius: 8
        color: !control.enabled ? Theme.panel2 : (control.hovered ? Theme.inputHover : Theme.inputBg)
        border.width: 1
        border.color: control.activeFocus ? Theme.blue : Theme.line
    }
}
