pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

// The shortcut editor: every entry of the window's shortcut table with its
// current keys, a search, a press-to-rebind capture, conflicts marked,
// and a reset per row or for the lot. Overrides live in the backend's
// settings; menus keep showing the defaults.
C.Popup {
    id: root
    property var shell: null
    property string filter: ""
    property string capturing: ""     // the id being rebound, "" when none
    modal: true
    parent: C.Overlay.overlay
    anchors.centerIn: parent
    width: 640; height: Math.min(parent ? parent.height - 80 : 600, 620); padding: Theme.s4
    background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }
    onOpened: { filter = ""; capturing = ""; searchField.forceActiveFocus() }
    onClosed: capturing = ""

    readonly property var entries: shell ? shell.shortcutTable.filter(e => !e.hidden) : []
    function keysOf(e) { return shell.keysFor(e.id, e.keys) }
    // Same keys in the same place: two globals, or a scoped one against a global.
    function conflictOf(e) {
        const k = keysOf(e)
        if (k === "") return ""
        for (const o of entries) {
            if (o.id === e.id || keysOf(o) !== k) continue
            if (!e.scope || !o.scope || e.scope === o.scope) return o.label
        }
        return ""
    }
    readonly property var shown: entries.filter(e => filter === "" || e.label.toLowerCase().indexOf(filter.toLowerCase()) >= 0 || keysOf(e).toLowerCase().indexOf(filter.toLowerCase()) >= 0)
    readonly property int overrideCount: shell ? Object.keys(backend.shortcutOverrides).length : 0

    // The capture: while an id is being rebound this item holds focus and
    // reads the next key; Escape cancels, Backspace unbinds.
    Item {
        id: catcher
        focus: root.capturing !== ""
        Keys.onPressed: event => {
            if (root.capturing === "") return
            event.accepted = true
            if (event.key === Qt.Key_Escape) { root.capturing = ""; return }
            if (event.key === Qt.Key_Backspace) { backend.setShortcut(root.capturing, ""); root.capturing = ""; return }
            const t = backend.keyText(event.key, event.modifiers)
            if (t === "") return
            backend.setShortcut(root.capturing, t)
            root.capturing = ""
        }
    }

    Column {
        anchors.fill: parent
        spacing: Theme.s2
        Text { text: qsTr("Keyboard shortcuts"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading; color: Theme.textPrimary }
        Text {
            width: parent.width; wrapMode: Text.WordWrap
            text: qsTr("Click a key to change it, then press the new keys. Backspace unbinds, Escape keeps it. A red key is also used by something else in the same place.")
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
        }
        Row {
            width: parent.width
            spacing: Theme.s2
            SearchField {
                id: searchField
                width: parent.width - resetAll.width - Theme.s2
                placeholder: qsTr("Search shortcuts")
                tip: qsTr("Find an action by name or by its current keys.")
                onTextChanged: root.filter = text
                onCleared: root.filter = ""
            }
            ToolButton { id: resetAll; text: qsTr("Reset all"); tip: qsTr("Restore every keyboard shortcut to its default keys."); showLabel: true; enabled: root.overrideCount > 0; onClicked: backend.resetShortcuts() }
        }
        C.ScrollView {
            id: scroller
            width: parent.width
            height: parent.height - y - doneRow.height - Theme.s2
            clip: true
            C.ScrollBar.vertical: ScrollBar {}
            C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
            contentWidth: availableWidth
            Column {
                width: scroller.availableWidth
                Repeater {
                    model: root.shown
                    Item {
                        id: row
                        required property var modelData
                        required property int index
                        width: parent.width; height: Theme.hRow + Theme.s1
                        readonly property string keys: root.keysOf(modelData)
                        readonly property string conflict: root.conflictOf(modelData)
                        readonly property bool overridden: backend.shortcutOverrides[modelData.id] !== undefined
                        readonly property bool first: index === 0 || root.shown[index - 1].group !== modelData.group
                        Text {
                            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                            width: 90
                            text: row.first ? row.modelData.group : ""
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; font.weight: Theme.wHeading; color: Theme.textMuted
                        }
                        Text {
                            anchors.left: parent.left; anchors.leftMargin: 90
                            anchors.right: keyBox.left; anchors.rightMargin: Theme.s2
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.modelData.label + (row.conflict !== "" ? qsTr("  — also %1").arg(row.conflict) : "")
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
                            color: row.conflict !== "" ? Theme.danger : Theme.textSecondary
                        }
                        Rectangle {
                            id: keyBox
                            anchors.right: resetOne.left; anchors.rightMargin: Theme.s1
                            anchors.verticalCenter: parent.verticalCenter
                            width: 150; height: Theme.hControl
                            radius: Theme.rControl
                            color: root.capturing === row.modelData.id ? Theme.selectionFill : keyHover.hovered ? Theme.hovered(Theme.controlBg) : Theme.controlBg
                            border.width: Theme.hairline
                            border.color: root.capturing === row.modelData.id ? Theme.accent : row.conflict !== "" ? Theme.danger : Theme.border
                            Text {
                                anchors.centerIn: parent
                                text: root.capturing === row.modelData.id ? qsTr("press keys…") : row.keys === "" ? qsTr("unbound") : row.keys
                                font.family: Theme.monoFamily; font.pixelSize: Theme.fsControl
                                color: root.capturing === row.modelData.id ? Theme.accent : row.keys === "" ? Theme.textMuted : row.overridden ? Theme.textPrimary : Theme.textSecondary
                            }
                            HoverHandler { id: keyHover }
                            Tooltip {
                                text: row.modelData.label
                                description: qsTr("Click, then press the new keys. Backspace removes the shortcut; Escape cancels.") + (row.conflict ? qsTr("\nThese keys are also assigned to %1.").arg(row.conflict) : "")
                                visible: keyHover.hovered && root.capturing === ""
                            }
                            TapHandler { onTapped: { root.capturing = row.modelData.id; catcher.forceActiveFocus() } }
                            Accessible.role: Accessible.Button
                            Accessible.name: qsTr("Shortcut for %1: %2").arg(row.modelData.label).arg(row.keys === "" ? qsTr("unbound") : row.keys)
                        }
                        IconButton {
                            id: resetOne
                            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                            tip: qsTr("Restore the default keys for this action.")
                            iconName: "rotate-ccw"; text: qsTr("Back to %1").arg(row.modelData.keys)
                            opacity: row.overridden ? 1 : 0
                            enabled: row.overridden
                            onClicked: backend.resetShortcut(row.modelData.id)
                        }
                    }
                }
                Text {
                    visible: root.shown.length === 0
                    x: Theme.s3; topPadding: Theme.s2
                    text: qsTr("No shortcut matches.")
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                }
            }
        }
        Row {
            id: doneRow
            anchors.right: parent.right
            spacing: Theme.s2
            ToolButton { text: qsTr("Done"); showLabel: true; onClicked: root.close() }
        }
    }
}
