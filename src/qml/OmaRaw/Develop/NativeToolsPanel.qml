pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

Column {
    id: root
    objectName: "nativeToolsPanel"
    required property string group
    property bool saveEnabled: true
    signal savePresetRequested(string operation, string label)
    // Noise reduction has its own block (NoiseReductionPanel), so its native
    // editor is not offered here as a second way in.
    // Embedded cards keep each editor alive when folded, preserving in-flight gestures.
    property string requestedOperation: ""
    property bool embedded: false
    property bool active: true
    readonly property var allRows: active ? engine.params.filter(p => p.advancedTool && (root.requestedOperation !== "" ? p.op === root.requestedOperation : p.group === group) && p.op !== "retouch" && p.op !== "denoiseprofile") : []
    // Choose the display order independently of the engine parameter table:
    // routine corrections first, specialised tools afterwards.
    readonly property var toolOrder: ({
        "Basic": ["toneequal"],
        "Curve": ["rgblevels"],
        "Color": ["primaries", "negadoctor"],
        "Detail": ["contrastntexture", "atrous"]
    })
    readonly property var operations: {
        const available = [...new Set(root.allRows.map(p => p.op))]
        const preferred = root.toolOrder[root.group] || []
        return preferred.filter(op => available.includes(op))
            .concat(available.filter(op => !preferred.includes(op)))
    }
    property int selection: 0
    readonly property string operation: operations[Math.min(selection, operations.length - 1)] || ""
    property int channel: 0
    function value(field, fallback) {
        const p = (engine.paramsVersion, engine.paramsFor(root.operation)).find(p => p.field === field)
        return p ? p.value : fallback
    }
    readonly property var channels: operation === "denoiseprofile"
        ? (value("wavelet_color_mode", 1) === 1 ? [qsTr("Luminance"), qsTr("Colour noise")] : [qsTr("Overall"), qsTr("Red"), qsTr("Green"), qsTr("Blue")])
        : operation === "atrous" ? [qsTr("Luminance"), qsTr("Colour"), qsTr("Edges")]
        : operation === "rgblevels" ? (value("autoscale", 0) === 0 ? [qsTr("RGB")] : [qsTr("Red"), qsTr("Green"), qsTr("Blue")]) : []
    readonly property int engineChannel: Math.min(channel, channels.length - 1) + (operation === "denoiseprofile" && value("wavelet_color_mode", 1) === 1 ? 4 : 0)
    readonly property var rows: allRows.filter(p => p.op === operation && (p.channel < 0 || p.channel === engineChannel))
    readonly property bool moduleOn: rows.length > 0 && rows[0].enabled
    readonly property string explanation: ({
        "toneequal": qsTr("Lighten or darken nine brightness zones while preserving detail. Mask exposure shifts which parts of the picture each zone affects."),
        "contrastntexture": qsTr("Enhance or soften texture at a chosen detail size. Noise protection limits amplification of shadow noise."),
        "denoiseprofile": qsTr("Wavelets: adjust luminance and colour noise from coarse to fine. Patch size, search radius and preserve detail apply to non-local means. Auto methods choose some settings themselves."),
        "atrous": qsTr("Shape contrast and edges at six detail sizes. The middle value is neutral. Noise reduction has its own block above."),
        "primaries": qsTr("Shift the hue and purity of the RGB primaries to change the overall colour rendering."),
        "rgblevels": qsTr("Set the black, middle and white input points. Linked RGB applies one set to all channels."),
        "negadoctor": qsTr("Convert a scanned or photographed negative. Start with film stock and film base, then density range and print settings. Disable Film tone and Tone when preparing a negative.")
    })[operation] || ""
    spacing: Theme.s1
    visible: operations.length > 0
    onOperationChanged: channel = 0
    onGroupChanged: selection = 0
    Text {
        visible: !root.embedded
        x: Theme.s3; width: parent.width - Theme.s3 * 2
        text: qsTr("More tools")
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; font.weight: Theme.wHeading; font.capitalization: Font.AllUppercase; font.letterSpacing: 0.6; color: Theme.textSecondary
    }
    Item {
        visible: !root.embedded
        width: parent.width
        height: visible ? toolPicker.implicitHeight : 0
        Row {
            id: toolPicker
            x: Theme.s3; width: parent.width - Theme.s3 * 2; spacing: Theme.s1
            ComboField {
            width: parent.width - actions.width - toggle.width - Theme.s1 * 2
            model: root.operations.map(op => Names.module(op, op))
            currentIndex: Math.min(root.selection, root.operations.length - 1)
            tipTitle: qsTr("Develop tool"); tip: qsTr("Choose the tool to edit. Each tool keeps its own settings.")
            onActivated: i => root.selection = i
        }
        ModuleActions { id: actions; operation: root.operation; title: Names.module(root.operation, root.operation); saveEnabled: root.saveEnabled; onSavePresetRequested: (operation, label) => root.savePresetRequested(operation, label) }
            EyeToggle { id: toggle; checked: root.moduleOn; text: Names.module(root.operation, root.operation); onClicked: engine.setModuleEnabled(root.operation, !root.moduleOn) }
        }
    }
    Text {
        x: Theme.s3; width: parent.width - Theme.s3 * 2
        text: root.explanation; wrapMode: Text.WordWrap
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
    }
    ToolButton {
        x: Theme.s3; visible: root.operation === "negadoctor"
        iconName: "wand-sparkles"; text: qsTr("Prepare negative"); showLabel: true
        tip: qsTr("Enables negative conversion and disables the normal Tone, Film tone and base curve transforms.")
        onClicked: engine.toolAction("negadoctor", "prepare")
    }
    ComboField {
        x: Theme.s3; width: parent.width - Theme.s3 * 2
        visible: root.channels.length > 1
        model: root.channels; currentIndex: Math.min(root.channel, root.channels.length - 1)
        tipTitle: qsTr("Channel"); tip: qsTr("Choose which part of the adjustment to edit.")
        onActivated: i => root.channel = i
    }
    StableList { id: stable; source: root.rows; key: p => p.op + ":" + p.field }
    Repeater {
        model: stable.model
        AdjustmentRow {
            required property int index
            required property var modelData
            width: parent.width
            param: root.rows[index] || modelData
        }
    }
}
