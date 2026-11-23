import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtMultimedia
import CineVault

Rectangle {
    id: videoPreview

    property url sourceUrl: ""
    property url thumbnailUrl: ""
    property string title: ""
    property bool isVideo: false
    property bool compactMode: false
    property bool autoPrimePreviewFrame: false
    property bool inlineMuted: false
    property string clickAction: ""
    readonly property bool hasSource: isVideo && sourceUrl.toString().length > 0
    readonly property bool hasThumbnail: isVideo && thumbnailUrl.toString().length > 0
    readonly property bool isPlaying: previewPlaybackRequested || mediaPlayer.playbackState === MediaPlayer.PlayingState
    readonly property bool isFullscreenPlaying: fullScreenPlayer.playbackState === MediaPlayer.PlayingState
    readonly property bool hasPreviewFrame: previewPlaybackRequested
        || previewFrameRequested
        || mediaPlayer.playbackState === MediaPlayer.PlayingState
        || mediaPlayer.position > 0
    readonly property bool hasFullscreenFrame: fullscreenFrameRequested
        || fullScreenPlayer.playbackState === MediaPlayer.PlayingState
        || fullScreenPlayer.position > 0
    readonly property bool showThumbnailPoster: hasThumbnail && !hasPreviewFrame
    readonly property bool showFullscreenPoster: hasThumbnail && !hasFullscreenFrame
    readonly property bool frameNavigationActive: hasSource
        && (fullScreenWindow.visibility === Window.FullScreen
            || previewPlaybackRequested
            || previewFrameRequested
            || mediaPlayer.playbackState === MediaPlayer.PlayingState
            || mediaPlayer.position > 0)
    property bool previewPlaybackRequested: false
    property bool previewFrameRequested: false
    property bool fullscreenFrameRequested: false
    property int pendingFullscreenPosition: 0
    property bool pendingFullscreenPlay: false
    property bool pendingFullscreenOpen: false

    function formatTime(milliseconds) {
        if (!milliseconds || milliseconds < 0) {
            return "00:00"
        }
        var totalSeconds = Math.floor(milliseconds / 1000)
        var hours = Math.floor(totalSeconds / 3600)
        var minutes = Math.floor((totalSeconds % 3600) / 60)
        var seconds = totalSeconds % 60
        var mm = minutes < 10 ? "0" + minutes : "" + minutes
        var ss = seconds < 10 ? "0" + seconds : "" + seconds
        return hours > 0 ? hours + ":" + mm + ":" + ss : mm + ":" + ss
    }

    function frameDurationMs(player) {
        var fallback = 40
        if (!player || !player.metaData || typeof player.metaData.value !== "function") {
            return fallback
        }

        var fps = Number(player.metaData.value(17))
        if ((!isFinite(fps) || fps <= 0) && typeof player.metaData.stringValue === "function") {
            var match = String(player.metaData.stringValue(17)).match(/([0-9]+(\.[0-9]+)?)/)
            fps = match ? Number(match[1]) : 0
        }

        return isFinite(fps) && fps > 0 ? Math.max(1, Math.round(1000 / fps)) : fallback
    }

    function resetPlayback() {
        mediaPlayer.stop()
        previewPlaybackRequested = false
        previewFrameRequested = false
        fullscreenFrameRequested = false
        previewSeekPauseTimer.stop()
        pendingFullscreenPosition = 0
        pendingFullscreenPlay = false
        pendingFullscreenOpen = false
        fullscreenPrimePauseTimer.stop()
        if (fullScreenWindow.visibility !== Window.Hidden) {
            closeFullscreen(false)
        }
        fullScreenPlayer.stop()
        fullScreenPlayer.source = ""
    }

    function togglePlayback() {
        if (!hasSource) {
            return
        }
        if (isPlaying) {
            mediaPlayer.pause()
            previewPlaybackRequested = false
            previewFrameRequested = true
        } else {
            previewPlaybackRequested = true
            mediaPlayer.play()
        }
    }

    function toggleFullscreenPlayback() {
        if (!hasSource) {
            return
        }
        if (isFullscreenPlaying) {
            fullScreenPlayer.pause()
            fullscreenFrameRequested = true
        } else {
            fullscreenFrameRequested = true
            fullScreenPlayer.play()
        }
    }

    function seek(positionMs) {
        if (hasSource && mediaPlayer.duration > 0) {
            var targetPosition = Math.max(0, Math.min(positionMs, mediaPlayer.duration))
            mediaPlayer.position = targetPosition
            previewFrameRequested = true
            if (!previewPlaybackRequested && mediaPlayer.playbackState !== MediaPlayer.PlayingState) {
                mediaPlayer.play()
                previewSeekPauseTimer.restart()
            }
        }
    }

    function seekFullscreen(positionMs) {
        if (hasSource && fullScreenPlayer.duration > 0) {
            fullScreenPlayer.position = Math.max(0, Math.min(positionMs, fullScreenPlayer.duration))
            fullscreenFrameRequested = true
        }
    }

    function stepPreviewFrame(direction) {
        if (!hasSource || mediaPlayer.duration <= 0) {
            return false
        }

        var targetPosition = Math.max(0, Math.min(mediaPlayer.position + direction * frameDurationMs(mediaPlayer), mediaPlayer.duration))
        previewPlaybackRequested = false
        previewFrameRequested = true
        previewSeekPauseTimer.stop()
        mediaPlayer.position = targetPosition
        mediaPlayer.pause()
        return true
    }

    function stepFullscreenFrame(direction) {
        if (!hasSource || fullScreenPlayer.duration <= 0) {
            return false
        }

        var targetPosition = Math.max(0, Math.min(fullScreenPlayer.position + direction * frameDurationMs(fullScreenPlayer), fullScreenPlayer.duration))
        fullscreenFrameRequested = true
        fullScreenPlayer.position = targetPosition
        fullScreenPlayer.pause()
        return true
    }

    function stepFrameFromKeyboard(direction) {
        return fullScreenWindow.visibility === Window.FullScreen
            ? stepFullscreenFrame(direction)
            : stepPreviewFrame(direction)
    }

    function primeFullscreenPlayback() {
        if (fullScreenWindow.visibility !== Window.FullScreen || !hasSource) {
            pendingFullscreenPlay = false
            return
        }

        if (pendingFullscreenPosition > 0 && fullScreenPlayer.duration > 0) {
            fullScreenPlayer.position = Math.min(pendingFullscreenPosition, fullScreenPlayer.duration)
            fullscreenFrameRequested = true
            pendingFullscreenPosition = 0
        }

        if (pendingFullscreenPlay) {
            pendingFullscreenPlay = false
            fullscreenFrameRequested = true
            fullScreenPlayer.play()
        }
    }

    function openFullscreen() {
        if (!hasSource) {
            return
        }
        pendingFullscreenPosition = mediaPlayer.position
        pendingFullscreenPlay = previewPlaybackRequested || mediaPlayer.playbackState === MediaPlayer.PlayingState
        pendingFullscreenOpen = pendingFullscreenPosition <= 0 && !pendingFullscreenPlay
        fullscreenFrameRequested = pendingFullscreenPosition > 0 || pendingFullscreenPlay
        mediaPlayer.pause()
        previewPlaybackRequested = false
        previewFrameRequested = true
        fullScreenPlayer.stop()
        fullScreenPlayer.source = ""
        fullScreenPlayer.source = sourceUrl
        if (pendingFullscreenOpen) {
            fullscreenPrimePauseTimer.stop()
            fullScreenPlayer.play()
            return
        }
        fullScreenWindow.visibility = Window.FullScreen
        fullScreenRoot.forceActiveFocus()
        Qt.callLater(primeFullscreenPlayback)
    }

    function closeFullscreen(syncToPreview) {
        var shouldSync = syncToPreview !== false
        var fullscreenWasPlaying = fullScreenPlayer.playbackState === MediaPlayer.PlayingState
        var fullscreenPosition = fullScreenPlayer.position
        fullScreenWindow.visibility = Window.Hidden
        if (shouldSync && hasSource) {
            if (fullscreenPosition > 0 && mediaPlayer.duration > 0) {
                mediaPlayer.position = Math.min(fullscreenPosition, mediaPlayer.duration)
            }
            if (fullscreenWasPlaying) {
                previewPlaybackRequested = true
                previewFrameRequested = false
                mediaPlayer.play()
            } else {
                previewPlaybackRequested = false
                previewFrameRequested = true
                mediaPlayer.pause()
            }
        }
        fullScreenPlayer.stop()
        fullScreenPlayer.source = ""
        fullscreenFrameRequested = false
        pendingFullscreenPosition = 0
        pendingFullscreenPlay = false
        pendingFullscreenOpen = false
        fullscreenPrimePauseTimer.stop()
    }

    function primePreviewFrame() {
        if (!hasSource || hasPreviewFrame) {
            return
        }
        previewPlaybackRequested = false
        previewFrameRequested = true
        mediaPlayer.play()
        previewSeekPauseTimer.restart()
    }

    function triggerClickAction() {
        if (!hasSource) {
            return
        }
        if (clickAction === "fullscreen") {
            openFullscreen()
        } else if (clickAction === "togglePlayback") {
            togglePlayback()
        }
    }

    radius: 16
    color: Theme.mediaSurface
    border.width: 1
    border.color: Theme.line
    clip: true

    onHasSourceChanged: {
        resetPlayback()
        if (hasSource && autoPrimePreviewFrame && !hasThumbnail) {
            Qt.callLater(primePreviewFrame)
        }
    }
    onSourceUrlChanged: {
        resetPlayback()
        if (hasSource && autoPrimePreviewFrame && !hasThumbnail) {
            Qt.callLater(primePreviewFrame)
        }
    }

    MediaPlayer {
        id: mediaPlayer
        source: videoPreview.hasSource ? videoPreview.sourceUrl : ""
        audioOutput: AudioOutput {
            id: previewAudioOutput
            muted: videoPreview.inlineMuted
        }
        videoOutput: previewOutput

        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.EndOfMedia || mediaStatus === MediaPlayer.InvalidMedia) {
                videoPreview.previewPlaybackRequested = false
                videoPreview.previewFrameRequested = false
                previewSeekPauseTimer.stop()
            }
        }

        onPlaybackStateChanged: {
            if (playbackState === MediaPlayer.PlayingState && videoPreview.previewFrameRequested
                    && !videoPreview.previewPlaybackRequested) {
                previewSeekPauseTimer.restart()
            }
        }
    }

    Timer {
        id: previewSeekPauseTimer
        interval: 120
        repeat: false
        onTriggered: {
            if (videoPreview.previewFrameRequested && !videoPreview.previewPlaybackRequested
                    && mediaPlayer.playbackState === MediaPlayer.PlayingState) {
                mediaPlayer.pause()
            }
        }
    }

    VideoOutput {
        id: previewOutput
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectFit
    }

    Image {
        id: thumbnailPoster
        anchors.fill: parent
        source: videoPreview.thumbnailUrl
        visible: videoPreview.showThumbnailPoster
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: false
    }

    Rectangle {
        anchors.fill: parent
        visible: thumbnailPoster.visible
        color: Qt.rgba(0, 0, 0, 0.18)
    }

    MouseArea {
        anchors.fill: parent
        anchors.bottomMargin: controlBar.visible ? controlBar.height : 0
        enabled: videoPreview.hasSource && (videoPreview.clickAction === "fullscreen" || videoPreview.clickAction === "togglePlayback")
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: videoPreview.triggerClickAction()
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        visible: !videoPreview.hasSource && !videoPreview.hasThumbnail

        Column {
            anchors.centerIn: parent
            width: parent.width - 32
            spacing: 6

            Text {
                width: parent.width
                text: videoPreview.isVideo ? "该视频暂无可播放路径" : "选择视频素材预览"
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: videoPreview.isVideo ? videoPreview.title : "素材库和质检页会联动显示"
                color: Theme.muted
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 10
        width: Math.min(parent.width - 20, 156)
        height: 30
        radius: 8
        color: Qt.rgba(0.03, 0.04, 0.06, 0.72)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.12)
        visible: videoPreview.hasThumbnail && !videoPreview.compactMode

        Text {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            text: videoPreview.title.length > 0 ? videoPreview.title : "视频素材"
            color: Theme.text
            font.pixelSize: 12
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }

    Rectangle {
        id: controlBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: videoPreview.compactMode ? 54 : 70
        color: Qt.rgba(0.04, 0.05, 0.08, 0.82)
        visible: videoPreview.hasSource

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: videoPreview.compactMode ? 6 : 8
            spacing: videoPreview.compactMode ? 2 : 4

            ThemedSlider {
                Layout.fillWidth: true
                Layout.preferredHeight: videoPreview.compactMode ? 18 : 24
                from: 0
                to: Math.max(mediaPlayer.duration, 1)
                value: mediaPlayer.position
                enabled: mediaPlayer.duration > 0
                onMoved: videoPreview.seek(value)
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: videoPreview.compactMode ? 24 : 30
                spacing: 8

                ActionButton {
                    Layout.preferredWidth: videoPreview.compactMode ? 54 : 58
                    Layout.preferredHeight: videoPreview.compactMode ? 24 : 30
                    text: videoPreview.isPlaying ? "暂停" : "播放"
                    enabled: videoPreview.hasSource
                    primary: videoPreview.isPlaying
                    onClicked: videoPreview.togglePlayback()
                }

                Text {
                    Layout.fillWidth: !videoPreview.compactMode
                    visible: !videoPreview.compactMode
                    text: videoPreview.title.length > 0 ? videoPreview.title : "视频预览"
                    color: Theme.text
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }

                Text {
                    visible: !videoPreview.compactMode
                    Layout.preferredWidth: 92
                    text: videoPreview.formatTime(mediaPlayer.position) + " / " + videoPreview.formatTime(mediaPlayer.duration)
                    color: Theme.muted
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                }

                ActionButton {
                    Layout.preferredWidth: 58
                    Layout.preferredHeight: 30
                    text: "全屏"
                    enabled: videoPreview.hasSource
                    onClicked: videoPreview.openFullscreen()
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0.35, 0.08, 0.08, 0.86)
        visible: mediaPlayer.error !== MediaPlayer.NoError

        Text {
            anchors.centerIn: parent
            width: parent.width - 28
            text: mediaPlayer.errorString.length > 0 ? mediaPlayer.errorString : "视频无法播放"
            color: Theme.text
            font.pixelSize: 12
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
    }

    Window {
        id: fullScreenWindow
        visibility: Window.Hidden
        color: "black"
        title: videoPreview.title.length > 0 ? videoPreview.title : "视频预览"

        onVisibilityChanged: {
            if (visibility === Window.FullScreen) {
                fullScreenRoot.forceActiveFocus()
            }
        }

        onClosing: function(close) {
            close.accepted = false
            videoPreview.closeFullscreen()
        }

        Shortcut {
            sequence: "Space"
            context: Qt.WindowShortcut
            autoRepeat: false
            enabled: fullScreenWindow.visibility === Window.FullScreen && videoPreview.hasSource
            onActivated: videoPreview.toggleFullscreenPlayback()
        }

        MediaPlayer {
            id: fullScreenPlayer
            audioOutput: AudioOutput {
                id: fullScreenAudioOutput
                muted: videoPreview.pendingFullscreenOpen || fullScreenWindow.visibility !== Window.FullScreen
            }
            videoOutput: fullScreenOutput

            onMediaStatusChanged: {
                if (videoPreview.pendingFullscreenOpen
                        && (mediaStatus === MediaPlayer.LoadedMedia
                            || mediaStatus === MediaPlayer.BufferedMedia
                            || mediaStatus === MediaPlayer.BufferingMedia)) {
                    fullscreenPrimePauseTimer.restart()
                } else if (mediaStatus === MediaPlayer.LoadedMedia
                        || mediaStatus === MediaPlayer.BufferedMedia
                        || mediaStatus === MediaPlayer.BufferingMedia) {
                    videoPreview.primeFullscreenPlayback()
                } else if (mediaStatus === MediaPlayer.InvalidMedia) {
                    videoPreview.pendingFullscreenOpen = false
                    fullscreenPrimePauseTimer.stop()
                    videoPreview.fullscreenFrameRequested = false
                }
            }

            onPlaybackStateChanged: {
                if (videoPreview.pendingFullscreenOpen && playbackState === MediaPlayer.PlayingState) {
                    fullscreenPrimePauseTimer.restart()
                } else if (playbackState === MediaPlayer.PlayingState) {
                    videoPreview.fullscreenFrameRequested = true
                }
            }
        }

        Timer {
            id: fullscreenPrimePauseTimer
            interval: 120
            repeat: false
            onTriggered: {
                if (!videoPreview.pendingFullscreenOpen) {
                    return
                }
                videoPreview.pendingFullscreenOpen = false
                videoPreview.fullscreenFrameRequested = true
                fullScreenPlayer.pause()
                fullScreenWindow.visibility = Window.FullScreen
                fullScreenRoot.forceActiveFocus()
            }
        }

        Item {
            id: fullScreenRoot
            anchors.fill: parent
            focus: true

            Keys.onEscapePressed: videoPreview.closeFullscreen()
            Keys.onPressed: function(event) {
                if (event.isAutoRepeat) {
                    return
                }

                if (event.key === Qt.Key_Left) {
                    event.accepted = videoPreview.stepFullscreenFrame(-1)
                } else if (event.key === Qt.Key_Right) {
                    event.accepted = videoPreview.stepFullscreenFrame(1)
                }
            }

            VideoOutput {
                id: fullScreenOutput
                anchors.fill: parent
                fillMode: VideoOutput.PreserveAspectFit
            }

            Image {
                anchors.fill: parent
                source: videoPreview.thumbnailUrl
                visible: videoPreview.showFullscreenPoster
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 82
                color: Qt.rgba(0, 0, 0, 0.72)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6

                    ThemedSlider {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 24
                        from: 0
                        to: Math.max(fullScreenPlayer.duration, 1)
                        value: fullScreenPlayer.position
                        enabled: fullScreenPlayer.duration > 0
                        onMoved: videoPreview.seekFullscreen(value)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ActionButton {
                            Layout.preferredWidth: 68
                            text: videoPreview.isFullscreenPlaying ? "暂停" : "播放"
                            enabled: videoPreview.hasSource
                            primary: videoPreview.isFullscreenPlaying
                            onClicked: videoPreview.toggleFullscreenPlayback()
                        }

                        Text {
                            Layout.fillWidth: true
                            text: videoPreview.title
                            color: Theme.text
                            font.pixelSize: 13
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }

                        Text {
                            Layout.preferredWidth: 112
                            text: videoPreview.formatTime(fullScreenPlayer.position) + " / " + videoPreview.formatTime(fullScreenPlayer.duration)
                            color: Theme.muted
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                        }

                        ActionButton {
                            Layout.preferredWidth: 68
                            text: "退出"
                            enabled: videoPreview.hasSource
                            onClicked: videoPreview.closeFullscreen()
                        }
                    }
                }
            }
        }
    }
}
