pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// Everything uses automatic non-local means. Colour only uses automatic
// Y0U0V0 wavelets with zero luminance thresholds. Amount 50 maps to strength
// 1 or a chroma curve of 0.5 respectively; the latter's threshold is 4*y*y.
// Keep fine detail raises the central pixel weight from 0.1 to 3.
Column {
    id: root
    property bool embedded: false
    objectName: "noiseReductionPanel"
    property bool saveEnabled: true
    signal savePresetRequested(string operation, string label)
    readonly property string op: "denoiseprofile"
    readonly property var rows: (engine.paramsVersion, engine.paramsFor(root.op))
    readonly property bool found: rows.length > 0
    readonly property bool storedOn: found && rows[0].enabled === true
    readonly property bool moduleOn: requested ? requested.on : storedOn
    readonly property var hotRows: (engine.paramsVersion, engine.paramsFor("hotpixels"))
    readonly property bool hotOn: hotRows.length > 0 && hotRows[0].enabled === true
    function value(field, fallback) { const p = root.rows.find(p => p.field === field); return p && p.value !== undefined ? p.value : fallback }
    function band(ch, b) { return root.value("y[" + ch + "][" + b + "]", 0.5) }
    readonly property int mode: Math.round(value("mode", 1))
    readonly property bool colourOnlyNow: (mode === 4 || mode === 1) && Math.round(value("wavelet_color_mode", 1)) === 1
                                          && [0, 1, 2, 3, 4, 5, 6].every(b => band(4, b) < 0.005)
    // Set up some other way (an older edit, a preset from elsewhere): the
    // controls show the nearest reading, and moving one replaces it.
    readonly property bool other: !requested && moduleOn && mode !== 3 && !colourOnlyNow
    // 0 everything, 1 colour only
    readonly property int storedWhat: colourOnlyNow ? 1 : 0
    readonly property real storedAmount: storedWhat === 1 ? [0, 1, 2, 3, 4, 5, 6].reduce((s, b) => s + band(5, b), 0) / 7 * 100
                                              : Math.min(100, value("strength", 1) * 50)
    readonly property real storedDetail: Math.max(0, Math.min(100, (value("central_pixel_weight", 0.1) - 0.1) / 2.9 * 100))
    readonly property int what: requested ? requested.what : storedWhat
    readonly property real amount: requested ? requested.amount : storedAmount
    readonly property real keepDetail: requested ? requested.keep : storedDetail
    // A mode click and the next slider edit can precede engine read-back.
    // Compose edits from the latest request until the worker has caught up.
    property var requested: null
    function settle() {
        if (!engine.busy && !amountControl.editing && !detailControl.editing) requested = null
    }
    Connections {
        target: engine
        function onBusyChanged() { root.settle() }
        function onParamsChanged() { root.settle() }
        function onEditStateReplaced() { root.discard() }
        function onHistoryJumped() { root.discard() }
        function onImageChanged() { root.discard() }
    }

    function valuesFor(what, amount, keep) {
        const v = (field, value) => ({op: root.op, field: field, value: value})
        if (what === 1) {
            const out = [v("mode", 4), v("wavelet_color_mode", 1), v("strength", 1), v("overshooting", 1)]
            for (let b = 0; b < 7; ++b) { out.push(v("y[4][" + b + "]", 0)); out.push(v("y[5][" + b + "]", amount / 100)) }
            return out
        }
        return [v("mode", 3), v("strength", Math.max(0.001, amount / 50)), v("overshooting", 1), v("central_pixel_weight", 0.1 + keep / 100 * 2.9)]
    }
    function send(what, amount, keep) {
        if (amount <= 0) {
            if (root.moduleOn) {
                requested = {on: false, what: what, amount: root.amount, keep: keep}
                engine.setModuleEnabled(root.op, false)
            }
            return
        }
        requested = {on: true, what: what, amount: amount, keep: keep}
        engine.applyValues(root.valuesFor(what, amount, keep))
    }
    function choose(what) {
        const amount = root.amount, keep = root.keepDetail
        amountControl.cancel(); detailControl.cancel()
        root.send(what, amount === 0 ? 50 : amount, keep)
    }
    function toggle() {
        const what = root.what, amount = root.moduleOn ? 0 : root.amount, keep = root.keepDetail
        amountControl.cancel(); detailControl.cancel()
        root.send(what, amount, keep)
    }
    function discard() {
        amountControl.cancel(); detailControl.cancel()
        requested = null
    }
    function reset() {
        discard()
        engine.resetTool("denoiseprofile")
    }
    spacing: Theme.s1
    visible: found
    height: visible ? implicitHeight : 0

    BlockHeading {
        visible: !root.embedded
        height: visible ? Theme.hRow + Theme.s1 : 0
        text: qsTr("Noise reduction")
        tip: qsTr("Cleans up grain and coloured blotches, using a camera and ISO noise profile when available. Judge it at 100%.")
        operation: root.op; on: root.moduleOn; saveEnabled: root.saveEnabled
        resetAction: () => root.reset()
        onEyeClicked: root.toggle()
        onSavePresetRequested: (operation, label) => root.savePresetRequested(operation, label)
        IconButton {
            objectName: "resetNoiseReduction"
            iconName: "rotate-ccw"; text: qsTr("Reset noise reduction")
            tip: qsTr("Restores the noise reduction this photo started with.")
            enabled: root.moduleOn || root.hotOn
            onClicked: root.reset()
        }
    }
    Item {
        width: parent.width; height: Theme.hControl + Theme.s1
        SegmentedControl {
            objectName: "noiseWhat"
            anchors.left: parent.left; anchors.leftMargin: Theme.s3
            anchors.verticalCenter: parent.verticalCenter
            labels: [qsTr("Everything"), qsTr("Colour only")]
            tips: [qsTr("Reduces brightness grain and colour blotches. Lower Keep fine detail for more grain smoothing."),
                   qsTr("Takes out the colour blotches and keeps the grain, for a filmic look.")]
            currentIndex: root.what
            onActivated: i => root.choose(i)
        }
    }
    Text {
        visible: root.other
        x: Theme.s3; width: parent.width - Theme.s3 * 2
        text: qsTr("This photo's noise reduction was set up another way. Moving a control here replaces it.")
        wrapMode: Text.WordWrap
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
    }
    component NoiseSlider: Item {
        id: slot
        property string key: ""
        property string label: ""
        property string tip: ""
        property real value: 0
        property real origin: 0
        readonly property bool editing: throttle.editing
        function cancel() { field.discardEdit(); throttle.drop() }
        signal moved(real v)
        objectName: "noiseSlider_" + key
        width: parent ? parent.width : 0
        height: visible ? field.implicitHeight + Theme.s1 : 0
        EditCoalescer { id: throttle; onSend: v => slot.moved(v) }
        SliderField {
            id: field
            anchors.left: parent.left; anchors.right: parent.right
            anchors.leftMargin: Theme.s3; anchors.rightMargin: Theme.s3
            stacked: true; resetOnDoubleClick: true
            label: slot.label
            tip: slot.tip + " " + qsTr("Double-click resets it.")
            from: 0; to: 100; origin: slot.origin; decimals: 0
            value: slot.key === "amount" && !root.moduleOn ? 0 : slot.value
            notApplied: !root.moduleOn
            onEdited: v => throttle.push(v)
            onEditingFinished: v => throttle.flush(v)
        }
    }
    NoiseSlider {
        id: amountControl
        key: "amount"; value: root.amount
        label: qsTr("Amount")
        tip: root.what === 1 ? qsTr("How much colour blotching to remove. Start at 50 and judge at 100% zoom; lower values preserve more colour detail.")
                             : qsTr("How much grain and colour noise to remove. Start at 50 and judge at 100% zoom; higher values can soften texture.")
        onMoved: v => root.send(root.what, v, root.keepDetail)
    }
    NoiseSlider {
        id: detailControl
        key: "detail"; value: root.keepDetail; origin: 0
        visible: root.what === 0
        label: qsTr("Keep fine detail")
        tip: qsTr("Preserves texture such as skin, hair and fabric by reducing grain smoothing. Start at 0 to judge noise removal, then raise it as needed. High values can leave Everything looking similar to Colour only.")
        onMoved: v => root.send(0, root.amount, v)
    }
    CheckField {
        objectName: "removeHotPixels"
        visible: root.hotRows.length > 0
        x: Theme.s3; width: parent.width - Theme.s3 * 2
        text: qsTr("Remove hot pixels")
        tip: qsTr("Replaces single bright pixels stuck on, common in long exposures and warm sensors.")
        checked: root.hotOn
        onClicked: { checked = Qt.binding(() => root.hotOn); engine.setModuleEnabled("hotpixels", !root.hotOn) }
    }
    Text {
        x: Theme.s3; width: parent.width - Theme.s3 * 2; topPadding: Theme.s1
        text: qsTr("Zoom to 100% to judge it: the fitted view hides noise and smoothing alike.")
        wrapMode: Text.WordWrap
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
    }
}
