import QtQuick
import QtQuick.Controls
import CineVault

Button {
    id: control

    property bool primary: false
    property bool danger: false
    property int textPixelSize: 13

    implicitWidth: Math.max(72, actionText.implicitWidth + 24)
    implicitHeight: 36
    padding: 0

    background: Rectangle {
        radius: 10
        color: !control.enabled
            ? Theme.panel2
            : (control.down
                ? (control.danger ? Theme.red : (control.primary ? Theme.primaryPressed : Theme.buttonPressed))
                : (control.hovered
                    ? (control.danger ? Theme.red : (control.primary ? Theme.primaryHover : Theme.buttonHover))
                    : (control.danger ? Theme.red : (control.primary ? Theme.primaryBg : Theme.buttonBg))))
        border.width: 1
        border.color: control.danger ? Theme.red : (control.primary ? Theme.selectedLine : Theme.line)
    }

    contentItem: Text {
        id: actionText
        text: control.text
        color: control.enabled ? ((control.primary || control.danger) ? Theme.primaryText : Theme.text) : Theme.weak
        font.pixelSize: control.textPixelSize
        font.weight: Font.DemiBold
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
