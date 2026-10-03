pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// Outlines of the local adjustment masks over the render (sized to the
// image item it sits on). The active local shows every shape in its
// group; the others show only their first shape, dim, as something to
// click. The active shape is the brightest of all and is the one a drag
// moves. Radial: centre dot, inner circle at the radius, dashed outer
// circle at radius + feather. Gradient: anchor dot with the line the
// mask fades across. Brush: the stroke through its nodes.
Item {
    id: root
    readonly property real edge: Math.min(width, height)
    // While a stroke is being painted the viewer feeds the nodes here.
    property var painting: []
    property real paintSize: 0.05
    property rect viewport: Qt.rect(0, 0, width, height)
    StableList {
        id: localList
        source: engine.locals
        key: (entry, index) => engine.imageId + ":" + entry.priority
    }

    Repeater {
        model: localList.model
        Item {
            id: localItem
            required property int index
            required property var modelData
            readonly property var live: engine.locals[index] || modelData
            readonly property bool activeLocal: engine.activeLocal === live.priority
            anchors.fill: parent
            opacity: live.enabled ? 1 : 0.4
            StableList { id: shapeList; source: localItem.live.shapes }

            Repeater {
                // Keep delegates alive through selection and engine read-back.
                model: shapeList.model
                Item {
                    id: shape
                    required property int index
                    required property var modelData
                    readonly property var live: localItem.live.shapes[index] || modelData
                    visible: localItem.activeLocal || index === 0
                    readonly property bool active: localItem.activeLocal && engine.activeShape === index
                    readonly property bool radial: live.shape === 1
                    readonly property bool gradient: live.shape === 2
                    readonly property bool brush: live.shape === 3
                    readonly property var anchor: live.shape === 4 && (live.displayPoints || []).length ? live.displayPoints[0] : {x: live.cx, y: live.cy}
                    readonly property real baseCx: anchor.x * root.width
                    readonly property real baseCy: anchor.y * root.height
                    property var dragging: null
                    readonly property real cx: dragging ? dragging.cx : baseCx
                    readonly property real cy: dragging ? dragging.cy : baseCy
                    // Keep pointer feedback local: moving the mouse must not
                    // write history, repaint a full mask or queue RAW renders.
                    function startDrag(px, py) {
                        if (live.shape === 4) { engine.activeLocal = localItem.live.priority; engine.activeShape = index; return }
                        dragging = {cx: baseCx, cy: baseCy, x: px, y: py, startCx: baseCx, startCy: baseCy}
                        engine.activeLocal = localItem.live.priority
                        engine.activeShape = index
                    }
                    function moveDrag(px, py) {
                        if (!dragging) return
                        const d = dragging
                        dragging = {x: d.x, y: d.y, startCx: d.startCx, startCy: d.startCy,
                                    cx: Math.max(0, Math.min(root.width, d.startCx + px - d.x)),
                                    cy: Math.max(0, Math.min(root.height, d.startCy + py - d.y))}
                    }
                    function commitDrag() {
                        if (!dragging) return
                        const d = dragging, m = live
                        if (Math.hypot(d.cx-d.startCx, d.cy-d.startCy) > 0.1) {
                            engine.setShape(index, d.cx/root.width, d.cy/root.height,
                                            radial ? m.radius : gradient ? m.compression : 0,
                                            radial ? m.border : 0, gradient ? m.rotation : 0, m.opacity, true)
                        }
                        dragging = null
                    }
                    onVisibleChanged: if (!visible) dragging = null
                    readonly property color line: shape.active ? Theme.accent
                                                : localItem.activeLocal ? Qt.rgba(1, 1, 1, 0.75)
                                                                        : Qt.rgba(1, 1, 1, 0.4)
                    anchors.fill: parent
                    // A subtracted shape reads as cut out of the mask; a
                    // bypassed one is barely there.
                    opacity: shape.live.enabled === false ? 0.3 : shape.live.combine === 2 ? 0.7 : 1

                    // radial rings
                    Rectangle {
                        visible: shape.radial
                        x: shape.cx - width / 2; y: shape.cy - height / 2
                        width: shape.live.radius * root.edge * 2; height: width
                        radius: width / 2; color: "transparent"
                        border.width: shape.active ? 2 : 1; border.color: shape.line
                    }
                    Rectangle {
                        visible: shape.radial && shape.live.border > 0
                        x: shape.cx - width / 2; y: shape.cy - height / 2
                        width: (shape.live.radius + shape.live.border) * root.edge * 2; height: width
                        radius: width / 2; color: "transparent"
                        border.width: 1; border.color: shape.line
                        opacity: 0.5
                    }
                    // gradient line through the anchor, at the rotation
                    Rectangle {
                        visible: shape.gradient
                        x: shape.cx - width / 2; y: shape.cy - 1
                        width: root.edge * 1.2; height: shape.active ? 2 : 1
                        color: shape.line
                        transformOrigin: Item.Center
                        rotation: -shape.live.rotation
                    }
                    Repeater {
                        model: shape.gradient ? 2 : 0
                        Rectangle {
                            id: fadeLine
                            required property int index
                            readonly property real distance: (index === 0 ? -1 : 1) * shape.live.compression * Math.hypot(root.width, root.height) * 0.9062
                            x: shape.cx - width / 2; y: shape.cy - 1
                            width: root.edge * 1.2; height: 1
                            color: shape.line; opacity: 0.4
                            transformOrigin: Item.Center
                            rotation: -shape.live.rotation
                            // The native falloff is normalised by the frame
                            // diagonal. These guides mark 10% and 90% coverage.
                            transform: Translate { y: fadeLine.distance * Math.cos(shape.live.rotation * Math.PI / 180); x: fadeLine.distance * Math.sin(shape.live.rotation * Math.PI / 180) }
                        }
                    }
                    // brush stroke: the nodes joined, at the node width
                    StrokeGuide {
                        objectName: "localStrokeGuide"
                        viewport: root.viewport
                        points: shape.brush ? (shape.live.points || []) : []
                        shiftX: shape.cx - shape.baseCx; shiftY: shape.cy - shape.baseCy
                        penColor: shape.line
                        penWidth: Math.max(1, shape.live.size * root.edge * 2)
                        penOpacity: 0.35
                        centreWidth: shape.active ? 2 : 1
                    }
                    // anchor handle: click activates, drag moves
                    Rectangle {
                        id: handle
                        objectName: "shapeHandle"
                        visible: shape.live.shape !== 4 || !shape.active
                        x: shape.cx - 8; y: shape.cy - 8
                        width: 16; height: 16; radius: 8
                        color: shape.active ? Theme.accent : Theme.scrim
                        border.width: 2; border.color: shape.active ? Theme.accentText : Qt.rgba(1, 1, 1, 0.8)
                        MouseArea {
                            id: dragArea
                            anchors.fill: parent
                            anchors.margins: -6
                            cursorShape: Qt.SizeAllCursor
                            preventStealing: true
                            onPressed: mouse => {
                                const p = mapToItem(root, mouse.x, mouse.y)
                                shape.startDrag(p.x, p.y)
                            }
                            onReleased: shape.commitDrag()
                            onCanceled: shape.dragging = null
                            onPositionChanged: mouse => {
                                if (!pressed) return
                                const p = mapToItem(root, mouse.x, mouse.y)
                                shape.moveDrag(p.x, p.y)
                            }
                        }
                    }
                    Text {
                        x: shape.cx + 12; y: shape.cy - 8
                        textFormat: Text.PlainText
                        text: localItem.live.name
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
                        color: shape.line
                        style: Text.Outline; styleColor: Qt.rgba(0, 0, 0, 0.6)
                        visible: shape.active && shape.index === 0
                    }
                }
            }
        }
    }

    // The stroke under the cursor, before it reaches the engine.
    StrokeGuide {
        objectName: "localWetGuide"
        viewport: root.viewport
        points: root.painting
        penColor: Theme.accent
        penWidth: Math.max(2, root.paintSize * root.edge * 2)
    }
}
