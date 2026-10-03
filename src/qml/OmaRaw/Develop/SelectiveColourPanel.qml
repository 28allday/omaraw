pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

Column {
    id: root
    property bool embedded: false
    property bool saveEnabled: true
    signal savePresetRequested(string operation, string label)
    objectName: "selectiveColourPanel"
    readonly property var params: (engine.paramsVersion, engine.paramsFor("colorequal"))
    readonly property var bands: ["red", "orange", "yellow", "green", "cyan", "blue", "lavender", "magenta"]
    readonly property var colours: ["#ed655d", "#ed9f55", "#ddd35d", "#79b95c", "#5bbcc2", "#639de4", "#a98ad9", "#d680bd"]
    property int band: 0
    readonly property bool moduleOn: params.length > 0 && params[0].enabled
    readonly property var channelRows: params.filter(p => p.field.endsWith("_" + bands[band]))
    readonly property var advancedRows: params.filter(p => !bands.some(b => p.field.endsWith("_" + b)))
    StableList { id: channelList; source: root.channelRows; key: p => p.op + ":" + p.field }
    StableList { id: advancedList; source: root.advanced ? root.advancedRows : []; key: p => p.op + ":" + p.field }
    property bool advanced: false
    // Folded to its heading until asked for.
    property bool open: false
    spacing: Theme.s1
    height: visible ? implicitHeight : 0

    BlockHeading {
        visible: !root.embedded
        height: visible ? Theme.hRow + Theme.s1 : 0
        text: qsTr("Selective colour"); tip: Names.moduleTip("colorequal")
        collapsible: true; open: root.open; onToggled: root.open = !root.open
        operation: "colorequal"; on: root.moduleOn; saveEnabled: root.saveEnabled
        onEyeClicked: engine.setModuleEnabled("colorequal", !root.moduleOn)
        onSavePresetRequested: (operation, label) => root.savePresetRequested(operation, label)
        IconButton {
            iconName: "rotate-ccw"; text: qsTr("Reset selective colour")
            tip: qsTr("Puts every band back to zero and hides the adjustment.")
            enabled: root.params.length > 0
            onClicked: engine.applyValues(root.params.map(p => ({op: p.op, field: p.field, value: p.def}))
                .concat([{op: "colorequal", field: "sat_red", enabled: false}]))
        }
    }
    Column {
        visible: root.open || height > 0
        width: parent.width; height: root.open ? implicitHeight : 0
        Behavior on height { NumberAnimation { duration: Theme.dSlow; easing.type: Theme.easing } }
        spacing: Theme.s1
        Row {
            x: Theme.s3; width: parent.width - Theme.s3 * 2; spacing: Theme.s2
            Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 12; height: 12; radius: 6; color: root.colours[root.band] }
            ComboField {
                width: parent.width - 12 - Theme.s2
                model: [qsTr("Red"), qsTr("Orange"), qsTr("Yellow"), qsTr("Green"), qsTr("Cyan"), qsTr("Blue"), qsTr("Lavender"), qsTr("Magenta")]
                tipTitle: qsTr("Colour band"); tip: qsTr("Which colour the sliders below act on; the others are left alone.")
                currentIndex: root.band; onActivated: i => root.band = i
            }
        }
        Repeater {
            model: channelList.model
            AdjustmentRow { required property int index; required property var modelData; width: parent.width; param: root.channelRows[index] || modelData }
        }
        ToolButton {
            x: Theme.s3; text: root.advanced ? qsTr("Hide smoothing controls") : qsTr("Smoothing and neutral protection")
            iconName: root.advanced ? "chevron-up" : "chevron-down"; showLabel: true
            tip: qsTr("How softly one band blends into the next, and how greys are kept out of the shift.")
            onClicked: root.advanced = !root.advanced
        }
        Repeater {
            model: advancedList.model
            AdjustmentRow { required property int index; required property var modelData; width: parent.width; param: (root.advanced ? root.advancedRows : [])[index] || modelData }
        }
        Text {
            x: Theme.s3; width: parent.width - Theme.s3 * 2
            text: qsTr("Adjust one colour range while protecting neutral tones. Smoothing controls soften transitions around fine detail.")
            color: Theme.textMuted; wrapMode: Text.Wrap
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        }
        Item { width: 1; height: Theme.s2 }
    }
}
