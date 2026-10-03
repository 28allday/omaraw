import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

// Which develop settings travel: the groups ticked here are copied to the
// settings clipboard ("copy") or laid straight onto other photos ("sync").
// The ticks are kept for the session, so the second sync asks nothing new.
C.Popup {
    id: root
    property string mode: "sync"          // "copy" | "sync"
    property var targets: []              // sync: [{path, variant}] the settings go to
    property var checked: ({})            // group key → bool
    readonly property var groups: engine.settingsGroups()
    readonly property var chosen: { const c = root.checked; return Object.keys(c).filter(k => c[k]) }
    property string sourcePath: ""
    property int sourceVariant: 0
    property string sourceName: ""
    readonly property bool sourceReady: engine.ready && !engine.busy && !engine.detailBusy
        && !engine.exporting && engine.syncPending === 0 && engine.imageId >= 0
        && engine.imagePath === sourcePath && engine.imageVariant === sourceVariant && engine.params.length > 0
    // Re-counted whenever the engine's saved state moves (valuesFor is a call, not a property).
    readonly property int valueCount: { engine.params; engine.locals; engine.spots; engine.curve; engine.zones; engine.parametric
                                        return sourceReady ? engine.valuesFor(chosen).length : 0 }

    function openFor(m, t, source) {
        mode = m
        targets = t || []
        sourcePath = source.path
        sourceVariant = source.variant || 0
        sourceName = source.filename || sourcePath.split("/").pop()
        if (sourceVariant > 0) sourceName += " (" + (source.variantName || qsTr("variant %1").arg(sourceVariant)) + ")"
        if (Object.keys(checked).length === 0) reset()
        // Library selection does not normally load Develop. Read this exact
        // photo/variant before enabling Copy or Sync, even if another is open.
        engine.load(sourcePath, sourceVariant)
        open()
    }
    function reset() { const c = {}; for (const g of groups) c[g.key] = g.defaultOn; checked = c }
    function setAll(on) { const c = {}; for (const g of groups) c[g.key] = on; checked = c }
    function toggle(key) { const c = Object.assign({}, checked); c[key] = !c[key]; checked = c }
    function accept() {
        if (!sourceReady || chosen.length === 0) return
        if (mode === "copy") { if (!engine.copySettings(chosen)) return }
        else engine.applyValuesTo(targets, engine.valuesFor(chosen))
        close()
    }

    modal: true
    parent: C.Overlay.overlay
    anchors.centerIn: parent
    width: 400; padding: Theme.s4
    background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }

    Column {
        width: parent.width
        spacing: Theme.s2
        Text {
            text: root.mode === "copy" ? qsTr("Copy settings") : qsTr("Sync settings")
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading; color: Theme.textPrimary
        }
        Text {
            width: parent.width
            textFormat: Text.PlainText
            text: root.mode === "copy" ? qsTr("From %1 to the clipboard").arg(root.sourceName)
                 : root.targets.length === 1 ? qsTr("From %1 to one other photo").arg(root.sourceName)
                 : qsTr("From %1 to %2 other photos").arg(root.sourceName).arg(root.targets.length)
            wrapMode: Text.Wrap
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
        }
        Text {
            visible: !root.sourceReady
            width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText
            text: engine.busy ? qsTr("Loading settings…") : engine.status
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
        }
        // The explicit target list: who gets the settings.
        Text {
            visible: root.mode === "sync" && root.targets.length > 0
            width: parent.width
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            textFormat: Text.PlainText
            text: {
                const names = root.targets.slice(0, 8).map(t => (t.path || "").split("/").pop() + (t.variant > 0 ? " (" + (t.name || qsTr("variant %1").arg(t.variant)) + ")" : ""))
                return names.join(", ") + (root.targets.length > 8 ? qsTr(" and %1 more").arg(root.targets.length - 8) : "")
            }
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
        }
        Item { width: 1; height: Theme.s1 }
        Repeater {
            model: root.groups
            Toggle {
                required property var modelData
                width: parent.width
                label: modelData.label
                tip: qsTr("Whether this group of settings travels.")
                // Not checkable: a click would assign `checked` and break the
                // binding, and All / None would stop reaching this row.
                checkable: false
                checked: root.checked[modelData.key] === true
                onClicked: root.toggle(modelData.key)
            }
        }
        Item { width: 1; height: Theme.s1 }
        Row {
            spacing: Theme.s2
            ToolButton { text: qsTr("All"); showLabel: true; tip: qsTr("Ticks every group."); onClicked: root.setAll(true) }
            ToolButton { text: qsTr("None"); showLabel: true; tip: qsTr("Clears every group."); onClicked: root.setAll(false) }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.valueCount === 1 ? qsTr("1 setting") : qsTr("%1 settings").arg(root.valueCount)
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
        }
        Row {
            anchors.right: parent.right
            spacing: Theme.s2
            ToolButton { text: qsTr("Cancel"); showLabel: true; onClicked: root.close() }
            ToolButton {
                objectName: "settingsTransferAccept"
                text: root.mode === "copy" ? qsTr("Copy") : qsTr("Sync")
                showLabel: true
                tip: root.mode === "copy" ? qsTr("Puts the ticked settings on the clipboard to paste onto other photos.") : qsTr("Lays the ticked settings onto the other photos now.")
                enabled: root.sourceReady && root.chosen.length > 0 && (root.mode === "copy" || root.targets.length > 0)
                onClicked: root.accept()
            }
        }
    }
}
