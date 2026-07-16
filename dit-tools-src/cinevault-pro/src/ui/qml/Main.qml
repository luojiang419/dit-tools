import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import CineVault

ApplicationWindow {
    id: root

    property var shellViewModel: shellVm
    property var projectLibraryViewModel: projectLibraryVm
    property var sourceRailViewModel: sourceRailVm
    property var libraryViewModel: libraryWorkspaceVm
    property var materialCenterViewModel: materialCenterVm
    property var documentPreviewViewModel: documentPreviewVm
    property var inspectorViewModel: inspectorVm
    property var jobTimelineViewModel: jobTimelineVm
    property var reportViewModel: reportWorkspaceVm
    property var settingsViewModel: settingsVm
    property var feedbackViewModel: feedbackVm
    readonly property bool hasOpenProject: root.shellViewModel && root.shellViewModel.projectEntered
    readonly property bool isProjectLibraryWorkspace: root.shellViewModel && root.shellViewModel.currentWorkspace === root.shellViewModel.projectLibraryWorkspaceId
    readonly property bool isFeedbackWorkspace: root.shellViewModel && root.shellViewModel.currentWorkspace === root.shellViewModel.feedbackWorkspaceId
    readonly property bool isJobsWorkspace: root.shellViewModel && root.shellViewModel.currentWorkspace === root.shellViewModel.jobsWorkspaceId
    property bool projectLibraryWorkspaceLoaded: true
    property bool libraryWorkspaceLoaded: false
    property bool materialCenterWorkspaceLoaded: false
    property bool reportWorkspaceLoaded: false
    property bool jobsWorkspaceLoaded: false
    property bool feedbackWorkspaceLoaded: false
    property bool sourceRailCollapsed: false
    property bool forceExitRequested: false

    visible: !(quickSearchController && quickSearchController.startHidden)
    width: 1600
    height: 980
    minimumWidth: 1280
    minimumHeight: 760
    title: "影资管家"
    color: Theme.bg

    function applyWindowTheme() {
        if (windowThemeController) {
            windowThemeController.apply(root, Theme.topBar, Theme.text, Theme.isDark)
        }
    }

    function restoreToForeground() {
        if (quickSearchController
                && typeof quickSearchController.restoreMainWindow === "function"
                && quickSearchController.restoreMainWindow(root)) {
            return
        }
        root.showNormal()
        root.raise()
        root.requestActivate()
    }

    function minimizeToTray() {
        closeConfirmDialog.close()
        if (quickSearchController && quickSearchController.trayAvailable) {
            root.hide()
        } else {
            root.showMinimized()
        }
    }

    function requestApplicationExit() {
        root.forceExitRequested = true
        closeConfirmDialog.close()
        Qt.quit()
    }

    function rememberWorkspace(workspace) {
        if (!root.shellViewModel) {
            return
        }
        if (workspace === root.shellViewModel.projectLibraryWorkspaceId) {
            root.projectLibraryWorkspaceLoaded = true
        } else if (workspace === root.shellViewModel.materialCenterWorkspaceId) {
            root.materialCenterWorkspaceLoaded = true
        } else if (workspace === root.shellViewModel.reportWorkspaceId) {
            root.reportWorkspaceLoaded = true
        } else if (workspace === root.shellViewModel.jobsWorkspaceId) {
            root.jobsWorkspaceLoaded = true
        } else if (workspace === root.shellViewModel.feedbackWorkspaceId) {
            root.feedbackWorkspaceLoaded = true
        } else {
            root.libraryWorkspaceLoaded = true
        }
    }

    Component.onCompleted: {
        root.rememberWorkspace(root.shellViewModel.currentWorkspace)
        Qt.callLater(root.applyWindowTheme)
        Qt.callLater(function() {
            if (root.settingsViewModel) {
                root.settingsViewModel.beginStartupUpdateFlow()
            }
        })
    }
    onVisibleChanged: if (visible) Qt.callLater(root.applyWindowTheme)
    onClosing: function(close) {
        if (root.forceExitRequested) {
            close.accepted = true
            return
        }
        var behavior = root.settingsViewModel ? root.settingsViewModel.closeButtonBehavior : 0
        if (behavior === 2) {
            close.accepted = true
            root.requestApplicationExit()
            return
        }
        close.accepted = false
        if (behavior === 1) {
            root.minimizeToTray()
        } else if (!closeConfirmDialog.opened) {
            closeConfirmDialog.open()
        }
    }

    Binding {
        target: Theme
        property: "mode"
        value: root.settingsViewModel ? root.settingsViewModel.themeMode : Theme.modeSystem
        restoreMode: Binding.RestoreBinding
    }

    Connections {
        target: Theme
        function onIsDarkChanged() { root.applyWindowTheme() }
        function onTopBarChanged() { root.applyWindowTheme() }
        function onTextChanged() { root.applyWindowTheme() }
    }

    Connections {
        target: root.shellViewModel
        function onCurrentWorkspaceChanged() {
            root.rememberWorkspace(root.shellViewModel.currentWorkspace)
        }
    }

    Connections {
        target: quickSearchController
        function onShowMainWindowRequested() {
            root.restoreToForeground()
        }
    }

    Shortcut {
        sequence: "Ctrl+K"
        context: Qt.ApplicationShortcut
        onActivated: {
            root.restoreToForeground()
            topCommandBar.focusSearch()
        }
    }

    Dialog {
        id: closeConfirmDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 560
        height: 260
        padding: 24
        closePolicy: Popup.CloseOnEscape

        background: Rectangle {
            radius: 18
            color: Theme.panel
            border.width: 1
            border.color: Theme.line
        }

        contentItem: ColumnLayout {
            spacing: 14

            Text {
                text: "关闭影资管家"
                color: Theme.text
                font.pixelSize: 22
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                text: "请选择本次关闭主窗口后的操作。默认行为可在设置页的“外观”区域修改。"
                color: Theme.muted
                font.pixelSize: 14
                wrapMode: Text.Wrap
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                ActionButton {
                    Layout.preferredWidth: 142
                    Layout.preferredHeight: 42
                    text: "最小化到托盘"
                    primary: true
                    onClicked: root.minimizeToTray()
                }

                Item { Layout.fillWidth: true }

                ActionButton {
                    Layout.preferredWidth: 86
                    Layout.preferredHeight: 42
                    text: "取消"
                    onClicked: closeConfirmDialog.reject()
                }

                ActionButton {
                    Layout.preferredWidth: 110
                    Layout.preferredHeight: 42
                    text: "退出软件"
                    danger: true
                    onClicked: root.requestApplicationExit()
                }
            }
        }
    }

    FolderDialog {
        id: sourceFolderDialog
        title: "选择素材源目录"
        onAccepted: {
            if (root.shellViewModel.importSourceDirectory(selectedFolder)) {
                sourcePathDialog.close()
            }
        }
    }

    Dialog {
        id: sourcePathDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 640
        height: 520
        padding: 24
        closePolicy: Popup.CloseOnEscape

        function submitPath() {
            if (root.shellViewModel && root.shellViewModel.importSourcePath(sourcePathField.text)) {
                sourcePathField.text = ""
                sourcePathDialog.close()
            }
        }

        onOpened: {
            sourcePathField.text = ""
            if (root.shellViewModel) root.shellViewModel.refreshStorageVolumes()
            sourcePathField.forceActiveFocus()
        }
        onRejected: if (root.shellViewModel) root.shellViewModel.cancelAddSourceDirectory()

        background: Rectangle {
            radius: 18
            color: Theme.panel
            border.width: 1
            border.color: Theme.line
        }

        contentItem: ColumnLayout {
            spacing: 14

            Text {
                text: "添加素材源"
                color: Theme.text
                font.pixelSize: 22
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                text: "可添加整个磁盘卷进行全盘索引，也可浏览本地文件夹或输入 UNC 网络共享路径。请确保当前 Windows 账户有读取权限。"
                color: Theme.muted
                font.pixelSize: 14
                wrapMode: Text.Wrap
            }

            ThemedTextField {
                id: sourcePathField
                objectName: "sourcePathField"
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                placeholderText: "例如：\\\\服务器\\共享\\素材"
                selectByMouse: true
                Keys.onReturnPressed: sourcePathDialog.submitPath()
            }

            Text {
                text: "可用磁盘卷 · 点击后递归索引卷内全部可读文件"
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 148
                radius: 10
                color: Theme.panel2
                border.width: 1
                border.color: Theme.line

                ListView {
                    id: storageVolumeList
                    anchors.fill: parent
                    anchors.margins: 6
                    clip: true
                    spacing: 4
                    model: root.shellViewModel ? root.shellViewModel.storageVolumes : []

                    delegate: Rectangle {
                        required property var modelData
                        width: storageVolumeList.width
                        height: 62
                        radius: 8
                        color: Theme.panel

                        Column {
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.right: addVolumeButton.left
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3

                            Text {
                                width: parent.width
                                text: modelData.label
                                color: Theme.text
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                elide: Text.ElideMiddle
                            }

                            Text {
                                width: parent.width
                                text: modelData.driveType + " · " + modelData.capacityText
                                      + (modelData.fileSystem.length > 0 ? " · " + modelData.fileSystem : "")
                                color: Theme.muted
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }

                        ActionButton {
                            id: addVolumeButton
                            width: 104
                            height: 38
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: "添加全盘"
                            primary: true
                            onClicked: {
                                if (root.shellViewModel.importStorageVolume(modelData.rootPath)) {
                                    sourcePathDialog.close()
                                }
                            }
                        }
                    }

                    ScrollBar.vertical: ThemedScrollBar { }
                }

                Text {
                    anchors.centerIn: parent
                    visible: storageVolumeList.count === 0
                    text: "未发现可用磁盘卷"
                    color: Theme.muted
                    font.pixelSize: 13
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                ActionButton {
                    Layout.preferredWidth: 126
                    Layout.preferredHeight: 42
                    text: "浏览文件夹"
                    onClicked: sourceFolderDialog.open()
                }

                Item { Layout.fillWidth: true }

                ActionButton {
                    Layout.preferredWidth: 86
                    Layout.preferredHeight: 42
                    text: "取消"
                    onClicked: sourcePathDialog.reject()
                }

                ActionButton {
                    Layout.preferredWidth: 110
                    Layout.preferredHeight: 42
                    text: "添加路径"
                    primary: true
                    enabled: sourcePathField.text.trim().length > 0
                    onClicked: sourcePathDialog.submitPath()
                }
            }
        }
    }

    Connections {
        target: root.shellViewModel
        function onAddSourceDirectoryRequested() {
            sourcePathDialog.open()
        }
        function onOpenSettingsRequested() {
            settingsPage.openPage()
        }
    }

    SettingsPage {
        id: settingsPage
        anchors.fill: parent
        z: 900
        viewModel: root.settingsViewModel
    }

    OnboardingWizard {
        id: onboardingWizard
        shellVm: root.shellViewModel
        settingsVm: root.settingsViewModel
        z: 850
    }

    Item {
        id: appContentLayer

        anchors.fill: parent
        visible: !settingsPage.opened

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            TopCommandBar {
                id: topCommandBar
                Layout.fillWidth: true
                shellVm: root.shellViewModel
                onGuideRequested: onboardingWizard.restart()
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                SourceRail {
                    Layout.preferredWidth: root.shellViewModel.currentWorkspace === root.shellViewModel.materialCenterWorkspaceId
                        || root.isJobsWorkspace
                        || root.isFeedbackWorkspace
                        || root.isProjectLibraryWorkspace
                        || !root.hasOpenProject
                        ? 0
                        : (root.sourceRailCollapsed ? 56 : 270)
                    Layout.fillHeight: true
                    visible: root.shellViewModel.currentWorkspace !== root.shellViewModel.materialCenterWorkspaceId
                        && !root.isJobsWorkspace
                        && !root.isFeedbackWorkspace
                        && !root.isProjectLibraryWorkspace
                        && root.hasOpenProject
                    shellVm: root.shellViewModel
                    viewModel: root.sourceRailViewModel
                    collapsed: root.sourceRailCollapsed
                    onCollapseRequested: function(nextCollapsed) {
                        root.sourceRailCollapsed = nextCollapsed
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Loader {
                        anchors.fill: parent
                        active: root.projectLibraryWorkspaceLoaded
                        visible: root.shellViewModel.currentWorkspace === root.shellViewModel.projectLibraryWorkspaceId
                        asynchronous: true
                        sourceComponent: projectLibraryWorkspaceComponent
                    }

                    Loader {
                        anchors.fill: parent
                        active: root.libraryWorkspaceLoaded
                        visible: root.shellViewModel.currentWorkspace !== root.shellViewModel.projectLibraryWorkspaceId
                            && root.shellViewModel.currentWorkspace !== root.shellViewModel.materialCenterWorkspaceId
                            && root.shellViewModel.currentWorkspace !== root.shellViewModel.reportWorkspaceId
                            && root.shellViewModel.currentWorkspace !== root.shellViewModel.jobsWorkspaceId
                            && root.shellViewModel.currentWorkspace !== root.shellViewModel.feedbackWorkspaceId
                        asynchronous: true
                        sourceComponent: libraryWorkspaceComponent
                    }

                    Loader {
                        anchors.fill: parent
                        active: root.materialCenterWorkspaceLoaded
                        visible: root.shellViewModel.currentWorkspace === root.shellViewModel.materialCenterWorkspaceId
                        asynchronous: true
                        sourceComponent: materialCenterWorkspaceComponent
                    }

                    Loader {
                        anchors.fill: parent
                        active: root.reportWorkspaceLoaded
                        visible: root.shellViewModel.currentWorkspace === root.shellViewModel.reportWorkspaceId
                        asynchronous: true
                        sourceComponent: reportWorkspaceComponent
                    }

                    Loader {
                        anchors.fill: parent
                        active: root.jobsWorkspaceLoaded
                        visible: root.shellViewModel.currentWorkspace === root.shellViewModel.jobsWorkspaceId
                        asynchronous: true
                        sourceComponent: jobsWorkspaceComponent
                    }

                    Loader {
                        anchors.fill: parent
                        active: root.feedbackWorkspaceLoaded
                        visible: root.shellViewModel.currentWorkspace === root.shellViewModel.feedbackWorkspaceId
                        asynchronous: true
                        sourceComponent: feedbackWorkspaceComponent
                    }
                }

                InspectorPane {
                    Layout.preferredWidth: root.shellViewModel.currentWorkspace === root.shellViewModel.reportWorkspaceId
                        || root.shellViewModel.currentWorkspace === root.shellViewModel.materialCenterWorkspaceId
                        || root.isJobsWorkspace
                        || root.isFeedbackWorkspace
                        || root.isProjectLibraryWorkspace
                        || !root.hasOpenProject
                        ? 0
                        : 330
                    Layout.fillHeight: true
                    visible: root.shellViewModel.currentWorkspace !== root.shellViewModel.reportWorkspaceId
                        && root.shellViewModel.currentWorkspace !== root.shellViewModel.materialCenterWorkspaceId
                        && !root.isJobsWorkspace
                        && !root.isFeedbackWorkspace
                        && !root.isProjectLibraryWorkspace
                        && root.hasOpenProject
                    viewModel: root.inspectorViewModel
                    mediaViewModel: root.libraryViewModel
                }

                JobProgressInspectorPane {
                    Layout.preferredWidth: root.isJobsWorkspace && root.hasOpenProject ? 330 : 0
                    Layout.fillHeight: true
                    visible: root.isJobsWorkspace && root.hasOpenProject
                    viewModel: root.jobTimelineViewModel
                }
            }

            JobTimelineBar {
                Layout.fillWidth: true
                Layout.preferredHeight: root.isProjectLibraryWorkspace
                    || root.isFeedbackWorkspace
                    ? 0
                    : implicitHeight
                visible: !root.isProjectLibraryWorkspace
                    && !root.isFeedbackWorkspace
                viewModel: root.jobTimelineViewModel
                libraryViewModel: root.libraryViewModel
            }
        }
    }

    QuickSearchWindow {
        controller: quickSearchController
        materialCenterViewModel: root.materialCenterViewModel
        shellViewModel: root.shellViewModel
        mainWindow: root
    }

    Component {
        id: projectLibraryWorkspaceComponent
        ProjectLibraryWorkspace {
            viewModel: root.projectLibraryViewModel
        }
    }

    Component {
        id: libraryWorkspaceComponent
        LibraryWorkspace {
            viewModel: root.libraryViewModel
            shellVm: root.shellViewModel
            documentPreviewVm: root.documentPreviewViewModel
        }
    }

    Component {
        id: reportWorkspaceComponent
        ReportWorkspace {
            viewModel: root.reportViewModel
        }
    }

    Component {
        id: materialCenterWorkspaceComponent
        MaterialCenterWorkspace {
            viewModel: root.materialCenterViewModel
            shellVm: root.shellViewModel
        }
    }

    Component {
        id: jobsWorkspaceComponent
        JobsWorkspace {
            viewModel: root.jobTimelineViewModel
        }
    }

    Component {
        id: feedbackWorkspaceComponent
        FeedbackWorkspace {
            viewModel: root.feedbackViewModel
            documentPreviewVm: root.documentPreviewViewModel
            workspaceActive: root.isFeedbackWorkspace
        }
    }
}
