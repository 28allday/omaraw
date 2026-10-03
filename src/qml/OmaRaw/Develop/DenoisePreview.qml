import QtQuick
import OmaRaw.Ui
import "../Ui/PhotoZoom.js" as PhotoZoom

Column {
    id: root
    required property var denoise
    property real displayScale: 1
    property bool showAfter: true
    property real viewHeight: 350
    property real percent: 0
    property bool active: true
    readonly property var area: denoise.previewArea || ({})
    readonly property real fitScale: area.width > 0 && area.height > 0 && view.width > 0 && view.height > 0
        ? Math.min(view.width / area.width, view.height / area.height) : 1
    readonly property real imageScale: percent > 0 ? percent / (100 * displayScale) : fitScale
    readonly property bool hasImage: crop.status === Image.Ready && area.frameWidth > 0 && area.frameHeight > 0
    property bool positioned: false
    spacing: Theme.s2
    onActiveChanged: {
        if (active && !positioned) Qt.callLater(centreOnArea)
        else if (!active) refresh.stop()
    }
    onAreaChanged: {
        if (!(area.frameWidth > 0)) { positioned = false; percent = 0; refresh.stop() }
        else if (active && !positioned) Qt.callLater(centreOnArea)
    }
    function centreOnArea() {
        // The popup can open before its content has been laid out. Wait for
        // usable dimensions before positioning the first detail tile.
        if (!active || !(area.frameWidth > 0) || view.width <= 0 || view.height <= 0 || positioned) return
        view.contentX = Math.max(0, (area.x + area.width / 2) * imageScale - view.width / 2)
        view.contentY = Math.max(0, (area.y + area.height / 2) * imageScale - view.height / 2)
        positioned = true
    }
    function zoomTo(value, point) {
        if (!hasImage) return
        const where = point || Qt.point(view.width / 2, view.height / 2)
        const anchor = PhotoZoom.anchor(view, world, where)
        percent = value
        PhotoZoom.restore(view, world, where, anchor)
        refresh.restart()
    }
    function requestArea() {
        if (!active || !hasImage || view.moving) return
        // Reuse the current tile while the whole viewport still fits inside
        // it. Moving beyond it requests only the latest settled position.
        const x = (view.contentX + view.width / 2 - world.x) / imageScale
        const y = (view.contentY + view.height / 2 - world.y) / imageScale
        const halfW = view.width / (2 * imageScale), halfH = view.height / (2 * imageScale)
        if (x-halfW >= area.x && x+halfW <= area.x+area.width
            && y-halfH >= area.y && y+halfH <= area.y+area.height) return
        denoise.previewAt(x / area.frameWidth, y / area.frameHeight)
    }
    Timer { id: refresh; interval: 220; onTriggered: root.requestArea() }
    Row {
        spacing: Theme.s2
        SegmentedControl {
            objectName: "aiDenoiseZoom"
            labels: [qsTr("Fit"), qsTr("100%"), qsTr("200%")]
            currentIndex: root.percent === 0 ? 0 : root.percent === 100 ? 1 : root.percent === 200 ? 2 : -1
            onActivated: i => root.zoomTo([0, 100, 200][i])
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: Math.round(root.imageScale * root.displayScale * 100) + "%"
            color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        }
    }
    Rectangle {
        width: parent.width; height: root.viewHeight; color: "#181818"
        Flickable {
            id: view; objectName: "aiDenoiseViewport"
            anchors.fill: parent; clip: true; boundsBehavior: Flickable.StopAtBounds
            contentWidth: Math.max(width, world.width); contentHeight: Math.max(height, world.height)
            interactive: root.hasImage
            onWidthChanged: if (!root.positioned) Qt.callLater(root.centreOnArea)
            onHeightChanged: if (!root.positioned) Qt.callLater(root.centreOnArea)
            onMovementStarted: refresh.stop()
            onMovementEnded: refresh.restart()
            Item {
                id: world
                width: (root.area.frameWidth || 0) * root.imageScale
                height: (root.area.frameHeight || 0) * root.imageScale
                x: Math.max(0, (view.width-width)/2); y: Math.max(0, (view.height-height)/2)
                Image {
                    id: crop; objectName: "aiDenoiseCrop"
                    source: root.showAfter ? root.denoise.after : root.denoise.before
                    x: (root.area.x || 0) * root.imageScale; y: (root.area.y || 0) * root.imageScale
                    width: (root.area.width || implicitWidth) * root.imageScale
                    height: (root.area.height || implicitHeight) * root.imageScale
                    smooth: root.imageScale * root.displayScale < 1; cache: false
                }
            }
            TapHandler {
                enabled: root.hasImage
                onDoubleTapped: root.zoomTo(root.percent === 0 ? 100 : 0)
            }
            HoverHandler { cursorShape: view.dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor }
        }
        WheelHandler {
            parent: view; target: null
            enabled: root.hasImage
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedModifiers: Qt.KeyboardModifierMask
            onWheel: event => {
                const steps = PhotoZoom.steps(event)
                if (!steps) { event.accepted = false; return }
                const point = view.mapFromItem(view.contentItem, event.x, event.y)
                let scale
                if (event.modifiers & Qt.ControlModifier) {
                    const stops = [root.fitScale, 1/root.displayScale, 2/root.displayScale, 4/root.displayScale, 8/root.displayScale].sort((a,b) => a-b)
                    scale = steps > 0 ? stops.find(s => s > root.imageScale + .000001)
                                      : stops.reverse().find(s => s < root.imageScale - .000001)
                    if (scale === undefined) return
                } else scale = PhotoZoom.nextScale(root.imageScale, steps, Math.min(root.fitScale, 1/root.displayScale),
                    Math.max(root.fitScale, 8/root.displayScale), root.fitScale)
                root.zoomTo(Math.abs(scale-root.fitScale) < .000001 ? 0 : scale*root.displayScale*100, point)
            }
        }
        Text {
            anchors.centerIn: parent; width: parent.width - Theme.s4; wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter; textFormat: Text.PlainText
            visible: !root.hasImage
            text: root.denoise.status || qsTr("Preparing the detail preview…")
            color: "white"; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        }
        Text {
            anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: Theme.s2
            visible: root.hasImage && root.denoise.busy
            text: qsTr("Updating detail…")
            color: "white"; style: Text.Outline; styleColor: "#181818"
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        }
    }
    Text {
        width: parent.width; wrapMode: Text.WordWrap
        text: qsTr("Drag to move through the photo. Scroll to zoom; double-click for Fit / 100%. The detail refreshes when you stop.")
        color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
    }
}
