import QtQuick

// The pacing between a control being dragged and the engine rendering it.
//
// The first sample of a gesture is sent at once, so the picture starts to
// move with the pointer. While a render is in flight later samples are held
// (a render superseded mid-flight is thrown away by the engine, so sending
// them would only delay the picture); the newest one goes the moment the
// engine is free, or after `interval` when it was free all along. Release
// goes through `flush()` so the final value always lands and never waits.
//
//   coalescer.push(v)   a sample from a drag
//   coalescer.flush(v)  the value at release (or a reset)
//   coalescer.drop()    forget what is pending (a reset elsewhere)
//   onSend(value)       the caller talks to the engine here, and only here
QtObject {
    id: root

    property int interval: 50
    // All continuous sliders/curves share the live-preview scheduling used
    // by the colour wheels. Reset, navigation and destruction release it.
    property bool editing: false
    function previewEditing(active) {
        if (editing === active) return
        editing = active
        if (typeof engine.setPreviewEditing === "function") engine.setPreviewEditing(root, active)
    }
    Component.onDestruction: if (editing && typeof engine.setPreviewEditing === "function") engine.setPreviewEditing(root, false)
    // Held while a render is in flight. Read, not bound: a send changes
    // engine.busy at once, and a binding would loop on itself.
    function busy() { return engine.busy }
    property var pending: undefined
    property bool has: false

    signal send(var value)

    // The last value sent: a release, arrow step or typed value that was
    // already sent by its own push is not sent a second time.
    property var lastSent: undefined
    function transmit(value) { lastSent = value; root.send(value) }
    function push(value) {
        previewEditing(true)
        pending = value; has = true
        if (!timer.running && !root.busy()) { has = false; timer.start(); transmit(value); return }
        if (!timer.running) timer.start()
    }
    function flush(value) {
        timer.stop()
        const repeat = !has && lastSent !== undefined && JSON.stringify(value) === JSON.stringify(lastSent)
        has = false; pending = undefined
        if (!repeat) transmit(value)
        previewEditing(false)
    }
    function drop() { timer.stop(); has = false; pending = undefined; lastSent = undefined; previewEditing(false) }

    property Connections engineWatch: Connections {
        target: engine
        ignoreUnknownSignals: true
        function onBusyChanged() { if (!engine.busy && root.has && !timer.running) root.deliver() }
        // These values belong to the state being replaced. Sending them
        // after Reset, Undo or a photo switch would restore the old edit.
        function onEditStateReplaced() { root.drop() }
        function onHistoryJumped() { root.drop() }
        function onImageChanged() { root.drop() }
        // Auto must meter the latest requested value, even if its preview
        // has kept this sample waiting in the control.
        function onAutoExposureRequested() { if (root.has) root.flush(root.pending) }
    }
    function deliver() {
        if (!has || root.busy()) return
        has = false; const v = pending; pending = undefined
        timer.restart()
        transmit(v)
    }

    property Timer timer: Timer {
        interval: root.interval
        onTriggered: if (root.has) root.deliver()
    }
}
