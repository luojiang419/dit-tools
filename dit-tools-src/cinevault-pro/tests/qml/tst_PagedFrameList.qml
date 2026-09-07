import QtQuick
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

    PagedFrameList {
        id: frameList
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
            waitForRendering(frameList)
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
