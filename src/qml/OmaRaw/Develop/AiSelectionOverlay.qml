import QtQuick
import OmaRaw.Ui

Item {
    id: root
    objectName: "aiSelectionOverlay"
    visible: engine.ai.mode !== ""
    readonly property bool brushing: engine.ai.mode !== "" && engine.ai.brushMode
    Image { anchors.fill: parent; source: engine.ai.result; visible: source !== "" && engine.ai.showResult; fillMode: Image.Stretch; cache: false }
    Image { anchors.fill: parent; source: engine.ai.overlay; visible: engine.ai.result === "" || !engine.ai.showResult; fillMode: Image.Stretch; cache: false }
    Repeater {
        model: engine.ai.result === "" && !root.brushing ? engine.ai.points : []
        Rectangle {
            required property var modelData
            width: 10; height: 10; radius: 5
            x: modelData[0] * root.width - 5; y: modelData[1] * root.height - 5
            color: modelData[2] ? "#45cf84" : "#ed6565"; border.color: "white"; border.width: 1
        }
    }
    Rectangle {
        visible: pointer.pressed && pointer.dragging && !root.brushing
        x: Math.min(pointer.fromX, pointer.toX); y: Math.min(pointer.fromY, pointer.toY)
        width: Math.abs(pointer.toX - pointer.fromX); height: Math.abs(pointer.toY - pointer.fromY)
        color: "transparent"; border.color: Theme.accent; border.width: 2
    }
    Canvas {
        id: brushPreview
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const points = pointer.stroke
            if (!root.brushing || points.length === 0) return
            ctx.strokeStyle = pointer.erasing ? "#bbed6565"
                : engine.ai.mode === "mask" && engine.ai.objectBrush ? "#8845cf84" : "#8846bef5"
            ctx.lineWidth = 2 * engine.ai.brushSize * Math.max(width, height)
            ctx.lineCap = "round"; ctx.lineJoin = "round"
            ctx.beginPath(); ctx.moveTo(points[0][0] * width, points[0][1] * height)
            for (let i = 1; i < points.length; ++i) ctx.lineTo(points[i][0] * width, points[i][1] * height)
            ctx.lineTo(points[points.length - 1][0] * width + .01, points[points.length - 1][1] * height)
            ctx.stroke()
        }
    }
    Rectangle {
        visible: root.brushing && pointer.containsMouse && !engine.ai.busy
        width: 2 * engine.ai.brushSize * Math.max(root.width, root.height); height: width; radius: width / 2
        x: pointer.toX - width / 2; y: pointer.toY - height / 2
        color: "transparent"; border.width: 1; border.color: "white"
    }
    MouseArea {
        id: pointer
        objectName: "aiSelectionInput"
        anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton; hoverEnabled: true
        preventStealing: true; cursorShape: engine.ai.busy ? Qt.BusyCursor : Qt.CrossCursor
        property real fromX: 0
        property real fromY: 0
        property real toX: 0
        property real toY: 0
        property var stroke: []
        property bool erasing: false
        readonly property bool dragging: Math.hypot(toX - fromX, toY - fromY) > 5
        onPressed: mouse => {
            fromX = toX = mouse.x; fromY = toY = mouse.y; forceActiveFocus()
            if (root.brushing && !engine.ai.busy) {
                erasing = mouse.button === Qt.RightButton; stroke = [[toX / width, toY / height]]
                engine.ai.showResult = false; brushPreview.requestPaint()
            }
        }
        onPositionChanged: mouse => {
            toX = Math.max(0, Math.min(width, mouse.x)); toY = Math.max(0, Math.min(height, mouse.y))
            if (pressed && root.brushing && !engine.ai.busy && stroke.length < 4096) {
                stroke = stroke.concat([[toX / width, toY / height]]); brushPreview.requestPaint()
            }
        }
        onCanceled: { stroke = []; brushPreview.requestPaint() }
        onReleased: mouse => {
            if (engine.ai.busy) return
            if (root.brushing) {
                engine.ai.brushStroke(stroke, erasing); stroke = []; brushPreview.requestPaint(); return
            }
            if (dragging && mouse.button === Qt.LeftButton) engine.ai.selectBox(fromX / width, fromY / height, toX / width, toY / height)
            else engine.ai.point(Math.max(0, Math.min(1, mouse.x / width)), Math.max(0, Math.min(1, mouse.y / height)), mouse.button === Qt.RightButton)
        }
        Keys.onEscapePressed: engine.ai.cancel()
        Keys.onPressed: event => { if (event.key === Qt.Key_Backspace) { engine.ai.undoPoint(); event.accepted = true } }
    }
}
