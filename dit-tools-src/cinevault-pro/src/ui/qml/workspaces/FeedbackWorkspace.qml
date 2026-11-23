import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import CineVault

Rectangle {
    id: root

    property var viewModel
    property var documentPreviewVm
    property bool workspaceActive: false
    property bool pendingDocumentPreview: false
    property bool profileEditMode: false
    property string draftProfileNickname: ""
    property string draftProfileContact: ""

    color: Theme.bg

    function attachmentSuffix(name) {
        if (!name || name.length === 0) {
            return ""
        }
        var index = name.lastIndexOf(".")
        if (index < 0 || index >= name.length - 1) {
            return ""
        }
        return name.substring(index + 1).toUpperCase()
    }

    function attachmentTypeLabel(attachment) {
        var suffix = attachmentSuffix(attachment.name || "")
        if (suffix.length > 0) {
            return suffix
        }
        if (attachment.previewKind === "document") {
            return "文档"
        }
        if (attachment.previewKind === "video") {
            return "视频"
        }
        if (attachment.previewKind === "image") {
            return "图片"
        }
        return "文件"
    }

    function safeWidth(item) {
        return item ? item.width : 0
    }

    function boundedWidth(item, horizontalPadding, maxWidth) {
        return Math.min(Math.max(0, safeWidth(item) - horizontalPadding), maxWidth)
    }

    function logFeedbackDebug(eventName, details) {
        var suffix = ""
        if (details !== undefined && details !== null) {
            try {
                suffix = " " + JSON.stringify(details)
            } catch (error) {
                suffix = " [details-unserializable]"
            }
        }
        console.warn("[FeedbackWorkspace] " + eventName + suffix)
    }

    function syncProfileDrafts() {
        draftProfileNickname = viewModel ? viewModel.profileNickname : ""
        draftProfileContact = viewModel ? viewModel.profileContact : ""
    }

    function submitProfileEdits() {
        if (viewModel) {
            viewModel.submitProfile(draftProfileNickname, draftProfileContact)
        }
    }

    function acceptSelectedAttachments() {
        if (!viewModel) {
            return
        }
        viewModel.addAttachmentUrls(attachmentDialog.selectedFiles)
        if (viewModel.attachmentSelectionError.length > 0) {
            attachmentSelectionDialog.text = viewModel.attachmentSelectionError
            attachmentSelectionDialog.open()
        }
    }

    function openImageAttachment(attachment) {
        attachmentPreviewOverlay.openImage(attachment.url, attachment.name)
    }

    function previewDocumentAttachment(attachment) {
        if (!viewModel || !documentPreviewVm) {
            return
        }
        pendingDocumentPreview = true
        viewModel.previewDocumentAttachment(attachment.id, attachment.url, attachment.name)
    }

    Component.onCompleted: {
        root.syncProfileDrafts()
        if (viewModel) {
            viewModel.activate()
            viewModel.setWorkspaceActive(workspaceActive)
        }
    }
    onWorkspaceActiveChanged: if (viewModel) viewModel.setWorkspaceActive(workspaceActive)
    onViewModelChanged: {
        root.syncProfileDrafts()
        if (viewModel) {
            viewModel.setWorkspaceActive(workspaceActive)
        }
    }

    Connections {
        target: viewModel

        function onAttachmentPreviewChanged() {
            if (!root.pendingDocumentPreview || !viewModel || viewModel.attachmentPreviewBusy) {
                return
            }
            if (viewModel.attachmentPreviewLocalUrl.toString().length > 0) {
                root.pendingDocumentPreview = false
                attachmentPreviewOverlay.openDocument(viewModel.attachmentPreviewLocalUrl, viewModel.attachmentPreviewTitle)
                return
            }
            if (viewModel.attachmentPreviewError.length > 0) {
                root.pendingDocumentPreview = false
                attachmentPreviewDialog.text = viewModel.attachmentPreviewError
                attachmentPreviewDialog.open()
            }
        }

        function onStateChanged() {
            if (!viewModel) {
                return
            }
            root.logFeedbackDebug("viewModelStateChanged", {
                busy: viewModel.busy,
                ready: viewModel.ready,
                sending: viewModel.sending,
                needsProfile: viewModel.needsProfile,
                unreadCount: viewModel.unreadCount
            })
            if (root.profileEditMode) {
                if (!viewModel.busy
                        && viewModel.ready
                        && root.draftProfileNickname === viewModel.profileNickname
                        && root.draftProfileContact === viewModel.profileContact) {
                    root.profileEditMode = false
                }
                return
            }
            root.syncProfileDrafts()
        }
    }

    FileDialog {
        id: attachmentDialog
        title: "选择要发送给开发者的附件"
        fileMode: FileDialog.OpenFiles
        onAccepted: root.acceptSelectedAttachments()
    }

    MessageDialog {
        id: attachmentSelectionDialog
        title: "附件未加入待发送列表"
    }

    MessageDialog {
        id: attachmentPreviewDialog
        title: "附件预览失败"
    }

    MessageDialog {
        id: clearConversationDialog
        title: "清空会话窗口"
        text: "这会删除你自己发送的全部消息，开发者回复会保留。是否继续？"
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: if (viewModel) viewModel.clearOwnMessages()
    }

    AssetPreviewOverlay {
        id: attachmentPreviewOverlay
        previewVm: root.documentPreviewVm
        onVisibleChanged: if (!visible && viewModel) viewModel.clearAttachmentPreview()
    }

    Rectangle {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 26
        anchors.rightMargin: 26
        visible: viewModel && viewModel.attachmentPreviewBusy
        radius: 16
        color: Qt.rgba(0.07, 0.10, 0.16, 0.92)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.10)
        implicitWidth: previewBusyRow.implicitWidth + 22
        implicitHeight: 42
        z: 120

        Row {
            id: previewBusyRow
            anchors.centerIn: parent
            spacing: 10

            BusyIndicator {
                running: true
                width: 18
                height: 18
            }

            Text {
                text: "正在准备文档预览..."
                color: "white"
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        Rectangle {
            Layout.fillWidth: true
            radius: 22
            color: Theme.panel2
            border.width: 1
            border.color: Theme.line
            implicitHeight: 132

            RowLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 14

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        text: viewModel ? viewModel.title : "使用反馈"
                        color: Theme.text
                        font.pixelSize: 28
                        font.weight: Font.Black
                    }

                    Text {
                        Layout.fillWidth: true
                        text: viewModel ? viewModel.subtitle : "反馈模块不可用"
                        color: Theme.muted
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }

                    Text {
                        Layout.fillWidth: true
                        text: viewModel ? viewModel.statusMessage : ""
                        color: Theme.weak
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 180
                    Layout.preferredHeight: 84
                    radius: 18
                    color: Theme.card
                    border.width: 1
                    border.color: Theme.line

                    Column {
                        anchors.centerIn: parent
                        spacing: 8

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: viewModel ? viewModel.unreadCount : 0
                            color: Theme.blue
                            font.pixelSize: 28
                            font.weight: Font.Black
                        }

                        Text {
                            text: viewModel ? viewModel.connectionStatus : "未连接"
                            color: Theme.muted
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }
            }
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: true
            sourceComponent: !viewModel ? unavailableComponent : (viewModel.needsProfile ? onboardingComponent : chatWorkspaceComponent)
        }
    }

    Component {
        id: unavailableComponent

        Item {
            ColumnLayout {
                anchors.centerIn: parent
                width: root.boundedWidth(parent, 40, 420)
                spacing: 12

                Text {
                    Layout.fillWidth: true
                    text: "当前构建未启用反馈模块"
                    color: Theme.text
                    font.pixelSize: 24
                    font.weight: Font.Black
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    Layout.fillWidth: true
                    text: "请切换到完整工作流构建包后再使用“使用反馈”页面。"
                    color: Theme.muted
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }

    Component {
        id: onboardingComponent

        Item {
            ColumnLayout {
                anchors.centerIn: parent
                width: root.boundedWidth(parent, 40, 520)
                spacing: 14

                Rectangle {
                    Layout.fillWidth: true
                    radius: 24
                    color: Theme.panel2
                    border.width: 1
                    border.color: Theme.line
                    implicitHeight: 380

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 24
                        spacing: 14

                        Text {
                            text: "建立你的长期反馈会话"
                            color: Theme.text
                            font.pixelSize: 24
                            font.weight: Font.Black
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "填写昵称和联系方式后，后续每次打开软件都会自动回到同一条反馈会话。你可以持续补充文本、图片、日志和压缩包，开发者回复后会实时显示。"
                            color: Theme.muted
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                        }

                        ThemedTextField {
                            id: nicknameField
                            Layout.fillWidth: true
                            placeholderText: "昵称，例如：李明 / 摄影组A机"
                        }

                        ThemedTextField {
                            id: contactField
                            Layout.fillWidth: true
                            placeholderText: "联系方式，例如：微信 / 手机 / 邮箱"
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 16
                            color: Theme.card
                            border.width: 1
                            border.color: Theme.line
                            implicitHeight: 108

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 8

                                Text {
                                    text: "会自动附带的信息"
                                    color: Theme.text
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: "软件版本、系统信息、当前项目名与项目路径会跟随反馈一起发送，方便开发者更快定位问题。"
                                    color: Theme.muted
                                    font.pixelSize: 12
                                    wrapMode: Text.Wrap
                                }
                            }
                        }

                        Item { Layout.fillHeight: true }

                        ActionButton {
                            Layout.alignment: Qt.AlignRight
                            Layout.preferredWidth: 132
                            Layout.preferredHeight: 40
                            text: viewModel && viewModel.busy ? "创建中..." : "开始反馈"
                            enabled: viewModel && !viewModel.busy
                            primary: true
                            onClicked: if (viewModel) viewModel.submitProfile(nicknameField.text, contactField.text)
                        }
                    }
                }
            }
        }
    }

    Component {
        id: chatWorkspaceComponent

        RowLayout {
            spacing: 14
            property bool scrollToLatestAfterSubmit: false
            property bool scrollToLatestWhenAvailable: false
            property int scrollToLatestRetries: 0

            function scheduleScrollToLatest(reason) {
                if (!messageListView || messageListView.count <= 0) {
                    scrollToLatestWhenAvailable = true
                    messageScrollToLatestTimer.lastReason = reason
                    root.logFeedbackDebug("messageListScrollSkipped", {
                        reason: reason,
                        count: messageListView ? messageListView.count : -1
                    })
                    return
                }

                scrollToLatestWhenAvailable = false
                scrollToLatestRetries = 4
                messageScrollToLatestTimer.lastReason = reason
                messageScrollToLatestTimer.restart()
            }

            function submitDraftMessage() {
                if (!root.viewModel || root.viewModel.sending) {
                    root.logFeedbackDebug("submitDraftMessageSkipped", {
                        hasViewModel: !!root.viewModel,
                        sending: root.viewModel ? root.viewModel.sending : false
                    })
                    return
                }

                var draftText = draftMessageArea ? draftMessageArea.text : ""
                root.logFeedbackDebug("submitDraftMessageScheduled", {
                    textLength: draftText ? draftText.length : 0,
                    hasDraftArea: !!draftMessageArea
                })
                root.logFeedbackDebug("submitDraftMessageExecuting", {
                    textLength: draftText ? draftText.length : 0,
                    ready: root.viewModel.ready,
                    sending: root.viewModel.sending
                })
                if (draftMessageArea && draftText.trim().length > 0) {
                    draftMessageArea.text = ""
                    root.logFeedbackDebug("draftClearedBeforeSubmit", {
                        textLength: draftText.length
                    })
                }
                scrollToLatestAfterSubmit = true
                root.viewModel.sendMessage(draftText)
            }

            Component.onCompleted: if (root.workspaceActive) scheduleScrollToLatest("chatCompleted")

            Connections {
                target: viewModel
                function onMessageSubmitted(success) {
                    root.logFeedbackDebug("messageSubmitted", {
                        success: success
                    })
                    if (success) {
                        scheduleScrollToLatest("submitSuccess")
                    } else {
                        scrollToLatestAfterSubmit = false
                    }
                }
            }

            Connections {
                target: root
                function onWorkspaceActiveChanged() {
                    if (root.workspaceActive) {
                        scheduleScrollToLatest("workspaceActivated")
                    }
                }
            }

            Timer {
                id: messageScrollToLatestTimer
                interval: 40
                repeat: false
                property string lastReason: ""

                onTriggered: {
                    if (!messageListView || messageListView.count <= 0) {
                        scrollToLatestRetries = 0
                        return
                    }

                    messageListView.forceLayout()
                    messageListView.positionViewAtEnd()
                    scrollToLatestAfterSubmit = false
                    root.logFeedbackDebug("messageListScrolledToLatest", {
                        reason: lastReason,
                        retriesLeft: scrollToLatestRetries,
                        count: messageListView.count,
                        contentHeight: messageListView.contentHeight,
                        contentY: messageListView.contentY
                    })

                    scrollToLatestRetries -= 1
                    if (scrollToLatestRetries > 0) {
                        restart()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 22
                color: Theme.panel2
                border.width: 1
                border.color: Theme.line

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 12

                    ListView {
                        id: messageListView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 12
                        model: viewModel ? viewModel.messageModel : null

                        MiddleDragScrollHandler {
                            flickable: messageListView
                        }

                        ScrollBar.vertical: ThemedScrollBar {
                            policy: ScrollBar.AsNeeded
                        }

                        onCountChanged: {
                            root.logFeedbackDebug("messageListCountChanged", {
                                count: messageListView ? messageListView.count : -1,
                                contentHeight: messageListView ? messageListView.contentHeight : -1
                            })
                            if (scrollToLatestAfterSubmit) {
                                scheduleScrollToLatest("submitMessageCountChanged")
                            } else if (scrollToLatestWhenAvailable || root.workspaceActive) {
                                scheduleScrollToLatest("messageCountChanged")
                            }
                        }

                        delegate: Item {
                            width: ListView.view ? ListView.view.width : 0
                            property real maxBubbleWidth: Math.max(120, Math.min(width * 0.78, 720))
                            property real attachmentCardWidth: 220
                            property real attachmentPreferredWidth: hasAttachments
                                ? Math.min(maxBubbleWidth - 24, attachments.length > 1 ? attachmentCardWidth * 2 + 10 : attachmentCardWidth)
                                : 0
                            property real textPreferredWidth: hasText ? Math.min(textMeasure.implicitWidth, maxBubbleWidth - 24) : 0
                            property real headerPreferredWidth: Math.min(maxBubbleWidth - 24, senderText.implicitWidth + timestampText.implicitWidth + 24)
                            property real bubbleContentWidth: Math.max(120, Math.min(maxBubbleWidth - 24, Math.max(headerPreferredWidth, textPreferredWidth, attachmentPreferredWidth)))
                            height: bubble.implicitHeight

                            Text {
                                id: textMeasure
                                visible: false
                                text: model.text
                                font.pixelSize: 14
                                wrapMode: Text.NoWrap
                            }

                            ThemedMenu {
                                id: messageContextMenu

                                ThemedMenuItem {
                                    text: "复制"
                                    enabled: hasText
                                    onTriggered: if (viewModel) viewModel.copyMessageText(messageId)
                                }
                                ThemedMenuItem {
                                    text: "删除"
                                    enabled: outgoing
                                    onTriggered: if (viewModel) viewModel.deleteOwnMessage(messageId)
                                }
                            }

                            Rectangle {
                                id: bubble
                                width: bubbleContentWidth + 24
                                anchors.left: outgoing ? undefined : parent.left
                                anchors.right: outgoing ? parent.right : undefined
                                radius: 20
                                color: outgoing ? Theme.feedbackOutgoingBg : Theme.feedbackIncomingBg
                                border.width: 1
                                border.color: outgoing ? Theme.feedbackOutgoingBorder : Theme.feedbackIncomingBorder
                                implicitHeight: contentColumn.implicitHeight + 24

                                Column {
                                    id: contentColumn
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 10

                                    RowLayout {
                                        width: root.safeWidth(parent)
                                        spacing: 8

                                        Text {
                                            id: senderText
                                            text: senderLabel
                                            color: outgoing ? Theme.feedbackOutgoingText : Theme.text
                                            font.pixelSize: 13
                                            font.weight: Font.DemiBold
                                        }

                                        Item { Layout.fillWidth: true }

                                        Text {
                                            id: timestampText
                                            text: createdAtLabel
                                            color: outgoing ? Theme.feedbackOutgoingText : Theme.weak
                                            opacity: outgoing ? 0.82 : 1
                                            font.pixelSize: 11
                                        }
                                    }

                                    Text {
                                        visible: hasText
                                        width: bubbleContentWidth
                                        text: model.text
                                        color: outgoing ? Theme.feedbackOutgoingText : Theme.text
                                        font.pixelSize: 14
                                        wrapMode: Text.Wrap
                                    }

                                    Flow {
                                        visible: hasAttachments
                                        width: bubbleContentWidth
                                        spacing: 10

                                        Repeater {
                                            model: attachments

                                            delegate: Rectangle {
                                                width: 220
                                                height: modelData.previewKind === "image" || modelData.previewKind === "video" ? 170 : 118
                                                radius: 16
                                                color: outgoing ? Theme.feedbackOutgoingAttachmentBg : Theme.panel
                                                border.width: 1
                                                border.color: outgoing ? Theme.feedbackOutgoingBorder : Theme.line
                                                clip: true

                                                Rectangle {
                                                    anchors.fill: parent
                                                    anchors.margins: 10
                                                    visible: modelData.previewKind === "image"
                                                    radius: 12
                                                    color: Theme.mediaSurface
                                                    border.width: 1
                                                    border.color: Qt.rgba(1, 1, 1, 0.06)

                                                    Image {
                                                        anchors.fill: parent
                                                        anchors.margins: 8
                                                        asynchronous: true
                                                        cache: false
                                                        fillMode: Image.PreserveAspectFit
                                                        source: modelData.url
                                                    }

                                                    MouseArea {
                                                        anchors.fill: parent
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: root.openImageAttachment(modelData)
                                                    }
                                                }

                                                VideoPreviewPlayer {
                                                    anchors.fill: parent
                                                    visible: modelData.previewKind === "video"
                                                    sourceUrl: modelData.url
                                                    thumbnailUrl: ""
                                                    title: modelData.name
                                                    isVideo: true
                                                    compactMode: true
                                                    autoPrimePreviewFrame: true
                                                    inlineMuted: true
                                                    clickAction: "fullscreen"
                                                }

                                                ColumnLayout {
                                                    anchors.fill: parent
                                                    anchors.margins: 12
                                                    spacing: 8
                                                    visible: modelData.previewKind === "document" || modelData.previewKind === ""

                                                    Rectangle {
                                                        radius: 999
                                                        color: outgoing ? Qt.rgba(1, 1, 1, 0.14) : Theme.selectedBg
                                                        border.width: 1
                                                        border.color: outgoing ? Qt.rgba(1, 1, 1, 0.12) : Theme.selectedLine
                                                        implicitHeight: 24
                                                        implicitWidth: typeBadgeLabel.implicitWidth + 16

                                                        Text {
                                                            id: typeBadgeLabel
                                                            anchors.centerIn: parent
                                                            text: root.attachmentTypeLabel(modelData)
                                                            color: outgoing ? Theme.feedbackOutgoingText : Theme.text
                                                            font.pixelSize: 11
                                                            font.weight: Font.DemiBold
                                                        }
                                                    }

                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: modelData.name
                                                        color: outgoing ? Theme.feedbackOutgoingText : Theme.text
                                                        font.pixelSize: 13
                                                        font.weight: Font.DemiBold
                                                        wrapMode: Text.Wrap
                                                        maximumLineCount: 2
                                                        elide: Text.ElideRight
                                                    }

                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: modelData.sizeLabel
                                                        color: outgoing ? Theme.feedbackOutgoingText : Theme.weak
                                                        opacity: outgoing ? 0.78 : 1
                                                        font.pixelSize: 11
                                                        elide: Text.ElideRight
                                                    }

                                                    Item { Layout.fillHeight: true }

                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 8

                                                        ActionButton {
                                                            visible: modelData.previewKind === "document"
                                                            Layout.preferredWidth: visible ? 72 : 0
                                                            Layout.preferredHeight: 30
                                                            text: "预览"
                                                            onClicked: root.previewDocumentAttachment(modelData)
                                                        }

                                                        ActionButton {
                                                            visible: modelData.canDownload
                                                            Layout.preferredWidth: visible ? 72 : 0
                                                            Layout.preferredHeight: 30
                                                            text: "下载"
                                                            onClicked: if (viewModel) viewModel.saveAttachment(modelData.url, modelData.name)
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                MouseArea {
                                    id: bubbleContextArea
                                    anchors.fill: parent
                                    acceptedButtons: Qt.RightButton
                                    propagateComposedEvents: true
                                    onClicked: function(mouse) {
                                        messageContextMenu.popup(bubbleContextArea, mouse.x, mouse.y)
                                    }
                                }
                            }
                        }

                        Rectangle {
                            anchors.centerIn: parent
                            visible: !messageListView.count
                            width: root.boundedWidth(parent, 24, 340)
                            height: 120
                            radius: 18
                            color: Theme.card
                            border.width: 1
                            border.color: Theme.line

                            Column {
                                anchors.centerIn: parent
                                spacing: 8

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "还没有反馈记录"
                                    color: Theme.text
                                    font.pixelSize: 16
                                    font.weight: Font.DemiBold
                                }

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "发第一条消息，把问题、截图或日志发给开发者。"
                                    color: Theme.muted
                                    font.pixelSize: 12
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 18
                        color: Theme.card
                        border.width: 1
                        border.color: Theme.line
                        implicitHeight: composerColumn.implicitHeight + 24

                        ColumnLayout {
                            id: composerColumn
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 10

                            Flow {
                                Layout.fillWidth: true
                                spacing: 8
                                visible: viewModel && viewModel.pendingAttachments.length > 0

                                Repeater {
                                    model: viewModel ? viewModel.pendingAttachments : []

                                    delegate: Rectangle {
                                        radius: 999
                                        color: Theme.selectedBg
                                        border.width: 1
                                        border.color: Theme.selectedLine
                                        implicitHeight: 32
                                        implicitWidth: label.implicitWidth + 38

                                        Row {
                                            anchors.centerIn: parent
                                            spacing: 8

                                            Text {
                                                id: label
                                                text: modelData.name + " · " + modelData.sizeLabel
                                                color: Theme.text
                                                font.pixelSize: 12
                                            }

                                            Text {
                                                text: "×"
                                                color: Theme.muted
                                                font.pixelSize: 14

                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: if (viewModel) viewModel.removePendingAttachment(index)
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            TextArea {
                                id: draftMessageArea
                                Layout.fillWidth: true
                                Layout.preferredHeight: 120
                                placeholderText: "描述你遇到的问题、复现步骤、期望结果，或直接附上截图和日志..."
                                wrapMode: TextEdit.Wrap
                                color: Theme.text
                                selectionColor: Theme.blue
                                selectedTextColor: Theme.primaryText
                                Keys.onPressed: function(event) {
                                    if (inputMethodComposing) {
                                        return
                                    }
                                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                            && !event.isAutoRepeat
                                            && !(event.modifiers & Qt.ControlModifier)
                                            && !(event.modifiers & Qt.ShiftModifier)
                                            && !(event.modifiers & Qt.AltModifier)
                                            && !(event.modifiers & Qt.MetaModifier)) {
                                        event.accepted = true
                                        submitDraftMessage()
                                    }
                                }
                                background: Rectangle {
                                    radius: 14
                                    color: Theme.panel
                                    border.width: 1
                                    border.color: draftMessageArea.activeFocus ? Theme.blue : Theme.line
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true

                                ActionButton {
                                    Layout.preferredWidth: 110
                                    Layout.preferredHeight: 38
                                    text: "添加附件"
                                    onClicked: attachmentDialog.open()
                                }

                                ActionButton {
                                    Layout.preferredWidth: 92
                                    Layout.preferredHeight: 38
                                    text: "刷新"
                                    enabled: viewModel && viewModel.ready && !viewModel.busy
                                    onClicked: if (viewModel) viewModel.refresh()
                                }

                                Item { Layout.fillWidth: true }

                                ActionButton {
                                    Layout.preferredWidth: 128
                                    Layout.preferredHeight: 38
                                    text: viewModel && viewModel.sending ? "发送中..." : "发送反馈"
                                    enabled: viewModel && viewModel.ready && !viewModel.sending
                                    primary: true
                                    onClicked: submitDraftMessage()
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                radius: 22
                color: Theme.panel2
                border.width: 1
                border.color: Theme.line

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 16

                    Text {
                        text: "会话详情"
                        color: Theme.text
                        font.pixelSize: 20
                        font.weight: Font.Black
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 18
                        color: Theme.card
                        border.width: 1
                        border.color: Theme.line
                        implicitHeight: profileCardColumn.implicitHeight + 28

                        ColumnLayout {
                            id: profileCardColumn
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Text {
                                    text: "资料"
                                    color: Theme.muted
                                    font.pixelSize: 12
                                }

                                Item { Layout.fillWidth: true }

                                ActionButton {
                                    Layout.preferredWidth: 96
                                    Layout.preferredHeight: 30
                                    text: root.profileEditMode ? "取消" : "修改资料"
                                    enabled: viewModel && !viewModel.busy
                                    onClicked: {
                                        if (root.profileEditMode) {
                                            root.profileEditMode = false
                                            root.syncProfileDrafts()
                                            return
                                        }
                                        root.syncProfileDrafts()
                                        root.profileEditMode = true
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: !root.profileEditMode
                                text: viewModel ? viewModel.profileSummary : ""
                                color: Theme.text
                                font.pixelSize: 14
                                wrapMode: Text.Wrap
                            }

                            ThemedTextField {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 38
                                visible: root.profileEditMode
                                enabled: viewModel && !viewModel.busy
                                text: root.draftProfileNickname
                                placeholderText: "昵称，例如：李明 / 摄影组A机"
                                onTextEdited: root.draftProfileNickname = text
                            }

                            ThemedTextField {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 38
                                visible: root.profileEditMode
                                enabled: viewModel && !viewModel.busy
                                text: root.draftProfileContact
                                placeholderText: "联系方式，例如：微信 / 手机 / 邮箱"
                                onTextEdited: root.draftProfileContact = text
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: root.profileEditMode
                                text: "保存后会继续沿用当前反馈会话，后续发消息时会带上新的资料信息。"
                                color: Theme.muted
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }

                            ActionButton {
                                Layout.alignment: Qt.AlignRight
                                Layout.preferredWidth: 104
                                Layout.preferredHeight: 34
                                visible: root.profileEditMode
                                text: viewModel && viewModel.busy ? "保存中..." : "保存资料"
                                enabled: viewModel
                                    && !viewModel.busy
                                    && root.draftProfileNickname.trim().length > 0
                                    && root.draftProfileContact.trim().length > 0
                                primary: true
                                onClicked: root.submitProfileEdits()
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 18
                        color: Theme.card
                        border.width: 1
                        border.color: Theme.line
                        implicitHeight: 122

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 8

                            Text {
                                text: "项目上下文"
                                color: Theme.muted
                                font.pixelSize: 12
                            }

                            Text {
                                Layout.fillWidth: true
                                text: viewModel ? viewModel.projectSummary : ""
                                color: Theme.text
                                font.pixelSize: 13
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: 10

                        Repeater {
                            model: [
                                { title: "处理状态", value: viewModel ? viewModel.conversationStatusLabel : "" },
                                { title: "软件版本", value: viewModel ? viewModel.appVersionLabel : "" },
                                { title: "最近更新", value: viewModel ? viewModel.latestUpdatedAt : "" },
                                { title: "实时状态", value: viewModel ? viewModel.connectionStatus : "" }
                            ]

                            delegate: Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 92
                                radius: 18
                                color: Theme.card
                                border.width: 1
                                border.color: Theme.line

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 8

                                    Text {
                                        text: modelData.title
                                        color: Theme.muted
                                        font.pixelSize: 12
                                    }

                                    Text {
                                        width: root.safeWidth(parent)
                                        text: modelData.value
                                        color: Theme.text
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    ActionButton {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        text: viewModel && viewModel.sending ? "处理中..." : "清空会话窗口"
                        enabled: viewModel && viewModel.ready && !viewModel.sending
                        onClicked: clearConversationDialog.open()
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 18
                        color: Theme.selectedBg
                        border.width: 1
                        border.color: Theme.selectedLine
                        implicitHeight: 86

                        Column {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 8

                            Text {
                                text: "提醒"
                                color: Theme.text
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }

                            Text {
                                width: root.safeWidth(parent)
                                text: "若开发者在你离线时回复，本页在下次打开软件或重新联网后会自动补收历史消息。"
                                color: Theme.muted
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }
        }
    }
}
