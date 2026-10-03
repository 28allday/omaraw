import QtQuick
import OmaRaw.Ui

// A visual view of the existing warmth/tint pair. No additional saved state.
Column {
    id: root
    objectName: "localColourPush"
    property var local: ({})
    readonly property int priority: local.priority === undefined ? -1 : local.priority
    readonly property real wheelRange: 40
    property real hue: 0
    property real amount: 0
    property bool editing: false
    property int editImage: -1
    property int editLocal: -1
    spacing: Theme.s1
    function read() {
        if (editing) return
        const w = local.warmth || 0, t = local.tint || 0
        amount = Math.hypot(w, t) / wheelRange
        hue = amount < 0.00001 ? 0 : (Math.atan2(-t, w) * 180 / Math.PI + 410) % 360
    }
    onLocalChanged: read()
    Component.onCompleted: read()
    function change(h, a) {
        if (!editing) {
            editing = true; editImage = engine.imageId; editLocal = priority
            engine.endGesture()
            // The coverage paint would hide the colour being judged.
            engine.maskShown = false
        }
        if (editImage !== engine.imageId || editLocal !== engine.activeLocal) { discard(); return }
        hue = h; amount = a
        const angle = (h - 50) * Math.PI / 180
        throttle.push([Math.cos(angle) * a * wheelRange, -Math.sin(angle) * a * wheelRange])
    }
    function finish() {
        if (!editing) return
        if (editImage !== engine.imageId || editLocal !== engine.activeLocal) { discard(); return }
        const angle = (hue - 50) * Math.PI / 180
        throttle.flush([Math.cos(angle) * amount * wheelRange, -Math.sin(angle) * amount * wheelRange])
        engine.endGesture(); editing = false; read()
    }
    function discard() { throttle.drop(); editing = false; editImage = -1; editLocal = -1; read() }
    onVisibleChanged: if (!visible) finish()
    EditCoalescer {
        id: throttle
        onSend: v => {
            if (root.editImage === engine.imageId && root.editLocal === engine.activeLocal)
                engine.setLocalColour(root.editLocal, v[0], v[1])
        }
    }
    Connections {
        target: engine
        function onImageChanged() { root.discard() }
        function onActiveLocalChanged() { root.discard() }
        function onHistoryJumped() { root.discard() }
        function onEditStateReplaced() { root.discard() }
    }
    ColourWheel {
        objectName: "localColourWheel"
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(root.width, 160); height: width
        label: qsTr("Selected colour push")
        hue: root.hue; amount: root.amount
        onEdited: (h, a) => root.change(h, a)
        onEditingFinished: root.finish()
    }
    Text {
        width: parent.width; wrapMode: Text.WordWrap
        text: qsTr("Drag towards a colour; distance sets strength. Shift for fine control. Double-click to reset the push.")
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
    }
}
