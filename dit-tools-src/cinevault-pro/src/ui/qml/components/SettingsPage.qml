import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CineVault

Item {
    id: root

    property var viewModel
    property string draftVisionBaseUrl: ""
    property string draftVisionApiKey: ""
    property string draftVisionModel: ""
    property bool draftSearchAssistantEnabled: true
    property int draftSearchAssistantAutoUnloadMinutes: 30
    property bool draftQuickSearchEnabled: true
    property string draftQuickSearchShortcut: "Alt+Space"
    property bool draftStartAtLogin: false
    property int draftCloseButtonBehavior: 0
    property int draftAnalysisMode: 0
    property int draftFrameInterval: 10
    property int draftThumbnailFrameIndex: 3
    property int draftContactSheetFrameCount: 24
    property int draftAnalysisTimeoutSec: 60
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
            draftVisionBaseUrl = viewModel.visionBaseUrl
            draftVisionApiKey = viewModel.visionApiKey
            draftVisionModel = viewModel.visionModel
            draftSearchAssistantEnabled = viewModel.searchAssistantEnabled
            draftSearchAssistantAutoUnloadMinutes = viewModel.searchAssistantAutoUnloadMinutes
            draftQuickSearchEnabled = viewModel.quickSearchEnabled
            draftQuickSearchShortcut = viewModel.quickSearchShortcut
            draftStartAtLogin = viewModel.startAtLogin
            draftCloseButtonBehavior = viewModel.closeButtonBehavior
            draftAnalysisMode = viewModel.analysisMode
            draftFrameInterval = viewModel.frameInterval
            draftThumbnailFrameIndex = viewModel.thumbnailFrameIndex
            draftContactSheetFrameCount = viewModel.contactSheetFrameCount
            draftAnalysisTimeoutSec = viewModel.analysisTimeoutSec
            draftThemeMode = viewModel.themeMode
            draftUpdatePolicy = viewModel.updatePolicy
            draftUpdateDownloadMode = viewModel.updateDownloadMode
            draftUpdateManualProxyUrl = viewModel.updateManualProxyUrl
        }
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
                            root.draftVisionBaseUrl,
                            root.draftVisionApiKey,
                            root.draftVisionModel,
                            root.draftSearchAssistantEnabled,
                            root.draftSearchAssistantAutoUnloadMinutes,
                            root.draftQuickSearchEnabled,
                            root.draftQuickSearchShortcut,
                            root.draftStartAtLogin,
                            root.draftCloseButtonBehavior,
                            root.draftAnalysisMode,
                            root.draftFrameInterval,
                            root.draftThumbnailFrameIndex,
                            root.draftContactSheetFrameCount,
                            root.draftAnalysisTimeoutSec,
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

                        GridLayout {
                            columns: 2
                            columnSpacing: 14
                            rowSpacing: 14
                            Layout.fillWidth: true

                            Text {
                                Layout.preferredWidth: root.formLabelWidth
                                Layout.alignment: Qt.AlignVCenter
                                text: "Base URL"
                                color: Theme.muted
                                font.pixelSize: root.bodyFontSize
                            }

                            ThemedTextField {
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.controlHeight
                                font.pixelSize: root.bodyFontSize
                                text: root.draftVisionBaseUrl
                                placeholderText: "https://api.openai.com/v1"
                                onTextEdited: root.draftVisionBaseUrl = text
                            }

                            Text {
                                Layout.preferredWidth: root.formLabelWidth
                                Layout.alignment: Qt.AlignVCenter
                                text: "API Key"
                                color: Theme.muted
                                font.pixelSize: root.bodyFontSize
                            }

                            ThemedTextField {
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.controlHeight
                                font.pixelSize: root.bodyFontSize
                                text: root.draftVisionApiKey
                                echoMode: TextInput.Password
                                placeholderText: "输入视觉接口密钥"
                                onTextEdited: root.draftVisionApiKey = text
                            }

                            Text {
                                Layout.preferredWidth: root.formLabelWidth
                                Layout.alignment: Qt.AlignVCenter
                                text: "模型名"
                                color: Theme.muted
                                font.pixelSize: root.bodyFontSize
                            }

                            ThemedTextField {
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.controlHeight
                                font.pixelSize: root.bodyFontSize
                                text: root.draftVisionModel
                                placeholderText: "gpt-4.1-mini"
                                onTextEdited: root.draftVisionModel = text
                            }

                            Text {
                                Layout.preferredWidth: root.formLabelWidth
                                Layout.alignment: Qt.AlignVCenter
                                text: "抽帧模式"
                                color: Theme.muted
                                font.pixelSize: root.bodyFontSize
                            }

                            ThemedComboBox {
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.controlHeight
                                font.pixelSize: root.bodyFontSize
                                model: [
                                    { label: "每10帧抽1帧", value: 0 },
                                    { label: "逐帧解析", value: 1 },
                                    { label: "自定义间隔", value: 2 }
                                ]
                                textRole: "label"
                                currentIndex: root.draftAnalysisMode === 1 ? 1 : (root.draftAnalysisMode === 2 ? 2 : 0)
                                onActivated: root.draftAnalysisMode = model[index].value
                            }

                            Text {
                                visible: root.draftAnalysisMode === 2
                                Layout.preferredWidth: root.formLabelWidth
                                Layout.alignment: Qt.AlignVCenter
                                text: "抽帧间隔"
                                color: Theme.muted
                                font.pixelSize: root.bodyFontSize
                            }

                            ThemedSpinBox {
                                visible: root.draftAnalysisMode === 2
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.controlHeight
                                font.pixelSize: root.bodyFontSize
                                from: 1
                                to: 240
                                value: root.draftFrameInterval
                                onValueModified: root.draftFrameInterval = value
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
                                    viewModel.testConnectionWith(
                                        root.draftVisionBaseUrl,
                                        root.draftVisionApiKey,
                                        root.draftVisionModel,
                                        root.draftAnalysisTimeoutSec)
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
