import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CineVault

Rectangle {
    property var shellVm
    signal guideRequested()

    function focusSearch() {
        if (globalSearchField.visible) {
            globalSearchField.forceActiveFocus()
            globalSearchField.selectAll()
        }
    }

    color: Theme.topBar
    border.width: 1
    border.color: Theme.line
    implicitHeight: 64

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        spacing: 8

        readonly property bool isFeedbackWorkspace: shellVm && shellVm.currentWorkspace === shellVm.feedbackWorkspaceId

        Text {
            text: "影资管家"
            color: Theme.text
            font.pixelSize: 22
            font.weight: Font.Black
            Layout.preferredWidth: 92
            elide: Text.ElideRight
        }

        Rectangle {
            color: Theme.panel2
            radius: 12
            border.width: 1
            border.color: Theme.line
            Layout.preferredWidth: 170
            Layout.minimumWidth: 130
            Layout.preferredHeight: 40

            Text {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                text: shellVm.projectName
                color: Theme.muted
                font.pixelSize: 13
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        Repeater {
            model: shellVm ? shellVm.workspaceTabs : []

            delegate: Button {
                readonly property bool tabEnabled: modelData.enabled === undefined || modelData.enabled
                readonly property int badgeCount: modelData.badgeCount === undefined ? 0 : Number(modelData.badgeCount)

                Layout.preferredWidth: modelData.buttonWidth
                Layout.preferredHeight: 36
                text: modelData.label
                flat: true
                enabled: tabEnabled
                opacity: tabEnabled ? 1.0 : 0.44
                onClicked: if (tabEnabled) shellVm.currentWorkspace = modelData.value
                background: Rectangle {
                    radius: 18
                    color: shellVm.currentWorkspace === modelData.value ? Theme.selectedBg : "transparent"
                    border.width: shellVm.currentWorkspace === modelData.value ? 1 : 0
                    border.color: Theme.selectedLine

                    Rectangle {
                        visible: badgeCount > 0
                        width: badgeText.implicitWidth + 10
                        height: 18
                        radius: 9
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.rightMargin: 4
                        anchors.topMargin: -4
                        color: Theme.red

                        Text {
                            id: badgeText
                            anchors.centerIn: parent
                            text: badgeCount > 99 ? "99+" : badgeCount
                            color: Theme.primaryText
                            font.pixelSize: 10
                            font.weight: Font.Bold
                        }
                    }
                }
                contentItem: Text {
                    text: parent.text
                    color: parent.enabled
                           ? (shellVm.currentWorkspace === modelData.value ? Theme.text : Theme.muted)
                           : Theme.weak
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        Item { Layout.fillWidth: true }

        ThemedTextField {
            id: globalSearchField
            Layout.preferredWidth: visible ? 220 : 0
            Layout.minimumWidth: visible ? 160 : 0
            Layout.maximumWidth: visible ? 260 : 0
            visible: !parent.isFeedbackWorkspace
            Layout.preferredHeight: visible ? implicitHeight : 0
            placeholderText: shellVm && shellVm.currentWorkspace === shellVm.projectLibraryWorkspaceId ? "搜索项目..." : "搜索素材..."
            text: shellVm.globalSearchText
            onTextChanged: shellVm.globalSearchText = text
        }

        ActionButton {
            Layout.preferredWidth: 64
            Layout.preferredHeight: 36
            text: "向导"
            onClicked: guideRequested()
        }

        ActionButton {
            Layout.preferredWidth: 64
            Layout.preferredHeight: 36
            text: "设置"
            onClicked: shellVm.openSettings()
        }
    }
}
