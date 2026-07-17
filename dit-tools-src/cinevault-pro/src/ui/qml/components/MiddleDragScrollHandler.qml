import QtQuick

DragHandler {
    id: handler

    // ScrollView.contentItem is exposed as Item by Qt Controls even though the
    // runtime object is a Flickable. Keep these handles dynamic so qmllint does
    // not require an unsafe Item-to-Flickable assignment.
    property var flickable: null
    property var horizontalFlickable: flickable
    property var verticalFlickable: flickable
    property real anchorViewportX: 0
    property real anchorViewportY: 0
    property real dragDistanceX: 0
    property real dragDistanceY: 0
    property real deadZone: 10
    property real speedFactor: 0.24
    property real maximumStep: 48
    readonly property bool horizontalScrollable: horizontalFlickable !== null
        && horizontalFlickable.contentWidth > horizontalFlickable.width + 0.5
    readonly property bool verticalScrollable: verticalFlickable !== null
        && verticalFlickable.contentHeight > verticalFlickable.height + 0.5

    enabled: flickable !== null && (horizontalScrollable || verticalScrollable)
    target: null
    acceptedDevices: PointerDevice.Mouse
    acceptedButtons: Qt.MiddleButton
    dragThreshold: 0
    grabPermissions: PointerHandler.CanTakeOverFromAnything
    cursorShape: active ? Qt.SizeAllCursor : Qt.ArrowCursor

    function boundedContentX(targetFlickable, value) {
        if (!targetFlickable) {
            return 0
        }
        var minimum = targetFlickable.originX
        var maximum = Math.max(minimum,
                               minimum + targetFlickable.contentWidth - targetFlickable.width)
        return Math.max(minimum, Math.min(maximum, value))
    }

    function boundedContentY(targetFlickable, value) {
        if (!targetFlickable) {
            return 0
        }
        var minimum = targetFlickable.originY
        var maximum = Math.max(minimum,
                               minimum + targetFlickable.contentHeight - targetFlickable.height)
        return Math.max(minimum, Math.min(maximum, value))
    }

    function stepForDistance(distance) {
        var magnitude = Math.abs(distance) - deadZone
        if (magnitude <= 0) {
            return 0
        }
        return Math.sign(distance) * Math.min(maximumStep, magnitude * speedFactor)
    }

    function cancelFlicks() {
        if (horizontalFlickable) {
            horizontalFlickable.cancelFlick()
        }
        if (verticalFlickable && verticalFlickable !== horizontalFlickable) {
            verticalFlickable.cancelFlick()
        }
    }

    function returnTargetsToBounds() {
        if (horizontalFlickable) {
            horizontalFlickable.returnToBounds()
        }
        if (verticalFlickable && verticalFlickable !== horizontalFlickable) {
            verticalFlickable.returnToBounds()
        }
    }

    function scrollStep() {
        if (!active) {
            return
        }
        if (horizontalScrollable) {
            horizontalFlickable.contentX = boundedContentX(
                        horizontalFlickable,
                        horizontalFlickable.contentX + stepForDistance(dragDistanceX))
        }
        if (verticalScrollable) {
            verticalFlickable.contentY = boundedContentY(
                        verticalFlickable,
                        verticalFlickable.contentY + stepForDistance(dragDistanceY))
        }
    }

    onActiveChanged: {
        if (!flickable) {
            return
        }
        if (active) {
            cancelFlicks()
            var localAnchor = flickable.mapFromItem(null,
                                                    centroid.scenePosition.x,
                                                    centroid.scenePosition.y)
            anchorViewportX = Math.max(0, Math.min(flickable.width, localAnchor.x))
            anchorViewportY = Math.max(0, Math.min(flickable.height, localAnchor.y))
            dragDistanceX = 0
            dragDistanceY = 0
        } else {
            dragDistanceX = 0
            dragDistanceY = 0
            returnTargetsToBounds()
        }
    }

    onTranslationChanged: {
        if (!active) {
            return
        }
        dragDistanceX = activeTranslation.x
        dragDistanceY = activeTranslation.y
    }

    property Timer autoScrollTimer: Timer {
        interval: 16
        repeat: true
        running: handler.active
        onTriggered: handler.scrollStep()
    }

    property Item anchorIndicator: Item {
        parent: handler.flickable ? handler.flickable.contentItem : null
        width: 34
        height: 34
        x: handler.flickable
            ? handler.flickable.contentX + handler.anchorViewportX - width / 2
            : 0
        y: handler.flickable
            ? handler.flickable.contentY + handler.anchorViewportY - height / 2
            : 0
        visible: handler.active
        z: 1000000

        Rectangle {
            anchors.centerIn: parent
            width: 30
            height: 30
            radius: 15
            color: "#E620242B"
            border.width: 2
            border.color: "#F2F5F8"

            Rectangle {
                visible: handler.horizontalScrollable
                anchors.centerIn: parent
                width: 18
                height: 2
                radius: 1
                color: "#F2F5F8"
            }

            Rectangle {
                visible: handler.verticalScrollable
                anchors.centerIn: parent
                width: 2
                height: 18
                radius: 1
                color: "#F2F5F8"
            }

            Rectangle {
                anchors.centerIn: parent
                width: 6
                height: 6
                radius: 3
                color: "#7AA2FF"
            }
        }
    }
}
