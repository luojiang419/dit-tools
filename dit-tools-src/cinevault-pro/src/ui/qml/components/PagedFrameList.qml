pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

ListView {
    id: frameList

    property bool hasMore: false
    property bool loading: false
    property int remainingCount: 0
    property int totalFrameCount: 0
    property color statusColor: "#91a0b5"
    property bool requestPending: false
    property int anchorFrameNumber: -1
    property real anchorOffset: 0
    signal loadMoreRequested()

    clip: true
    reuseItems: true
    spacing: 8
    boundsBehavior: Flickable.StopAtBounds

    function requestNextPage() {
        if (!hasMore || loading || requestPending)
            return
        var index = indexAt(10, contentY + 1)
        if (index < 0)
            index = indexAt(10, contentY + spacing + 1)
        var item = index >= 0 ? itemAtIndex(index) : null
        anchorFrameNumber = index >= 0 && model ? model[index].frameNumber : -1
        anchorOffset = item ? contentY - item.y : 0
        requestPending = true
        loadMoreRequested()
    }

    function completePage(replaceCurrent, fromEnd) {
        Qt.callLater(function() {
            if (replaceCurrent) {
                if (fromEnd)
                    positionViewAtEnd()
                else
                    positionViewAtBeginning()
            } else if (anchorFrameNumber > 0 && model) {
                for (var index = 0; index < model.length; ++index) {
                    if (model[index].frameNumber === anchorFrameNumber) {
                        positionViewAtIndex(index, ListView.Beginning)
                        contentY += anchorOffset
                        break
                    }
                }
            }
            anchorFrameNumber = -1
            requestPending = false
        })
    }

    onAtYEndChanged: {
        if (atYEnd && !atYBeginning)
            requestNextPage()
    }
    onMovementEnded: {
        if (atYEnd)
            requestNextPage()
    }

    footer: Column {
        width: frameList.width
        spacing: 8
        visible: frameList.totalFrameCount > 0

        Text {
            width: parent.width
            text: frameList.loading ? "正在加载后续视频帧…"
                : (frameList.hasMore
                   ? "后面还有 " + frameList.remainingCount + " 帧，向下滚动继续浏览"
                   : "已到抽帧结果末尾 · 共 " + frameList.totalFrameCount + " 帧")
            color: frameList.statusColor
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }

        Button {
            objectName: "loadMoreFramesButton"
            width: parent.width
            height: visible ? 32 : 0
            visible: frameList.hasMore
            enabled: !frameList.loading
            text: frameList.loading ? "正在加载…" : "加载后续帧"
            onClicked: frameList.requestNextPage()
        }
    }
}
