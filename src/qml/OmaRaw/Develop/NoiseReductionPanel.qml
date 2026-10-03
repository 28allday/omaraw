pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// Noise reduction, in one place. It drives the engine's profiled denoise,
// which knows the noise this camera makes at this ISO, in one of two ways:
//
//   Everything        non-local means, automatic: the cleanest result the
//                     engine has, brightness grain and colour blotches alike.
//   Colour only       wavelets, automatic, with the brightness curve at zero:
//                     the colour blotches go and the grain stays, for anyone
//                     who likes the grain (darktable's "chroma only" recipe).
//
// Amount 50 is what the profile calls for (strength 1 in Everything, curve
// 0.5 in Colour only, whose threshold goes as 4·y²); 0 is nothing, 100 twice
// as strong, past which skin turns waxy. Keep fine detail raises the weight
// of the centre pixel in the patch comparison (0.1 → 3), measured on an
// LX100 at ISO 2500 to bring texture back without the grain.
Column {
    id: root
    property bool embedded: false
    objectName: "noiseReductionPanel"
    property bool saveEnabled: true
    signal savePresetRequested(string operation, string label)
    readonly property string op: "denoiseprofile"
    readonly property var rows: (engine.paramsVersion, engine.paramsFor(root.op))
    readonly property bool found: rows.length > 0
    readonly property bool moduleOn: found && rows[0].enabled === true
    readonly property var hotRows: (engine.paramsVersion, engine.paramsFor("hotpixels"))
    readonly property bool hotOn: hotRows.length > 0 && hotRows[0].enabled === true
    function value(field, fallback) { const p = root.rows.find(p => p.field === field); return p && p.value !== undefined ? p.value : fallback }
    function band(ch, b) { return root.value("y[" + ch + "][" + b + "]", 0.5) }
    readonly property int mode: Math.round(value("mode", 1))
    readonly property bool colourOnlyNow: (mode === 4 || mode === 1) && Math.round(value("wavelet_color_mode", 1)) === 1
                                          && [0, 1, 2, 3, 4, 5, 6].every(b => band(4, b) < 0.005)
    // Set up some other way (an older edit, a preset from elsewhere): the
    // controls show the nearest reading, and moving one replaces it.
    readonly property bool other: moduleOn && mode !== 3 && !colourOnlyNow
    // 0 everything, 1 colour only
    readonly property int what: colourOnlyNow ? 1 : 0
    readonly property real amount: what === 1 ? [0, 1, 2, 3, 4, 5, 6].reduce((s, b) => s + band(5, b), 0) / 7 * 100
                                              : Math.min(100, value("strength", 1) * 50)
    readonly property real keepDetail: Math.max(0, Math.min(100, (value("central_pixel_weight", 0.1) - 0.1) / 2.9 * 100))

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
        if (amount <= 0) { if (root.moduleOn) engine.setModuleEnabled(root.op, false); return }
        engine.applyValues(root.valuesFor(what, amount, keep))
    }
    function reset() {
        engine.resetTool("denoiseprofile")
    }
    spacing: Theme.s1
    visible: found
    height: visible ? implicitHeight : 0

    BlockHeading {
        visible: !root.embedded
        height: visible ? Theme.hRow + Theme.s1 : 0
        text: qsTr("Noise reduction")
        tip: qsTr("Cleans up the grain and coloured blotches of high ISO and lifted shadows, using noise measured for this camera at this ISO. Judge it at 100%.")
        operation: root.op; on: root.moduleOn; saveEnabled: root.saveEnabled
        onEyeClicked: if (root.moduleOn) engine.setModuleEnabled(root.op, false); else root.send(root.what, root.amount, root.keepDetail)
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
            tips: [qsTr("Cleans grain and colour blotches together: the cleanest result."),
                   qsTr("Takes out the colour blotches and keeps the grain, for a filmic look.")]
            currentIndex: root.what
            onActivated: i => root.send(i, root.amount === 0 ? 50 : root.amount, root.keepDetail)
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
        key: "amount"; value: root.amount
        label: qsTr("Amount")
        tip: root.what === 1 ? qsTr("How much of the colour blotching to take out. 50 is what this camera needs at this ISO.")
                             : qsTr("How much noise to take out. 50 is what this camera needs at this ISO; much past 70 and skin starts to look waxy.")
        onMoved: v => root.send(root.what, v, root.keepDetail)
    }
    NoiseSlider {
        key: "detail"; value: root.keepDetail; origin: 0
        visible: root.what === 0
        label: qsTr("Keep fine detail")
        tip: qsTr("Brings back texture — skin, hair, fabric — that the smoothing took, at the cost of a little grain.")
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
