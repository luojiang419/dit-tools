import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CineVault

Item {
    id: root

    property var viewModel
    property var draftVisionApiConfigs: []
    property string draftActiveVisionApiConfigId: ""
    property bool draftSearchAssistantEnabled: true
    property int draftSearchAssistantAutoUnloadMinutes: 30
    property bool draftQuickSearchEnabled: true
    property string draftQuickSearchShortcut: "Alt+Space"
    property bool draftStartAtLogin: false
    property int draftCloseButtonBehavior: 0
    property int draftAnalysisMode: 0
    property int draftFrameInterval: 10
    property int draftVideoFrameExtractionStrategy: 1
    property real draftVideoFrameIntervalSeconds: 1.0
    property real draftVideoSceneThreshold: 0.3
    property real draftVideoMinimumSharpness: 0.08
    property int draftThumbnailFrameIndex: 3
    property int draftContactSheetFrameCount: 24
    property int draftAnalysisTimeoutSec: 60
    property bool draftDocumentAutoAnalysisEnabled: false
    property bool draftPhotoshopAutoAnalysisEnabled: false
    property int draftThemeMode: Theme.modeSystem
    property int draftUpdatePolicy: 0
    property int draftUpdateDownloadMode: 0
    property string draftUpdateManualProxyUrl: ""
    property int bodyFontSize: 15
    property int sectionTitleSize: 20
    property int controlHeight: 42
    property int sectionPadding: 20
    property int formLabelWidth: 116
    property string customDimensionMessage: ""

    readonly property bool opened: visible

    visible: false
    enabled: visible
    focus: visible

    function reloadDrafts() {
        if (viewModel) {
            viewModel.refresh()
            draftVisionApiConfigs = copyVisionApiConfigs(viewModel.visionApiConfigs)
            draftActiveVisionApiConfigId = viewModel.activeVisionApiConfigId
            draftSearchAssistantEnabled = viewModel.searchAssistantEnabled
            draftSearchAssistantAutoUnloadMinutes = viewModel.searchAssistantAutoUnloadMinutes
            draftQuickSearchEnabled = viewModel.quickSearchEnabled
            draftQuickSearchShortcut = viewModel.quickSearchShortcut
            draftStartAtLogin = viewModel.startAtLogin
            draftCloseButtonBehavior = viewModel.closeButtonBehavior
            draftAnalysisMode = viewModel.analysisMode
            draftFrameInterval = viewModel.frameInterval
            draftVideoFrameExtractionStrategy = viewModel.videoFrameExtractionStrategy
            draftVideoFrameIntervalSeconds = viewModel.videoFrameIntervalSeconds
            draftVideoSceneThreshold = viewModel.videoSceneThreshold
            draftVideoMinimumSharpness = viewModel.videoMinimumSharpness
            draftThumbnailFrameIndex = viewModel.thumbnailFrameIndex
            draftContactSheetFrameCount = viewModel.contactSheetFrameCount
            draftAnalysisTimeoutSec = viewModel.analysisTimeoutSec
            draftDocumentAutoAnalysisEnabled = viewModel.documentAutoAnalysisEnabled
            draftPhotoshopAutoAnalysisEnabled = viewModel.photoshopAutoAnalysisEnabled
            draftThemeMode = viewModel.themeMode
            draftUpdatePolicy = viewModel.updatePolicy
            draftUpdateDownloadMode = viewModel.updateDownloadMode
            draftUpdateManualProxyUrl = viewModel.updateManualProxyUrl
        }
    }

    function copyVisionApiConfigs(configs) {
        var copies = []
        for (var index = 0; index < configs.length; ++index) {
            var config = configs[index]
            copies.push({
                id: config.id,
                name: config.name,
                baseUrl: config.baseUrl,
                apiKey: config.apiKey,
                model: config.model
            })
        }
        return copies
    }

    function activeVisionApiConfig() {
        for (var index = 0; index < draftVisionApiConfigs.length; ++index) {
            if (draftVisionApiConfigs[index].id === draftActiveVisionApiConfigId) {
                return draftVisionApiConfigs[index]
            }
        }
        return draftVisionApiConfigs.length > 0 ? draftVisionApiConfigs[0] : null
    }

    function selectVisionApiConfig(configId) {
        draftActiveVisionApiConfigId = configId
    }

    function saveVisionApiConfig(config) {
        var configs = copyVisionApiConfigs(draftVisionApiConfigs)
        var updated = false
        for (var index = 0; index < configs.length; ++index) {
            if (configs[index].id === config.id) {
                configs[index] = config
                updated = true
                break
            }
        }
        if (!updated) {
            configs.push(config)
            draftActiveVisionApiConfigId = config.id
        }
        draftVisionApiConfigs = configs
    }

    function removeVisionApiConfig(configId) {
        if (draftVisionApiConfigs.length <= 1) {
            return
        }
        var configs = []
        for (var index = 0; index < draftVisionApiConfigs.length; ++index) {
            if (draftVisionApiConfigs[index].id !== configId) {
                configs.push(draftVisionApiConfigs[index])
            }
        }
        draftVisionApiConfigs = configs
        if (draftActiveVisionApiConfigId === configId) {
            draftActiveVisionApiConfigId = configs[0].id
        }
    }

    function nextVisionApiConfigId() {
        return "vision-" + Date.now() + "-" + Math.floor(Math.random() * 1000000)
    }

    function openVisionApiConfigDialog(config) {
        visionConfigDialog.configId = config ? config.id : nextVisionApiConfigId()
        visionConfigDialog.nameValue = config ? config.name : ""
        visionConfigDialog.baseUrlValue = config ? config.baseUrl : ""
        visionConfigDialog.apiKeyValue = config ? config.apiKey : ""
        visionConfigDialog.modelValue = config ? config.model : "gpt-4.1-mini"
        visionConfigDialog.editing = config !== null
        visionConfigDialog.open()
    }

    function openPage() {
        reloadDrafts()
        if (settingsScroll.contentItem) {
            settingsScroll.contentItem.contentY = 0
        }
        visible = true
        forceActiveFocus()
    }

    function closePage() {
        visible = false
    }

    function openCustomDimensionDialog() {
        customDimensionMessage = ""
        customDimensionNameField.text = ""
        customDimensionDialog.open()
    }

    function submitCustomDimension() {
        if (!viewModel) {
            return
        }
        var name = customDimensionNameField.text.trim()
        if (viewModel.addCustomAnalysisDimension(name)) {
            customDimensionMessage = "已添加“" + name + "”，后续常规解析会自动执行该维度。"
            customDimensionNameField.text = ""
            customDimensionNameField.forceActiveFocus()
        } else {
            customDimensionMessage = viewModel.lastMessage
        }
    }

    Keys.onEscapePressed: function(event) {
        closePage()
        event.accepted = true
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            color: Theme.panel2
            border.width: 1
            border.color: Theme.line

            RowLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                ColumnLayout {
                    spacing: 4

                    Text {
                        text: "设置"
                        color: Theme.text
                        font.pixelSize: 26
                        font.weight: Font.Black
                    }

                    Text {
                        text: viewModel ? viewModel.currentVersionLabel : "当前版本：v0.0.0"
                        color: Theme.weak
                        font.pixelSize: 13
                    }

                    Text {
                        text: "软件更新、视觉解析、缩略图和解析图片配置"
                        color: Theme.muted
                        font.pixelSize: root.bodyFontSize
                    }
                }

                Item { Layout.fillWidth: true }

                ActionButton {
                    Layout.preferredWidth: 118
                    Layout.preferredHeight: root.controlHeight
                    text: viewModel && viewModel.updateBusy ? "检查中..." : "检查更新"
                    enabled: viewModel && !viewModel.updateBusy && root.draftUpdatePolicy !== 2
                    textPixelSize: root.bodyFontSize
                    onClicked: if (viewModel) {
                        viewModel.saveUpdateDownloadSettings(
                            root.draftUpdatePolicy,
                            root.draftUpdateDownloadMode,
                            root.draftUpdateManualProxyUrl)
                        viewModel.checkForUpdates()
                    }
                }

                ActionButton {
                    Layout.preferredWidth: 118
                    Layout.preferredHeight: root.controlHeight
                    text: "保存并应用"
                    primary: true
                    textPixelSize: root.bodyFontSize
                    onClicked: if (viewModel) {
                        viewModel.saveAndApply(
                            root.draftVisionApiConfigs,
                            root.draftActiveVisionApiConfigId,
                            root.draftSearchAssistantEnabled,
                            root.draftSearchAssistantAutoUnloadMinutes,
                            root.draftQuickSearchEnabled,
                            root.draftQuickSearchShortcut,
                            root.draftStartAtLogin,
                            root.draftCloseButtonBehavior,
                            root.draftAnalysisMode,
                            root.draftFrameInterval,
                            root.draftVideoFrameExtractionStrategy,
                            root.draftVideoFrameIntervalSeconds,
                            root.draftVideoSceneThreshold,
                            root.draftVideoMinimumSharpness,
                            root.draftThumbnailFrameIndex,
                            root.draftContactSheetFrameCount,
                            root.draftAnalysisTimeoutSec,
                            root.draftDocumentAutoAnalysisEnabled,
                            root.draftPhotoshopAutoAnalysisEnabled,
                            root.draftUpdatePolicy,
                            root.draftUpdateDownloadMode,
                            root.draftUpdateManualProxyUrl)
                    }
                }

                ActionButton {
                    Layout.preferredWidth: 78
                    Layout.preferredHeight: root.controlHeight
                    text: "返回"
                    textPixelSize: root.bodyFontSize
                    onClicked: root.closePage()
                }
            }
        }

        ScrollView {
            id: settingsScroll

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            contentHeight: settingsColumn.implicitHeight + 40

            ScrollBar.horizontal: ThemedScrollBar {
                policy: ScrollBar.AlwaysOff
            }

            ScrollBar.vertical: ThemedScrollBar {
                policy: ScrollBar.AsNeeded
            }

            MiddleDragScrollHandler {
                parent: settingsScroll.contentItem
                flickable: settingsScroll.contentItem
            }

            ColumnLayout {
                id: settingsColumn

                x: 20
                y: 20
                width: Math.max(0, settingsScroll.availableWidth - 40)
                spacing: 18

                Rectangle {
                    Layout.fillWidth: true
                    radius: 18
                    color: Theme.panel2
                    border.width: 1
                    border.color: Theme.line
                    implicitHeight: updateContent.implicitHeight + root.sectionPadding * 2

                    ColumnLayout {
                        id: updateContent
                        anchors.fill: parent
                        anchors.margins: root.sectionPadding
                        spacing: 14

                        Text {
                            text: "软件更新"
                            color: Theme.text
                            font.pixelSize: root.sectionTitleSize
                            font.weight: Font.DemiBold
                        }

                        GridLayout {
                            columns: 2
                            columnSpacing: 14
                            rowSpacing: 14
                            Layout.fillWidth: true

                            Text {
                                Layout.preferredWidth: root.formLabelWidth
                                Layout.alignment: Qt.AlignVCenter
                                text: "更新策略"
                                color: Theme.muted
                                font.pixelSize: root.bodyFontSize
                            }

                            ThemedComboBox {
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.controlHeight
                                font.pixelSize: root.bodyFontSize
                                model: [
                                    { label: "自动更新（检测并下载后询问）", value: 0 },
                                    { label: "手动更新（仅点击检查时联网）", value: 1 },
                                    { label: "禁止更新（不联网、不提示）", value: 2 }
                                ]
                                textRole: "label"
                                currentIndex: root.draftUpdatePolicy === 1 ? 1 : (root.draftUpdatePolicy === 2 ? 2 : 0)
                                onActivated: root.draftUpdatePolicy = model[index].value
                            }

                            Text {
                                Layout.preferredWidth: root.formLabelWidth
                                Layout.alignment: Qt.AlignVCenter
                                text: "更新网络"
                                color: Theme.muted
                                font.pixelSize: root.bodyFontSize
                            }

                            ThemedComboBox {
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.controlHeight
                                font.pixelSize: root.bodyFontSize
                                model: [
                                    { label: "自动检测代理", value: 0 },
                                    { label: "手动代理", value: 1 },
                                    { label: "直连下载", value: 2 }
                                ]
                                textRole: "label"
                                currentIndex: root.draftUpdateDownloadMode === 1 ? 1 : (root.draftUpdateDownloadMode === 2 ? 2 : 0)
                                onActivated: root.draftUpdateDownloadMode = model[index].value
                            }

                            Text {
                                visible: root.draftUpdateDownloadMode === 1
                                Layout.preferredWidth: root.formLabelWidth
                                Layout.alignment: Qt.AlignVCenter
                                text: "代理地址"
                                color: Theme.muted
                                font.pixelSize: root.bodyFontSize
                            }

                            ThemedTextField {
                                visible: root.draftUpdateDownloadMode === 1
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.controlHeight
                                font.pixelSize: root.bodyFontSize
                                text: root.draftUpdateManualProxyUrl
                                placeholderText: "http://127.0.0.1:7890"
                                onTextEdited: root.draftUpdateManualProxyUrl = text
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 18
                    color: Theme.panel2
                    border.width: 1
                    border.color: Theme.line
                    implicitHeight: quickSearchContent.implicitHeight + root.sectionPadding * 2

                    ColumnLayout {
                        id: quickSearchContent
                        anchors.fill: parent
                        anchors.margins: root.sectionPadding
                        spacing: 12

                        Text {
                            text: "快捷搜索"
                            color: Theme.text
                            font.pixelSize: root.sectionTitleSize
                            font.weight: Font.DemiBold
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "像 Flow Launcher 一样，从任何窗口拉起独立搜索框，直接使用自然语言搜索全部项目的文件夹、画面和素材。"
                            color: Theme.muted
                            font.pixelSize: root.bodyFontSize
                            wrapMode: Text.Wrap
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 14
                            rowSpacing: 12

                            Text {
                                Layout.preferredWidth: root.formLabelWidth
                                Layout.alignment: Qt.AlignVCenter
                                text: "全局唤起"
                                color: Theme.muted
                                font.pixelSize: root.bodyFontSize
                            }

                            ThemedSwitch {
                                checked: root.draftQuickSearchEnabled
                                text: checked ? "已启用" : "已关闭"
                                font.pixelSize: root.bodyFontSize
                                onToggled: root.draftQuickSearchEnabled = checked
                            }

                            Text {
                                Layout.preferredWidth: root.formLabelWidth
                                Layout.alignment: Qt.AlignVCenter
                                text: "快捷键"
                                color: root.draftQuickSearchEnabled ? Theme.muted : Theme.weak
                                font.pixelSize: root.bodyFontSize
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                enabled: root.draftQuickSearchEnabled
                                spacing: 10

                                ThemedTextField {
                                    id: quickSearchShortcutRecorder
                                    Layout.preferredWidth: 220
                                    Layout.preferredHeight: root.controlHeight
                                    readOnly: true
                                    selectByMouse: true
                                    text: root.draftQuickSearchShortcut
                                    placeholderText: "点击后按下组合键"
                                    Keys.onPressed: function(event) {
                                        if (!viewModel) {
                                            return
                                        }
                                        if (event.key === Qt.Key_Escape) {
                                            quickSearchShortcutRecorder.focus = false
                                            event.accepted = true
                                            return
                                        }
                                        var shortcut = viewModel.shortcutFromKeyEvent(event.key, event.modifiers)
                                        if (shortcut.length > 0) {
                                            root.draftQuickSearchShortcut = shortcut
                                        }
                                        event.accepted = true
                                    }
                                }

                                ActionButton {
                                    Layout.preferredWidth: 104
                                    Layout.preferredHeight: root.controlHeight
                                    text: "恢复默认"
                                    onClicked: root.draftQuickSearchShortcut = "Alt+Space"
                                }

                                Item { Layout.fillWidth: true }
                            }

                            Text {
                                Layout.preferredWidth: root.formLabelWidth
                                Layout.alignment: Qt.AlignVCenter
                                text: "开机启动"
                                color: Theme.muted
                                font.pixelSize: root.bodyFontSize
                            }

                            ThemedSwitch {
                                checked: root.draftStartAtLogin
                                text: checked ? "登录后在托盘运行" : "不自动启动"
                                font.pixelSize: root.bodyFontSize
                                onToggled: root.draftStartAtLogin = checked
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: viewModel ? viewModel.quickSearchStatusText : ""
                            color: text.indexOf("失败") >= 0 || text.indexOf("占用") >= 0 ? Theme.red : Theme.blue
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "保存后立即重新注册快捷键。关闭主窗口时程序保留在托盘；可从托盘菜单重新显示或彻底退出。"
                            color: Theme.weak
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 18
                    color: Theme.panel2
                    border.width: 1
                    border.color: Theme.line
                    implicitHeight: appearanceContent.implicitHeight + root.sectionPadding * 2

                    ColumnLayout {
                        id: appearanceContent
                        anchors.fill: parent
                        anchors.margins: root.sectionPadding
                        spacing: 12

                        Text {
                            text: "外观"
                            color: Theme.text
                            font.pixelSize: root.sectionTitleSize
                            font.weight: Font.DemiBold
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Text {
                                Layout.preferredWidth: 88
                                Layout.alignment: Qt.AlignVCenter
                                text: "主题模式"
                                color: Theme.muted
                                font.pixelSize: root.bodyFontSize
                            }

                            ThemedComboBox {
                                Layout.preferredWidth: 180
                                Layout.preferredHeight: root.controlHeight
                                font.pixelSize: root.bodyFontSize
                                model: [
                                    { label: "跟随系统", value: Theme.modeSystem },
                                    { label: "暗色", value: Theme.modeDark },
                                    { label: "浅色", value: Theme.modeLight }
                                ]
                                textRole: "label"
                                currentIndex: root.draftThemeMode
                                onActivated: {
                                    root.draftThemeMode = model[index].value
                                    if (viewModel) {
                                        viewModel.themeMode = root.draftThemeMode
                                    }
                                }
                            }

                            Item { Layout.fillWidth: true }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Text {
                                Layout.preferredWidth: 88
                                Layout.alignment: Qt.AlignVCenter
                                text: "关闭按钮"
                                color: Theme.muted
                                font.pixelSize: root.bodyFontSize
                            }

                            ThemedComboBox {
                                Layout.preferredWidth: 220
                                Layout.preferredHeight: root.controlHeight
                                font.pixelSize: root.bodyFontSize
                                model: [
                                    { label: "每次询问", value: 0 },
                                    { label: "最小化到托盘", value: 1 },
                                    { label: "直接退出软件", value: 2 }
                                ]
                                textRole: "label"
                                currentIndex: root.draftCloseButtonBehavior
                                onActivated: root.draftCloseButtonBehavior = model[index].value
                            }

                            Text {
                                Layout.fillWidth: true
                                text: "选择点击主窗口关闭按钮时的默认行为"
                                color: Theme.weak
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }
                        }

                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 18
                    color: Theme.panel2
                    border.width: 1
                    border.color: Theme.line
                    implicitHeight: visionContent.implicitHeight + root.sectionPadding * 2

                    ColumnLayout {
                        id: visionContent
                        anchors.fill: parent
                        anchors.margins: root.sectionPadding
                        spacing: 14

                        Text {
                            text: "视觉解析"
                            color: Theme.text
                            font.pixelSize: root.sectionTitleSize
                            font.weight: Font.DemiBold
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "点击卡片设为默认使用；编辑后点击“保存并应用”才会切换解析服务。"
                            color: Theme.muted
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }

                        Flow {
                            id: visionConfigFlow
                            Layout.fillWidth: true
                            Layout.preferredHeight: childrenRect.height
                            spacing: 12

                            Repeater {
                                model: root.draftVisionApiConfigs

                                delegate: Rectangle {
                                    id: visionConfigCard
                                    property var config: modelData
                                    property bool active: config.id === root.draftActiveVisionApiConfigId
                                    width: Math.min(300, Math.max(220, (visionConfigFlow.width - visionConfigFlow.spacing) / 2))
                                    height: 154
                                    radius: 12
                                    color: active ? Qt.rgba(0.22, 0.48, 0.92, 0.16) : Theme.panel
                                    border.width: active ? 2 : 1
                                    border.color: active ? Theme.blue : Theme.line

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: root.selectVisionApiConfig(visionConfigCard.config.id)
                                    }

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 14
                                        spacing: 6

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8

                                            Text {
                                                Layout.fillWidth: true
                                                text: visionConfigCard.config.name
                                                color: Theme.text
                                                font.pixelSize: 15
                                                font.weight: Font.DemiBold
                                                elide: Text.ElideRight
                                            }

                                            ActionButton {
                                                Layout.preferredWidth: 54
                                                Layout.preferredHeight: 30
                                                text: "编辑"
                                                textPixelSize: 12
                                                onClicked: root.openVisionApiConfigDialog(visionConfigCard.config)
                                            }

                                            ActionButton {
                                                visible: root.draftVisionApiConfigs.length > 1
                                                Layout.preferredWidth: visible ? 54 : 0
                                                Layout.preferredHeight: 30
                                                text: "删除"
                                                textPixelSize: 12
                                                onClicked: root.removeVisionApiConfig(visionConfigCard.config.id)
                                            }
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: visionConfigCard.config.model.length > 0
                                                ? visionConfigCard.config.model : "未设置模型"
                                            color: visionConfigCard.active ? Theme.blue : Theme.muted
                                            font.pixelSize: 13
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: visionConfigCard.config.baseUrl.length > 0
                                                ? visionConfigCard.config.baseUrl
                                                      .replace("https://", "")
                                                      .replace("http://", "")
                                                : "未设置接口"
                                            color: Theme.weak
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                        }

                                        Item { Layout.fillHeight: true }

                                        Rectangle {
                                            visible: visionConfigCard.active
                                            Layout.preferredWidth: currentVisionConfigLabel.implicitWidth + 14
                                            Layout.preferredHeight: 24
                                            radius: 12
                                            color: Qt.rgba(0.25, 0.55, 1.0, 0.18)

                                            Text {
                                                id: currentVisionConfigLabel
                                                anchors.centerIn: parent
                                                text: "当前使用"
                                                color: Theme.blue
                                                font.pixelSize: 11
                                                font.weight: Font.DemiBold
                                            }
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                width: Math.min(300, Math.max(220, (visionConfigFlow.width - visionConfigFlow.spacing) / 2))
                                height: 154
                                radius: 12
                                color: Qt.rgba(0, 0, 0, 0)
                                border.width: 1
                                border.color: Theme.line

                                Column {
                                    anchors.centerIn: parent
                                    spacing: 8

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "+"
                                        color: Theme.blue
                                        font.pixelSize: 30
                                    }

                                    Text {
                                        text: "添加 API 配置"
                                        color: Theme.blue
                                        font.pixelSize: 13
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.openVisionApiConfigDialog(null)
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: root.activeVisionApiConfig()
                                && root.activeVisionApiConfig().model.trim().toLowerCase() === "minimax-m3"
                            text: "MiniMax-M3 将自动使用 MiniMax 原生接口，并对视频帧解析启用最多 200 路并发请求；其他模型不受影响。"
                            color: Theme.muted
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }

                        GridLayout {
                            columns: 2
                            columnSpacing: 14
                            rowSpacing: 14
                            Layout.fillWidth: true

                            Text {
                                Layout.preferredWidth: root.formLabelWidth
                                Layout.alignment: Qt.AlignVCenter
                                text: "抽帧策略"
                                color: Theme.muted
                                font.pixelSize: root.bodyFontSize
                            }

                            ThemedComboBox {
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.controlHeight
                                font.pixelSize: root.bodyFontSize
                                model: [
                                    { label: "逐帧", value: 0 },
                                    { label: "场景变化 + 间隔补帧", value: 1 },
                                    { label: "固定间隔", value: 2 },
                                    { label: "高保真采样", value: 3 }
                                ]
                                textRole: "label"
                                currentIndex: root.draftVideoFrameExtractionStrategy
                                onActivated: root.draftVideoFrameExtractionStrategy = model[index].value
                            }

                            Text {
                                visible: root.draftVideoFrameExtractionStrategy !== 0
                                Layout.preferredWidth: root.formLabelWidth
                                Layout.alignment: Qt.AlignVCenter
                                text: "抽帧间隔（秒）"
                                color: Theme.muted
                                font.pixelSize: root.bodyFontSize
                            }

                            ThemedSpinBox {
                                visible: root.draftVideoFrameExtractionStrategy !== 0
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.controlHeight
                                font.pixelSize: root.bodyFontSize
                                from: 1
                                to: 2400
                                value: Math.round(root.draftVideoFrameIntervalSeconds * 10)
                                textFromValue: function(value, locale) { return (value / 10).toFixed(1) }
                                valueFromText: function(text, locale) { return Math.round(Number.fromLocaleString(locale, text) * 10) }
                                onValueModified: root.draftVideoFrameIntervalSeconds = value / 10
                            }

                            Text {
                                visible: root.draftVideoFrameExtractionStrategy === 1
                                Layout.preferredWidth: root.formLabelWidth
                                Layout.alignment: Qt.AlignVCenter
                                text: "场景阈值（0.05–0.95）"
                                color: Theme.muted
                                font.pixelSize: root.bodyFontSize
                            }

                            ThemedSpinBox {
                                visible: root.draftVideoFrameExtractionStrategy === 1
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.controlHeight
                                font.pixelSize: root.bodyFontSize
                                from: 5
                                to: 95
                                value: Math.round(root.draftVideoSceneThreshold * 100)
                                textFromValue: function(value, locale) { return (value / 100).toFixed(2) }
                                valueFromText: function(text, locale) { return Math.round(Number.fromLocaleString(locale, text) * 100) }
                                onValueModified: root.draftVideoSceneThreshold = value / 100
                            }

                            Text {
                                Layout.preferredWidth: root.formLabelWidth
                                Layout.alignment: Qt.AlignVCenter
                                text: "最低清晰度（0–1）"
                                color: Theme.muted
                                font.pixelSize: root.bodyFontSize
                            }

                            ThemedSpinBox {
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.controlHeight
                                font.pixelSize: root.bodyFontSize
                                from: 0
                                to: 100
                                value: Math.round(root.draftVideoMinimumSharpness * 100)
                                textFromValue: function(value, locale) { return (value / 100).toFixed(2) }
                                valueFromText: function(text, locale) { return Math.round(Number.fromLocaleString(locale, text) * 100) }
                                onValueModified: root.draftVideoMinimumSharpness = value / 100
                            }

                            Text {
                                Layout.preferredWidth: root.formLabelWidth
                                Layout.alignment: Qt.AlignVCenter
                                text: "超时（秒）"
                                color: Theme.muted
                                font.pixelSize: root.bodyFontSize
                            }

                            ThemedSpinBox {
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.controlHeight
                                font.pixelSize: root.bodyFontSize
                                from: 5
                                to: 600
                                value: root.draftAnalysisTimeoutSec
                                onValueModified: root.draftAnalysisTimeoutSec = value
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 9

                            Text {
                                text: "空闲自动解析文件类型"
                                color: Theme.text
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                            }

                            Text {
                                Layout.fillWidth: true
                                text: "图片和视频默认开启。文档与 PSD/PSB 文件默认仅建立索引；勾选并保存后，软件会在电脑空闲时自动解析已有和新增素材。"
                                color: Theme.muted
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 22

                                RowLayout {
                                    spacing: 8

                                    ThemedSwitch {
                                        objectName: "documentAutoAnalysisSwitch"
                                        checked: root.draftDocumentAutoAnalysisEnabled
                                        onToggled: root.draftDocumentAutoAnalysisEnabled = checked
                                    }

                                    Text {
                                        text: "文档"
                                        color: Theme.text
                                        font.pixelSize: root.bodyFontSize
                                    }
                                }

                                RowLayout {
                                    spacing: 8

                                    ThemedSwitch {
                                        objectName: "photoshopAutoAnalysisSwitch"
                                        checked: root.draftPhotoshopAutoAnalysisEnabled
                                        onToggled: root.draftPhotoshopAutoAnalysisEnabled = checked
                                    }

                                    Text {
                                        text: "PSD 文件（Adobe 图片文件）"
                                        color: Theme.text
                                        font.pixelSize: root.bodyFontSize
                                    }
                                }

                                Item { Layout.fillWidth: true }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 14
                            color: Qt.rgba(0.36, 0.56, 0.92, 0.10)
                            border.width: 1
                            border.color: Qt.rgba(0.56, 0.72, 1.0, 0.30)
                            implicitHeight: customDimensionSettingsRow.implicitHeight + 28

                            RowLayout {
                                id: customDimensionSettingsRow
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 14

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 5

                                    Text {
                                        text: "自定义解析维度"
                                        color: Theme.text
                                        font.pixelSize: 16
                                        font.weight: Font.DemiBold
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: "内置维度已合并到常规任务；可在此添加行业、项目或品牌专用维度。当前已添加 "
                                              + (viewModel ? viewModel.customAnalysisDimensions.length : 0) + " 个。"
                                        color: Theme.muted
                                        font.pixelSize: 12
                                        wrapMode: Text.Wrap
                                    }
                                }

                                ActionButton {
                                    objectName: "customAnalysisDimensionButton"
                                    Layout.preferredWidth: 154
                                    Layout.preferredHeight: root.controlHeight
                                    text: "自定义解析维度"
                                    primary: true
                                    textPixelSize: 14
                                    onClicked: root.openCustomDimensionDialog()
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 14
                            color: Qt.rgba(0.22, 0.48, 0.84, 0.08)
                            border.width: 1
                            border.color: Qt.rgba(0.22, 0.48, 0.84, 0.28)
                            implicitHeight: searchAssistSettings.implicitHeight + 28

                            ColumnLayout {
                                id: searchAssistSettings
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 8

                                Text {
                                    text: "智能搜索与隐私"
                                    color: Theme.text
                                    font.pixelSize: 16
                                    font.weight: Font.DemiBold
                                }

                                ThemedSwitch {
                                    checked: root.draftSearchAssistantEnabled
                                    text: "实时使用内置轻量文本模型辅助理解查询"
                                    font.pixelSize: root.bodyFontSize
                                    onToggled: root.draftSearchAssistantEnabled = checked
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10

                                    Text {
                                        Layout.preferredWidth: 150
                                        Layout.alignment: Qt.AlignVCenter
                                        text: "自动卸载时间"
                                        color: root.draftSearchAssistantEnabled ? Theme.muted : Theme.weak
                                        font.pixelSize: root.bodyFontSize
                                    }

                                    ThemedSpinBox {
                                        objectName: "searchAssistantAutoUnloadMinutesInput"
                                        Layout.preferredWidth: 150
                                        Layout.preferredHeight: root.controlHeight
                                        enabled: root.draftSearchAssistantEnabled
                                        font.pixelSize: root.bodyFontSize
                                        from: 5
                                        to: 1440
                                        value: root.draftSearchAssistantAutoUnloadMinutes
                                        onValueModified: root.draftSearchAssistantAutoUnloadMinutes = value
                                    }

                                    Text {
                                        Layout.alignment: Qt.AlignVCenter
                                        text: "分钟"
                                        color: Theme.weak
                                        font.pixelSize: 13
                                    }

                                    ThemedComboBox {
                                        id: searchAssistantAutoUnloadPreset
                                        objectName: "searchAssistantAutoUnloadPreset"
                                        Layout.preferredWidth: 190
                                        Layout.preferredHeight: root.controlHeight
                                        enabled: root.draftSearchAssistantEnabled
                                        font.pixelSize: root.bodyFontSize
                                        model: [
                                            { label: "15 分钟", value: 15 },
                                            { label: "30 分钟（默认）", value: 30 },
                                            { label: "1 小时", value: 60 },
                                            { label: "2 小时", value: 120 },
                                            { label: "4 小时", value: 240 },
                                            { label: "8 小时", value: 480 }
                                        ]
                                        textRole: "label"
                                        currentIndex: {
                                            for (var i = 0; i < model.length; ++i) {
                                                if (model[i].value === root.draftSearchAssistantAutoUnloadMinutes)
                                                    return i
                                            }
                                            return -1
                                        }
                                        displayText: currentIndex >= 0 ? model[currentIndex].label : "自定义时间"
                                        onActivated: root.draftSearchAssistantAutoUnloadMinutes = model[index].value
                                    }

                                    Item { Layout.fillWidth: true }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: "软件启动后会自动在 GPU 上加载模型；鼠标、键盘或快捷搜索操作都会重置计时。连续无操作达到该时间后释放模型和显存，恢复操作时自动重新预热。可直接输入 5–1440 分钟，或从右侧选择常用时间。"
                                    color: Theme.muted
                                    font.pixelSize: 12
                                    wrapMode: Text.Wrap
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: viewModel ? viewModel.localSearchAssistantStatusText : ""
                                    color: Theme.muted
                                    font.pixelSize: 12
                                    wrapMode: Text.Wrap
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: "搜索始终在本机完成：内置查询助手只接收搜索文字，不读取图片、视频或文件路径，也不会发往外网。视觉接口仅用于素材导入和解析，不参与搜索，也不会在搜索时发送候选帧。"
                                    color: Theme.muted
                                    font.pixelSize: 12
                                    wrapMode: Text.Wrap
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            ActionButton {
                                Layout.preferredWidth: 150
                                Layout.preferredHeight: root.controlHeight
                                text: "测试连通状态"
                                primary: true
                                textPixelSize: root.bodyFontSize
                                onClicked: if (viewModel) {
                                    var config = root.activeVisionApiConfig()
                                    if (config) {
                                        viewModel.testVisionApiConfig(config, root.draftAnalysisTimeoutSec)
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: viewModel ? viewModel.lastMessage : ""
                                color: Theme.muted
                                font.pixelSize: root.bodyFontSize
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 18
                    color: Theme.panel2
                    border.width: 1
                    border.color: Theme.line
                    implicitHeight: thumbnailContent.implicitHeight + root.sectionPadding * 2

                    ColumnLayout {
                        id: thumbnailContent
                        anchors.fill: parent
                        anchors.margins: root.sectionPadding
                        spacing: 12

                        Text {
                            text: "缩略图"
                            color: Theme.text
                            font.pixelSize: root.sectionTitleSize
                            font.weight: Font.DemiBold
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                text: "默认取第几帧作为缩略图"
                                color: Theme.muted
                                font.pixelSize: root.bodyFontSize
                            }

                            ThemedSpinBox {
                                Layout.preferredWidth: 150
                                Layout.preferredHeight: root.controlHeight
                                font.pixelSize: root.bodyFontSize
                                from: 1
                                to: 120
                                value: root.draftThumbnailFrameIndex
                                onValueModified: root.draftThumbnailFrameIndex = value
                            }

                            Item { Layout.fillWidth: true }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                text: "右侧详情多宫格数量"
                                color: Theme.muted
                                font.pixelSize: root.bodyFontSize
                            }

                            ThemedSpinBox {
                                Layout.preferredWidth: 150
                                Layout.preferredHeight: root.controlHeight
                                font.pixelSize: root.bodyFontSize
                                from: 1
                                to: 64
                                value: root.draftContactSheetFrameCount
                                onValueModified: root.draftContactSheetFrameCount = value
                            }

                            Item { Layout.fillWidth: true }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 18
                    color: Theme.panel2
                    border.width: 1
                    border.color: Theme.line
                    implicitHeight: frameCacheContent.implicitHeight + root.sectionPadding * 2

                    ColumnLayout {
                        id: frameCacheContent
                        anchors.fill: parent
                        anchors.margins: root.sectionPadding
                        spacing: 12

                        Text {
                            text: "解析图片"
                            color: Theme.text
                            font.pixelSize: root.sectionTitleSize
                            font.weight: Font.DemiBold
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "实际数据目录：" + (viewModel ? viewModel.dataRootPath : "")
                            color: Theme.muted
                            font.pixelSize: root.bodyFontSize
                            wrapMode: Text.WrapAnywhere
                        }

                        Text {
                            text: "解析图片占用：" + (viewModel ? viewModel.frameCacheSizeLabel : "0B")
                            color: Theme.muted
                            font.pixelSize: root.bodyFontSize
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            ActionButton {
                                Layout.preferredWidth: 132
                                Layout.preferredHeight: root.controlHeight
                                text: "刷新图片信息"
                                textPixelSize: root.bodyFontSize
                                onClicked: if (viewModel) viewModel.refreshCacheInfo()
                            }
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: visionConfigDialog
        modal: true
        width: Math.max(1, Math.min(560, root.width - 24))
        x: Math.max(0, Math.round((root.width - width) / 2))
        y: Math.max(0, Math.round((root.height - height) / 2))
        padding: 22
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property bool editing: false
        property bool apiKeyVisible: false
        property string configId: ""
        property string nameValue: ""
        property string baseUrlValue: ""
        property string apiKeyValue: ""
        property string modelValue: "gpt-4.1-mini"

        onOpened: {
            apiKeyVisible = false
            Qt.callLater(function() { visionConfigNameField.forceActiveFocus() })
        }

        background: Rectangle {
            radius: 18
            color: Theme.panel2
            border.width: 1
            border.color: Theme.line
        }

        contentItem: ColumnLayout {
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: visionConfigDialog.editing ? "编辑 API 配置" : "添加 API 配置"
                color: Theme.text
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                text: "每张卡片保存一组视觉解析接口。选择卡片后，保存并应用即可作为默认接口。"
                color: Theme.muted
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }

            Text {
                text: "配置名称"
                color: Theme.muted
                font.pixelSize: 13
            }

            ThemedTextField {
                id: visionConfigNameField
                Layout.fillWidth: true
                Layout.preferredHeight: root.controlHeight
                text: visionConfigDialog.nameValue
                placeholderText: "例如：主力 GPT-4.1"
                onTextEdited: visionConfigDialog.nameValue = text
            }

            Text {
                text: "Base URL"
                color: Theme.muted
                font.pixelSize: 13
            }

            ThemedTextField {
                Layout.fillWidth: true
                Layout.preferredHeight: root.controlHeight
                text: visionConfigDialog.baseUrlValue
                placeholderText: "https://api.openai.com/v1"
                onTextEdited: visionConfigDialog.baseUrlValue = text
            }

            Text {
                text: "API Key"
                color: Theme.muted
                font.pixelSize: 13
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                ThemedTextField {
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.controlHeight
                    text: visionConfigDialog.apiKeyValue
                    echoMode: visionConfigDialog.apiKeyVisible ? TextInput.Normal : TextInput.Password
                    placeholderText: "输入视觉接口密钥"
                    onTextEdited: visionConfigDialog.apiKeyValue = text
                }

                ActionButton {
                    Layout.preferredWidth: 72
                    Layout.preferredHeight: root.controlHeight
                    text: visionConfigDialog.apiKeyVisible ? "隐藏" : "显示"
                    textPixelSize: 12
                    onClicked: visionConfigDialog.apiKeyVisible = !visionConfigDialog.apiKeyVisible
                }
            }

            Text {
                text: "模型名"
                color: Theme.muted
                font.pixelSize: 13
            }

            ThemedTextField {
                Layout.fillWidth: true
                Layout.preferredHeight: root.controlHeight
                text: visionConfigDialog.modelValue
                placeholderText: "gpt-4.1-mini"
                onTextEdited: visionConfigDialog.modelValue = text
            }

            Text {
                Layout.fillWidth: true
                visible: visionConfigDialog.modelValue.trim().toLowerCase() === "minimax-m3"
                text: "MiniMax-M3 会自动使用 MiniMax 原生接口，并启用最多 200 路视频帧并发请求。"
                color: Theme.muted
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Item { Layout.fillWidth: true }

                ActionButton {
                    Layout.preferredWidth: 78
                    Layout.preferredHeight: root.controlHeight
                    text: "取消"
                    onClicked: visionConfigDialog.close()
                }

                ActionButton {
                    Layout.preferredWidth: 104
                    Layout.preferredHeight: root.controlHeight
                    text: "保存卡片"
                    primary: true
                    onClicked: {
                        root.saveVisionApiConfig({
                            id: visionConfigDialog.configId,
                            name: visionConfigDialog.nameValue.trim().length > 0
                                ? visionConfigDialog.nameValue.trim() : "未命名配置",
                            baseUrl: visionConfigDialog.baseUrlValue.trim(),
                            apiKey: visionConfigDialog.apiKeyValue.trim(),
                            model: visionConfigDialog.modelValue.trim().length > 0
                                ? visionConfigDialog.modelValue.trim() : "gpt-4.1-mini"
                        })
                        visionConfigDialog.close()
                    }
                }
            }
        }
    }

    Dialog {
        id: customDimensionDialog
        objectName: "customAnalysisDimensionDialog"

        modal: true
        width: Math.max(1, Math.min(680, root.width - 24))
        height: Math.max(1, Math.min(580, root.height - 24))
        x: Math.max(0, Math.round((root.width - width) / 2))
        y: Math.max(0, Math.round((root.height - height) / 2))
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: Qt.callLater(function() { customDimensionNameField.forceActiveFocus() })

        background: Rectangle {
            radius: 22
            color: Theme.isDark
                ? Qt.rgba(0.055, 0.070, 0.105, 0.97)
                : Qt.rgba(0.965, 0.978, 1.0, 0.98)
            border.width: 1
            border.color: Qt.rgba(0.58, 0.72, 1.0, 0.34)

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 1
                height: parent.height * 0.42
                radius: 21
                color: Qt.rgba(0.30, 0.50, 0.90, 0.055)
            }
        }

        ScrollView {
            id: customDimensionScroll
            anchors.fill: parent
            anchors.margins: 22
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal: ThemedScrollBar {
                policy: ScrollBar.AlwaysOff
            }

            ScrollBar.vertical: ThemedScrollBar {
                policy: ScrollBar.AsNeeded
            }

            MiddleDragScrollHandler {
                parent: customDimensionScroll.contentItem
                flickable: customDimensionScroll.contentItem
            }

            ColumnLayout {
                width: customDimensionScroll.availableWidth
                spacing: 16

                Text {
                    Layout.fillWidth: true
                    text: "自定义解析维度"
                    color: Theme.text
                    font.pixelSize: 22
                    font.weight: Font.Black
                }

                Text {
                    Layout.fillWidth: true
                    text: "添加后，常规解析、补充解析和全部重新解析都会自动处理这些维度，无需在每次任务前重复勾选。"
                    color: Theme.muted
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                }

                Text {
                    text: "已并入常规解析"
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }

                Flow {
                    id: builtInDimensionFlow
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: 9

                    Repeater {
                        model: ["色彩风格", "构图与镜头语言", "品牌/文字线索", "适用场景", "情绪氛围"]

                        delegate: Rectangle {
                            width: builtInDimensionLabel.implicitWidth + 28
                            height: 36
                            radius: 18
                            color: Qt.rgba(0.44, 0.61, 0.94, 0.12)
                            border.width: 1
                            border.color: Qt.rgba(0.62, 0.76, 1.0, 0.28)

                            Text {
                                id: builtInDimensionLabel
                                anchors.centerIn: parent
                                text: modelData
                                color: Theme.muted
                                font.pixelSize: 13
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Qt.rgba(0.62, 0.72, 0.90, 0.16)
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    ThemedTextField {
                        id: customDimensionNameField
                        objectName: "customAnalysisDimensionNameField"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        placeholderText: "例如：电商转化潜力、服装版型、镜头可复用性"
                        maximumLength: 32
                        onAccepted: root.submitCustomDimension()
                    }

                    ActionButton {
                        Layout.preferredWidth: 88
                        Layout.preferredHeight: 42
                        text: "添加维度"
                        primary: true
                        onClicked: root.submitCustomDimension()
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.customDimensionMessage.length > 0
                    text: root.customDimensionMessage
                    color: text.indexOf("已添加") === 0 ? Theme.blue : Theme.orange
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }

                Text {
                    text: "我的自定义维度"
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }

                Text {
                    Layout.fillWidth: true
                    visible: !viewModel || viewModel.customAnalysisDimensions.length === 0
                    text: "尚未添加自定义维度。常规解析仍会自动完成上方五项内置维度。"
                    color: Theme.weak
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                }

                Flow {
                    id: customDimensionFlow
                    objectName: "customAnalysisDimensionGlassFlow"
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: 10

                    Repeater {
                        model: viewModel ? viewModel.customAnalysisDimensions : []

                        delegate: Rectangle {
                            id: customDimensionChip
                            width: Math.min(customDimensionFlow.width, customDimensionChipLabel.implicitWidth + 58)
                            height: 40
                            radius: 20
                            color: chipMouse.containsMouse
                                ? Qt.rgba(0.36, 0.61, 1.0, 0.24)
                                : Qt.rgba(0.36, 0.61, 1.0, 0.15)
                            border.width: 1
                            border.color: chipMouse.containsMouse
                                ? Qt.rgba(0.74, 0.84, 1.0, 0.72)
                                : Qt.rgba(0.67, 0.79, 1.0, 0.42)

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                height: 1
                                color: Qt.rgba(1, 1, 1, 0.28)
                            }

                            Row {
                                anchors.centerIn: parent
                                spacing: 10

                                Text {
                                    id: customDimensionChipLabel
                                    text: modelData
                                    color: Theme.text
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    text: "×"
                                    color: chipMouse.containsMouse ? Theme.text : Theme.muted
                                    font.pixelSize: 18
                                    font.weight: Font.DemiBold
                                }
                            }

                            MouseArea {
                                id: chipMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (viewModel && viewModel.removeCustomAnalysisDimension(modelData)) {
                                        root.customDimensionMessage = "已移除“" + modelData + "”。"
                                    }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }

                    ActionButton {
                        Layout.preferredWidth: 88
                        Layout.preferredHeight: 38
                        text: "完成"
                        primary: true
                        onClicked: customDimensionDialog.close()
                    }
                }
            }
        }
    }
}
