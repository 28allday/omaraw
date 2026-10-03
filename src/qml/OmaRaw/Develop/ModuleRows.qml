pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// A run of engine parameters grouped by the adjustment they belong to. An
// adjustment with several controls gets a quiet heading; one with a single
// slider is just that slider, named for what it does. Either way the dot
// shows or hides the adjustment and the menu copies, saves or resets it;
// the dot stays visible; the action menu appears when the pointer is near.
Column {
    id: root
    property var rows: []
    property bool saveEnabled: true
    // Never fold a lone slider into its heading (the fuller lists, where the
    // same adjustment's main control sits elsewhere).
    property bool alwaysHeadings: false
    property bool headings: true
    // Short, related adjustments share a card, with actions on their first row.
    property bool inlineActions: false
    property var actionOperations: null
    signal savePresetRequested(string operation, string label)
    spacing: Theme.s1
    height: visible ? implicitHeight : 0

    function count(op) { let n = 0; for (const p of root.rows) if (p.op === op) ++n; return n }
    // Rows persist across a parameter read-back (see StableList): a slider
    // being dragged must not be rebuilt under the pointer.
    StableList { id: rowList; source: root.rows; key: p => p.op + ":" + p.field }
    Repeater {
        model: rowList.model
        Column {
            id: entry
            required property int index
            required property var modelData
            readonly property var live: root.rows[index] || modelData
            readonly property bool first: index === 0 || (root.rows[index - 1] || {}).op !== live.op
            readonly property bool actions: root.headings && (root.actionOperations === null || root.actionOperations.includes(live.op))
            readonly property bool lone: entry.actions && entry.first && (root.inlineActions || (!root.alwaysHeadings && root.count(live.op) === 1))
                                         && live.type !== 2 && live.type !== 3 && live.kind !== "file"
            readonly property string moduleName: Names.module(live.op, live.module)
            readonly property bool on: live.enabled || false
            width: root.width
            BlockHeading {
                visible: entry.actions && entry.first && !entry.lone
                height: visible ? Theme.hRow + Theme.s1 : 0
                text: entry.moduleName; tip: Names.moduleTip(entry.live.op)
                // A folded-away heading must not answer to the adjustment's name.
                operation: entry.actions && entry.first && !entry.lone ? entry.live.op : ""; on: entry.on
                saveEnabled: root.saveEnabled
                onEyeClicked: engine.setModuleEnabled(entry.live.op, !entry.on)
                onSavePresetRequested: (operation, label) => root.savePresetRequested(operation, label)
            }
            AdjustmentRow {
                id: row
                param: entry.live; width: parent.width
                caption: entry.lone && Names.isGenericLabel(entry.live.op, entry.live.field, entry.live.label) ? entry.moduleName : ""
                trailingReserve: entry.lone ? loneControls.width : 0
                showOffIndicator: !entry.lone
                Row {
                    id: loneControls
                    visible: entry.lone
                    // Beside the figure, on the slider's caption line.
                    x: row.trailingX - width; y: 0
                    spacing: 0
                    ModuleActions {
                        opacity: row.hovered || activeFocus ? 1 : 0
                        operation: entry.lone ? entry.live.op : ""; title: entry.moduleName
                        saveEnabled: root.saveEnabled
                        onSavePresetRequested: (operation, label) => root.savePresetRequested(operation, label)
                    }
                    EyeToggle {
                        objectName: entry.lone ? "moduleEye_" + entry.live.op : ""
                        prominent: true
                        height: Theme.hRow
                        checked: entry.on; text: entry.moduleName
                        onClicked: engine.setModuleEnabled(entry.live.op, !entry.on)
                    }
                }
            }
        }
    }
}
