import QtQuick
import QtQuick.Controls
import CineVault

Menu {
    id: control

    padding: 4
    implicitWidth: Math.max(184, contentItem ? contentItem.implicitWidth + leftPadding + rightPadding : 184)
    implicitHeight: Math.max(12, contentItem ? contentItem.implicitHeight + topPadding + bottomPadding : 12)

    background: Rectangle {
        radius: 8
        color: Theme.popupBg
        border.width: 1
        border.color: Theme.line
    }
}
