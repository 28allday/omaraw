import QtQuick
import OmaRaw.Ui

// Hue follows the engine's conventional degrees (Yrg angle + 30 degrees).
// Only the pointer moves during editing; the disk is a shared, cached image.
FocusScope {
    id: root
    property real hue: 0
    property real amount: 0
    property string label: ""
    property bool primary: false
    readonly property real radius: Math.max(1, Math.min(width, height) / 2 - Theme.s2)
    readonly property real angle: hue * Math.PI / 180
    readonly property real handleX: width / 2 + Math.cos(angle) * Math.min(1, amount) * radius
    readonly property real handleY: height / 2 - Math.sin(angle) * Math.min(1, amount) * radius
    signal edited(real hue, real amount)
    signal editingFinished()
    activeFocusOnTab: true
    Accessible.role: Accessible.Slider
    Accessible.name: label + qsTr(" colour wheel")
    Accessible.description: qsTr("Hue %1 degrees, tint %2 percent. Arrow keys move the tint; Shift makes fine changes. Home clears the tint.").arg(hue.toFixed(0)).arg((amount * 100).toFixed(1))

    function point(x, y, snap = true) {
        const dx = (x - width / 2) / radius, dy = (height / 2 - y) / radius
        const distance = Math.sqrt(dx * dx + dy * dy)
        edited((snap && distance < 0.025) ? hue : (Math.atan2(dy, dx) * 180 / Math.PI + 360) % 360,
               (snap && distance < 0.025) ? 0 : Math.min(1, distance))
    }
    function nudge(dx, dy, fine) {
        point(handleX + dx * (fine ? 0.2 : 2), handleY + dy * (fine ? 0.2 : 2), false)
        editingFinished()
    }
    Keys.onPressed: event => {
        if (clickFinish.running) { clickFinish.stop(); editingFinished() }
        const fine = (event.modifiers & Qt.ShiftModifier) !== 0
        const arrow = [Qt.Key_Left,Qt.Key_Right,Qt.Key_Up,Qt.Key_Down].includes(event.key)
        if ((event.modifiers & Qt.ControlModifier) && arrow) {
            const direction = event.key === Qt.Key_Right || event.key === Qt.Key_Up ? 1 : -1
            edited(hue,Math.max(0,Math.min(1,amount+direction*(fine?0.001:0.01))))
            editingFinished(); event.accepted = true; return
        }
        if (event.key === Qt.Key_Left) nudge(-1, 0, fine)
        else if (event.key === Qt.Key_Right) nudge(1, 0, fine)
        else if (event.key === Qt.Key_Up) nudge(0, -1, fine)
        else if (event.key === Qt.Key_Down) nudge(0, 1, fine)
        else if (event.key === Qt.Key_Home || event.key === Qt.Key_Delete) { edited(0, 0); editingFinished() }
        else { event.accepted = false; return }
        event.accepted = true
    }

    Image {
        anchors.centerIn: parent
        width: root.radius * 2; height: width
        source: root.primary ? "primary-wheel.png" : "colour-wheel.png"
        smooth: true; mipmap: true
    }
    Rectangle {
        anchors.centerIn: parent
        width: root.radius * 2 + Theme.s1; height: width; radius: width / 2
        color: "transparent"; border.width: root.activeFocus ? Theme.focusRing : Theme.hairline
        border.color: root.activeFocus ? Theme.accent : Theme.borderStrong
    }
    Rectangle { anchors.centerIn: parent; width: root.primary ? root.radius*1.88 : Theme.s2; height: Theme.hairline; color: Theme.textPrimary; opacity: root.primary ? .18 : .5 }
    Rectangle { anchors.centerIn: parent; width: Theme.hairline; height: root.primary ? root.radius*1.88 : Theme.s2; color: Theme.textPrimary; opacity: root.primary ? .18 : .5 }
    Rectangle {
        objectName: "wheelHandle"
        x: root.handleX - width / 2; y: root.handleY - height / 2
        width: Theme.s3; height: width; radius: width / 2
        color: "transparent"; border.color: Theme.pasteboard; border.width: 3
        Rectangle { anchors.fill: parent; anchors.margins: 1; radius: width/2; color: "transparent"; border.width: 1.5; border.color: Theme.textPrimary }
    }
    MouseArea {
        id: pointer
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        preventStealing: true
        hoverEnabled: true
        cursorShape: Qt.CrossCursor
        property real originX: 0
        property real originY: 0
        property real startX: 0
        property real startY: 0
        property bool grabbing: false
        property bool moved: false
        property bool doubleReset: false
        property real lockedHue: 0
        function movePoint(x, y, modifiers) {
            if (modifiers & Qt.ControlModifier) {
                const a = lockedHue * Math.PI / 180
                const strength = ((x-root.width/2)*Math.cos(a) - (y-root.height/2)*Math.sin(a)) / root.radius
                root.edited(lockedHue, Math.max(0, Math.min(1, strength)))
            } else root.point(x,y)
        }
        onPressed: mouse => {
            const dx = mouse.x - root.width/2, dy = mouse.y - root.height/2
            if (Math.sqrt(dx*dx+dy*dy) > root.radius + Theme.s2) { mouse.accepted = false; return }
            if (clickFinish.running && Math.hypot(mouse.x-originX, mouse.y-originY) > Theme.dragThreshold) {
                clickFinish.stop(); root.editingFinished()
            }
            clickFinish.stop()
            moved = false; doubleReset = false; lockedHue = root.hue
            root.forceActiveFocus()
            if (mouse.button === Qt.RightButton) { root.edited(0,0); root.editingFinished(); return }
            originX = mouse.x; originY = mouse.y
            grabbing = Math.hypot(mouse.x-root.handleX, mouse.y-root.handleY) < Theme.s3
            startX = grabbing ? root.handleX : mouse.x; startY = grabbing ? root.handleY : mouse.y
            if (!grabbing) movePoint(mouse.x, mouse.y, mouse.modifiers)
        }
        onPositionChanged: mouse => {
            if (!(pressedButtons & Qt.LeftButton)) return
            const scale = (mouse.modifiers & Qt.ShiftModifier) ? 0.1 : 1
            if (Math.hypot(mouse.x-originX, mouse.y-originY) > Theme.dragThreshold) moved = true
            movePoint(startX + (mouse.x-originX)*scale, startY + (mouse.y-originY)*scale, mouse.modifiers)
        }
        onDoubleClicked: mouse => {
            if (mouse.button !== Qt.LeftButton) return
            clickFinish.stop(); doubleReset = true
            root.edited(0,0); root.editingFinished()
        }
        onReleased: mouse => {
            if (mouse.button !== Qt.LeftButton || doubleReset) return
            if (moved) root.editingFinished()
            else clickFinish.restart()
        }
        onCanceled: { clickFinish.stop(); root.editingFinished() }
        onWheel: wheel => {
            if (clickFinish.running) { clickFinish.stop(); root.editingFinished() }
            const step = wheel.angleDelta.y ? wheel.angleDelta.y / 120 : wheel.pixelDelta.y / 30
            if ((wheel.modifiers & Qt.ShiftModifier) && !(wheel.modifiers & Qt.ControlModifier)) root.edited((root.hue + step + 360) % 360, root.amount)
            else root.edited(root.hue, Math.max(0, Math.min(1, root.amount + step * ((wheel.modifiers & Qt.ShiftModifier) ? 0.001 : 0.01))))
            root.editingFinished()
            wheel.accepted = true
        }
    }
    // Keep a click and its possible second click in one history gesture.
    // Moving drags finish immediately; their preview never waits for this timer.
    Timer { id: clickFinish; interval: Qt.styleHints.mouseDoubleClickInterval + 20; onTriggered: root.editingFinished() }
    onVisibleChanged: if (!visible && clickFinish.running) { clickFinish.stop(); root.editingFinished() }
    Tooltip {
        text: root.label
        description: qsTr("Drag towards a colour to add a tint. Centre is neutral. Shift-drag for fine control. Ctrl-drag locks hue. Double-click or right-click clears the tint.")
        visible: pointer.containsMouse && !pointer.pressed
    }
}
