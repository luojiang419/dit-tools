import QtQuick
import QtQuick.Controls
import QtTest
import "../../src/ui/qml/components"

Item {
    width: 360
    height: 260

    property int total: 168
    property int cursor: 24
    property int requests: 0
    property bool holdResponse: false

    function rows(first, last) {
        var values = []
        for (var i = first; i <= last; ++i)
            values.push({frameNumber: i})
        return values
    }

    Timer {
        id: response
        interval: 20
        onTriggered: {
            cursor = Math.min(total, cursor + 24)
            frameList.model = rows(Math.max(1, cursor - 239), cursor)
            frameList.hasMore = cursor < total
            frameList.remainingCount = total - cursor
            frameList.loading = false
            frameList.completePage(false, false)
        }
    }

    ScrollView {
        id: detailScroll
        width: 360
        height: 260
        contentWidth: availableWidth
        contentHeight: 400

        MiddleDragScrollHandler {
            parent: detailScroll.contentItem
            flickable: detailScroll.contentItem
        }

        PagedFrameList {
            id: frameList
            y: 80
            width: 340
            height: 240
            totalFrameCount: total
            delegate: Text {
                required property var modelData
                width: 300
                height: 44
                text: "Frame " + modelData.frameNumber
            }
            onLoadMoreRequested: {
                ++requests
                loading = true
                if (!holdResponse)
                    response.start()
            }
        }
    }

    TestCase {
        name: "PagedFrameList"
        when: windowShown

        function init() {
            response.stop()
            frameList.loading = true
            frameList.hasMore = false
            frameList.requestPending = true
            frameList.anchorFrameNumber = -1
            total = 168
            cursor = 24
            requests = 0
            holdResponse = false
            frameList.model = rows(1, 24)
            frameList.remainingCount = total - cursor
            frameList.positionViewAtBeginning()
            frameList.hasMore = true
            frameList.loading = false
            frameList.completePage(true, false)
            tryCompare(frameList, "requestPending", false)
            detailScroll.contentItem.contentY = 80
            waitForRendering(frameList)
        }

        function test_middleHoldCrossesPagesAndWindowBudget() {
            total = 505
            var outerY = detailScroll.contentItem.contentY
            mousePress(frameList, 120, 20, Qt.MiddleButton)
            try {
                mouseMove(frameList, 120, 22, 20, Qt.MiddleButton)
                mouseMove(frameList, 120, 220, 20, Qt.MiddleButton)
                // Keep the pointer stationary with the button held: no re-press
                // or extra movement may be needed after a page/model replacement.
                tryVerify(function() { return cursor === total }, 20000)
                tryCompare(frameList, "requestPending", false)
                tryCompare(frameList, "atYEnd", true)
                compare(frameList.model[frameList.count - 1].frameNumber, 505)
                verify(frameList.count <= 240)
                compare(detailScroll.contentItem.contentY, outerY)
            } finally {
                mouseRelease(frameList, 120, 220, Qt.MiddleButton)
            }
        }

        function test_middleReleaseStopsAndReverseScrolls() {
            mousePress(frameList, 120, 20, Qt.MiddleButton)
            try {
                mouseMove(frameList, 120, 22, 20, Qt.MiddleButton)
                mouseMove(frameList, 120, 180, 20, Qt.MiddleButton)
                tryVerify(function() { return frameList.contentY > 300 })
            } finally {
                mouseRelease(frameList, 120, 180, Qt.MiddleButton)
            }
            wait(60)
            var stoppedY = frameList.contentY
            wait(120)
            compare(frameList.contentY, stoppedY)
            mousePress(frameList, 120, 220, Qt.MiddleButton)
            try {
                mouseMove(frameList, 120, 218, 20, Qt.MiddleButton)
                mouseMove(frameList, 120, 40, 20, Qt.MiddleButton)
                tryVerify(function() { return frameList.contentY < stoppedY - 100 })
            } finally {
                mouseRelease(frameList, 120, 40, Qt.MiddleButton)
            }
        }

        function test_middleOutsideFramesScrollsDetails() {
            detailScroll.contentItem.contentY = 0
            mousePress(detailScroll, 120, 10, Qt.MiddleButton)
            try {
                mouseMove(detailScroll, 120, 12, 20, Qt.MiddleButton)
                mouseMove(detailScroll, 120, 65, 20, Qt.MiddleButton)
                tryVerify(function() { return detailScroll.contentItem.contentY > 50 })
                compare(frameList.contentY, 0)
            } finally {
                mouseRelease(detailScroll, 120, 65, Qt.MiddleButton)
            }
        }

        function test_wheelLoadsBeyondFirstPage() {
            mouseWheel(frameList, 120, 150, 0, -4800)
            tryVerify(function() { return cursor > 24 }, 3000)
            tryCompare(frameList, "requestPending", false)
            verify(frameList.contentY > 0, "Appending a page must preserve the reading position")
        }

        function test_continuesPastWindowBudgetToRealEnd() {
            total = 505
            while (cursor < total) {
                var expected = Math.min(total, cursor + 24)
                frameList.positionViewAtEnd()
                tryCompare(parent, "cursor", expected)
                tryCompare(frameList, "requestPending", false)
                verify(frameList.count <= 240)
            }
            compare(frameList.model[frameList.count - 1].frameNumber, 505)
            compare(frameList.remainingCount, 0)
            compare(frameList.hasMore, false)
        }

        function test_oneRequestWhileLoadingAndRetryAfterFailure() {
            holdResponse = true
            frameList.requestNextPage()
            frameList.requestNextPage()
            frameList.requestNextPage()
            compare(requests, 1)
            frameList.loading = false
            frameList.completePage(false, false)
            tryCompare(frameList, "requestPending", false)
            frameList.requestNextPage()
            compare(requests, 2)
        }

        function test_endAndBeginningNavigation() {
            frameList.loading = true
            frameList.hasMore = false
            frameList.model = rows(145, 168)
            frameList.loading = false
            frameList.completePage(true, true)
            tryCompare(frameList, "atYEnd", true)
            compare(requests, 0)
            frameList.loading = true
            frameList.model = rows(1, 24)
            frameList.loading = false
            frameList.hasMore = true
            frameList.completePage(true, false)
            tryCompare(frameList, "atYBeginning", true)
            compare(requests, 0)
        }
    }
}
