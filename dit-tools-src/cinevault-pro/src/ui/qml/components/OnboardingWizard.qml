import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import CineVault

Popup {
    id: root

    property var shellVm
    property var settingsVm
    property int stepIndex: 0
    readonly property int stepCount: 7
    readonly property bool apiVerified: settingsVm
                                      && settingsVm.visionBaseUrl.trim().length > 0
                                      && settingsVm.visionApiKey.trim().length > 0
                                      && settingsVm.visionModel.trim().length > 0
                                      && settingsVm.lastMessage === "视觉接口连通测试成功。"

    parent: Overlay.overlay
    modal: false
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    width: Math.min(720, parent ? parent.width - 36 : 720)
    height: Math.min(610, parent ? parent.height - 112 : 610)
    x: Math.max(18, (parent ? parent.width : width) - width - 26)
    y: Math.max(78, (parent ? parent.height : height) - height - 76)

    function restart() {
        stepIndex = 0
        open()
    }

    function stepTitle() {
        switch (stepIndex) {
        case 0: return "先认识影资管家的工作方式"
        case 1: return "创建项目：先确定数据的家"
        case 2: return "添加源素材：告诉软件去哪里找文件"
        case 3: return "等待索引：让素材库建立可检索目录"
        case 4: return "浏览与检索：从素材库进入素材管理中心"
        case 5: return "设置视觉 API：开启智能解析"
        default: return "准备完成：开始你的第一个工作流"
        }
    }

    function stepDescription() {
        switch (stepIndex) {
        case 0:
            return "影资管家把“项目数据”和“原始素材”分开管理：项目负责保存数据库、缓存、分析结果和报表；素材源始终保留在原有磁盘或共享目录中。先按向导完成一遍，之后就可以自由使用各个工作区。"
        case 1:
            return "在“项目库”中点击“新建项目”，输入项目名称后选择项目保存目录。创建完成后，再点击项目卡片进入该项目。已经存在的项目可用“打开项目”加入项目库。"
        case 2:
            return "进入项目后，点击左侧“添加素材源”。你可以选择单个素材文件夹、输入网络共享路径，或把整个磁盘卷加入全盘索引。软件只读取源素材，不会移动或改写原始文件。"
        case 3:
            return "添加后会后台建立文件、文件夹和基础索引。底部“任务中心”显示总进度，任务页可查看明细。即使软件意外退出，下次打开项目也会从已保存的目录检查点继续。"
        case 4:
            return "“素材库”适合按文件夹浏览、预览和检查技术信息；“素材管理中心”适合从全项目范围搜索、筛选和发起解析。先打开任一页面，熟悉素材卡片和搜索入口。"
        case 5:
            return "在“设置 → 视觉 API”填写服务地址、API Key 和模型名称，先点击“保存并应用”，再点击“测试连接”。只有测试成功后，向导才会允许进入下一步。"
        default:
            return "现在可以在素材管理中心选择素材执行常规解析，或使用搜索和报表功能。后续随时点击右上角“向导”，可从第一步重新学习完整流程。"
        }
    }

    function stepAdvice() {
        switch (stepIndex) {
        case 0: return "推荐顺序：项目 → 素材源 → 索引 → 浏览/搜索 → API → 解析。"
        case 1: return "推荐为项目准备独立的存储位置，例如 D: 或专门的项目磁盘。"
        case 2: return "首次使用可先添加一张存储卡或一个小文件夹，确认流程后再添加完整素材盘。"
        case 3: return "索引期间不要重命名、移动或拔出素材盘；如果必须中断，稍后重新打开同一项目即可恢复。"
        case 4: return "先用关键词和筛选缩小范围，再进入详情查看缩略图、技术元数据和解析状态。"
        case 5: return "API Key 属于敏感凭据，不要截屏、分享或写入项目名称、备注和素材文件名。"
        default: return "首次批量解析建议从少量代表性素材开始，确认模型结果与费用预期后再扩大范围。"
        }
    }

    function requirementText() {
        if (stepIndex === 0 || stepIndex === 6) return "已了解本步骤后即可继续。"
        if (stepIndex === 1) return shellVm && shellVm.projectEntered ? "已进入项目，可以继续。" : "请创建或打开项目，并点击项目卡片进入项目。"
        if (stepIndex === 2) return shellVm && shellVm.sourceRootCount > 0 ? "已添加素材源，可以继续。" : "请至少添加一个素材目录、共享目录或磁盘卷。"
        if (stepIndex === 3) {
            if (!shellVm || shellVm.sourceRootCount === 0) return "请先添加素材源。"
            return shellVm.sourceScanInProgress ? "索引仍在进行中，请等待任务中心显示完成。" : "首轮索引已结束，可以继续浏览素材。"
        }
        if (stepIndex === 4) {
            const visited = shellVm && (shellVm.currentWorkspace === shellVm.libraryWorkspaceId
                                        || shellVm.currentWorkspace === shellVm.materialCenterWorkspaceId)
            return visited ? "已进入素材浏览页面，可以继续。" : "请打开素材库或素材管理中心。"
        }
        return apiVerified ? "API 配置与连接测试已通过。" : "请保存 API 配置并完成一次成功的连接测试。"
    }

    function canAdvance() {
        if (stepIndex === 0 || stepIndex === 6) return true
        if (stepIndex === 1) return shellVm && shellVm.projectEntered
        if (stepIndex === 2) return shellVm && shellVm.sourceRootCount > 0
        if (stepIndex === 3) return shellVm && shellVm.sourceRootCount > 0 && !shellVm.sourceScanInProgress
        if (stepIndex === 4) return shellVm && (shellVm.currentWorkspace === shellVm.libraryWorkspaceId
                                                || shellVm.currentWorkspace === shellVm.materialCenterWorkspaceId)
        return apiVerified
    }

    function actionLabel() {
        switch (stepIndex) {
        case 1: return "前往项目库"
        case 2: return "添加素材源"
        case 3: return "打开任务页"
        case 4: return "打开素材库"
        case 5: return "打开设置"
        default: return ""
        }
    }

    function performAction() {
        if (!shellVm) return
        if (stepIndex === 1) shellVm.currentWorkspace = shellVm.projectLibraryWorkspaceId
        else if (stepIndex === 2) shellVm.addSourceDirectory()
        else if (stepIndex === 3) shellVm.currentWorkspace = shellVm.jobsWorkspaceId
        else if (stepIndex === 4) shellVm.currentWorkspace = shellVm.libraryWorkspaceId
        else if (stepIndex === 5) shellVm.openSettings()
    }

    background: Rectangle {
        radius: 20
        color: Theme.panel
        border.width: 1
        border.color: Theme.selectedLine
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 22
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: "新手向导 · 第 " + (root.stepIndex + 1) + "/" + root.stepCount + " 步"
                    color: Theme.blue
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }

                Text {
                    Layout.fillWidth: true
                    text: root.stepTitle()
                    color: Theme.text
                    font.pixelSize: 22
                    font.weight: Font.Black
                    elide: Text.ElideRight
                }
            }

            ActionButton {
                Layout.preferredWidth: 64
                Layout.preferredHeight: 34
                text: "关闭"
                onClicked: root.close()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.line
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 26

            Repeater {
                model: root.stepCount

                delegate: Rectangle {
                    required property int index
                    x: 22 + index * ((parent.width - 44 - root.stepCount * width) / Math.max(1, root.stepCount - 1))
                    anchors.verticalCenter: parent.verticalCenter
                    width: 18
                    height: 4
                    radius: 2
                    color: index <= root.stepIndex ? Theme.blue : Theme.line
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 22
            Layout.topMargin: 4
            spacing: 14

            Text {
                Layout.fillWidth: true
                text: root.stepDescription()
                color: Theme.text
                font.pixelSize: 14
                wrapMode: Text.Wrap
                lineHeight: 1.35
            }

            Rectangle {
                Layout.fillWidth: true
                visible: root.stepIndex === 1
                Layout.preferredHeight: warningText.implicitHeight + 24
                radius: 12
                color: Qt.rgba(Theme.orange.r, Theme.orange.g, Theme.orange.b, 0.14)
                border.width: 1
                border.color: Theme.orange

                Text {
                    id: warningText
                    anchors.fill: parent
                    anchors.margins: 12
                    text: "⚠ 项目文件夹不要选择素材文件夹。项目文件夹用于保存项目数据库、缩略图、解析帧、报表和缓存；最好放到专门存储项目的非 C 盘，避免巨量缓存和素材数据撑爆系统盘。"
                    color: Theme.text
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                    lineHeight: 1.3
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: adviceText.implicitHeight + 22
                radius: 12
                color: Theme.panel2
                border.width: 1
                border.color: Theme.line

                Text {
                    id: adviceText
                    anchors.fill: parent
                    anchors.margins: 11
                    text: "建议：" + root.stepAdvice()
                    color: Theme.muted
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: requirementText.implicitHeight + 20
                radius: 10
                color: root.canAdvance() ? Qt.rgba(Theme.green.r, Theme.green.g, Theme.green.b, 0.12)
                                         : Qt.rgba(Theme.blue.r, Theme.blue.g, Theme.blue.b, 0.10)

                Text {
                    id: requirementText
                    anchors.fill: parent
                    anchors.margins: 10
                    text: root.requirementText()
                    color: root.canAdvance() ? Theme.green : Theme.muted
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                ActionButton {
                    Layout.preferredWidth: 110
                    Layout.preferredHeight: 40
                    visible: root.actionLabel().length > 0
                    text: root.actionLabel()
                    onClicked: root.performAction()
                }

                Item { Layout.fillWidth: true }

                ActionButton {
                    Layout.preferredWidth: 76
                    Layout.preferredHeight: 40
                    text: "上一步"
                    enabled: root.stepIndex > 0
                    onClicked: root.stepIndex = Math.max(0, root.stepIndex - 1)
                }

                ActionButton {
                    Layout.preferredWidth: root.stepIndex === root.stepCount - 1 ? 92 : 84
                    Layout.preferredHeight: 40
                    text: root.stepIndex === root.stepCount - 1 ? "完成" : "下一步"
                    primary: true
                    enabled: root.canAdvance()
                    onClicked: {
                        if (root.stepIndex === root.stepCount - 1) root.close()
                        else root.stepIndex += 1
                    }
                }
            }
        }
    }
}
