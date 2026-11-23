import QtQuick
import QtQuick.Controls
import CineVault

ComboBox {
    id: control

    hoverEnabled: true
    spacing: 8
    implicitHeight: 42
    font.pixelSize: 15

    function optionText(rowData, rowModel) {
        var role = control.textRole
        if (role && role.length > 0) {
            if (rowModel && rowModel[role] !== undefined && rowModel[role] !== null) {
                return rowModel[role]
            }
            if (rowData && typeof rowData === "object" && rowData[role] !== undefined && rowData[role] !== null) {
                return rowData[role]
            }
        }

        if (rowData === undefined || rowData === null) {
            return ""
        }
        return rowData.toString()
    }

    contentItem: Text {
        leftPadding: 12
        rightPadding: 34
        text: control.displayText
        color: control.enabled ? Theme.text : Theme.weak
        font: control.font
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    indicator: Text {
        x: control.width - width - 12
        y: (control.height - height) / 2
        text: "▾"
        color: control.enabled ? Theme.muted : Theme.weak
        font.pixelSize: 16
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
    }

    background: Rectangle {
        radius: 8
        color: !control.enabled ? Theme.panel2 : (control.down ? Theme.inputPressed : (control.hovered ? Theme.inputHover : Theme.inputBg))
        border.width: 1
        border.color: control.activeFocus ? Theme.blue : Theme.line
    }

    delegate: ItemDelegate {
        id: option

        width: control.width
        height: 42
        highlighted: control.highlightedIndex === index

        contentItem: Text {
            leftPadding: 12
            rightPadding: 12
            text: control.optionText(modelData, model)
            color: control.enabled ? Theme.text : Theme.weak
            font: control.font
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            radius: 6
            color: option.highlighted ? Theme.popupHover : "transparent"
        }
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2, 280)
        padding: 1

        contentItem: ListView {
            id: popupList

            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ThemedScrollBar { policy: ScrollBar.AsNeeded }

            MiddleDragScrollHandler {
                flickable: popupList
            }
        }

        background: Rectangle {
            radius: 8
            color: Theme.popupBg
            border.width: 1
            border.color: Theme.line
        }
    }
}
