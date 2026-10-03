pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// Retouch spots over the render (sized to the image item it sits on):
// the spot as a solid circle, its feather as a fainter ring, and for
// heal and clone the source circle joined to it by a line. Both circles
// drag; a press on either makes the spot the active one.
Item {
    id: root
    readonly property real edge: Math.min(width, height)
    property rect viewport: Qt.rect(0, 0, width, height)
    StableList { id: spotList; source: engine.spots; key: (entry, index) => engine.imageId + ":" + index }

    Repeater {
        model: spotList.model
        Item {
            id: spot
            required property int index
            required property var modelData
            readonly property var live: engine.spots[index] || modelData
            readonly property bool active: engine.activeSpot === index
            readonly property bool sourced: live.algorithm === 1 || live.algorithm === 2
            readonly property real baseCx: (live.displayCx !== undefined ? live.displayCx : live.cx) * root.width
            readonly property real baseCy: (live.displayCy !== undefined ? live.displayCy : live.cy) * root.height
            readonly property real baseSx: (live.displaySx !== undefined ? live.displaySx : live.sx) * root.width
            readonly property real baseSy: (live.displaySy !== undefined ? live.displaySy : live.sy) * root.height
            // Keep drawing responsive while the handle moves. Commit one native
            // edit on release; read-back updates the same delegate and cannot
            // steal the next gesture while the previous edit finishes.
            property var dragging: null
            property var dragStart: null
            readonly property real cx: dragging ? dragging.cx : baseCx
            readonly property real cy: dragging ? dragging.cy : baseCy
            readonly property real sx: dragging ? dragging.sx : baseSx
            readonly property real sy: dragging ? dragging.sy : baseSy
            function startDrag(px, py) {
                dragStart = {cx: cx, cy: cy, sx: sx, sy: sy, x: px, y: py}
                dragging = {cx: cx, cy: cy, sx: sx, sy: sy}
                engine.activeSpot = index
            }
            function moveDrag(px, py, source) {
                if (!dragStart) return
                const d = dragStart
                const x = Math.max(0, Math.min(root.width, (source ? d.sx : d.cx) + px - d.x))
                const y = Math.max(0, Math.min(root.height, (source ? d.sy : d.cy) + py - d.y))
                dragging = source ? {cx: d.cx, cy: d.cy, sx: x, sy: y}
                                  : {cx: x, cy: y, sx: d.sx + x - d.cx, sy: d.sy + y - d.cy}
            }
            function commitDrag() {
                if(!dragging || !dragStart) return
                const d = dragStart
                dragStart = null
                if(Math.abs(cx-d.cx)<0.1 && Math.abs(cy-d.cy)<0.1 && Math.abs(sx-d.sx)<0.1 && Math.abs(sy-d.sy)<0.1) {
                    dragging = null
                    return
                }
                engine.toolAction("retouch", "move", {index: index, x: cx/root.width, y: cy/root.height, sx: sx/root.width, sy: sy/root.height})
            }
            onLiveChanged: if (!dragStart) dragging = null
            onVisibleChanged: if (!visible) { dragStart = null; dragging = null }
            readonly property var points: live.displayPoints || []
            readonly property real r: live.radius * root.edge
            readonly property color line: active ? Theme.accent : Qt.rgba(1, 1, 1, 0.75)
            anchors.fill: parent
            StrokeGuide {
                objectName: "retouchStrokeGuide"
                viewport: root.viewport
                points: spot.points
                shiftX: spot.cx - spot.baseCx; shiftY: spot.cy - spot.baseCy
                penColor: spot.line
                penWidth: spot.live.shape === 3 ? Math.max(2, spot.r*2) : 2
                closed: spot.live.shape === 4
            }

            // source → spot
            Rectangle {
                visible: spot.sourced
                x: spot.sx; y: spot.sy - 0.5
                width: Math.hypot(spot.cx - spot.sx, spot.cy - spot.sy); height: 1
                color: spot.line; opacity: 0.6
                transformOrigin: Item.Left
                rotation: Math.atan2(spot.cy - spot.sy, spot.cx - spot.sx) * 180 / Math.PI
            }
            // the spot and its feather
            Rectangle {
                x: spot.cx - spot.r; y: spot.cy - spot.r
                width: spot.r * 2; height: width; radius: width / 2
                color: "transparent"; border.width: spot.active ? 2 : 1; border.color: spot.line
            }
            Rectangle {
                visible: spot.live.border > 0
                x: spot.cx - width / 2; y: spot.cy - height / 2
                width: (spot.live.radius + spot.live.border) * root.edge * 2; height: width; radius: width / 2
                color: "transparent"; border.width: 1; border.color: spot.line; opacity: 0.4
            }
            MouseArea {
                objectName: "retouchTarget_" + spot.index
                x: spot.cx - Math.max(10, spot.r); y: spot.cy - Math.max(10, spot.r)
                width: Math.max(20, spot.r * 2); height: width
                cursorShape: Qt.SizeAllCursor
                preventStealing: true
                onPressed: mouse => { const p = mapToItem(root, mouse.x, mouse.y); spot.startDrag(p.x, p.y) }
                onReleased: spot.commitDrag()
                onCanceled: { spot.dragStart = null; spot.dragging = null }
                onPositionChanged: mouse => {
                    if (!pressed) return
                    const p = mapToItem(root, mouse.x, mouse.y)
                    spot.moveDrag(p.x, p.y, false)
                }
            }
            // the source, dashed by opacity, drags on its own
            Rectangle {
                visible: spot.sourced
                x: spot.sx - spot.r; y: spot.sy - spot.r
                width: spot.r * 2; height: width; radius: width / 2
                color: "transparent"; border.width: 1; border.color: spot.line; opacity: 0.7
            }
            MouseArea {
                objectName: "retouchSource_" + spot.index
                visible: spot.sourced
                x: spot.sx - Math.max(10, spot.r); y: spot.sy - Math.max(10, spot.r)
                width: Math.max(20, spot.r * 2); height: width
                cursorShape: Qt.SizeAllCursor
                preventStealing: true
                onPressed: mouse => {
                    const p = mapToItem(root, mouse.x, mouse.y)
                    // Small spots have overlapping minimum-size hit areas.
                    // Let the nearer target underneath receive its own press.
                    if (Math.hypot(p.x-spot.cx, p.y-spot.cy) < Math.hypot(p.x-spot.sx, p.y-spot.sy)) {
                        mouse.accepted = false
                        return
                    }
                    spot.startDrag(p.x, p.y)
                }
                onReleased: spot.commitDrag()
                onCanceled: { spot.dragStart = null; spot.dragging = null }
                onPositionChanged: mouse => {
                    if (!pressed) return
                    const p = mapToItem(root, mouse.x, mouse.y)
                    spot.moveDrag(p.x, p.y, true)
                }
            }
        }
    }
}
