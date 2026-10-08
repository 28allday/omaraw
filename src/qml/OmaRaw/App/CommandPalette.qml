pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

// Command search: every menu command, gathered from the menu bar when the
// palette opens, plus the extras the caller lists (tools that live in the
// viewer rather than a menu). Type to filter, arrows to move, Return to
// run, Escape to close. Disabled commands are listed dimmed so the
// keyboard equivalent still teaches itself.
C.Popup {
    id: root
    objectName: "commandPalette"
    // The C.MenuBar to walk.
    property var menuBar: null
    // [{label, path, shortcut, tip, enabled, run}] for commands outside the menus.
    property var extras: []
    property var commands: []
    // The query, for the selftest to type into.
    property alias queryText: query.text
    readonly property var matches: {
        const q = query.text.trim().toLowerCase()
        if (q === "") return commands
        const words = q.split(/\s+/)
        const scored = []
        for (const c of commands) {
            const hay = (c.path + " " + c.label).toLowerCase()
            let ok = true
            for (const w of words) if (hay.indexOf(w) < 0) { ok = false; break }
            if (!ok) continue
            const score = c.label.toLowerCase().startsWith(q) ? 0 : c.label.toLowerCase().indexOf(q) >= 0 ? 1 : 2
            scored.push({ c: c, s: score })
        }
        scored.sort((a, b) => a.s - b.s)
        return scored.map(x => x.c)
    }

    modal: true
    focus: true
    x: Math.round((parent.width - width) / 2)
    y: Math.round(parent.height * 0.14)
    width: Math.min(560, parent.width - Theme.s4 * 2)
    padding: Theme.s2
    closePolicy: C.Popup.CloseOnEscape | C.Popup.CloseOnPressOutside
    background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }

    function gather() {
        const out = []
        const walk = (menu, path, ancestors) => {
            if (!menu || typeof menu.count !== "number") return
            const chain = ancestors.concat([menu])
            for (let i = 0; i < menu.count; ++i) {
                const item = menu.itemAt(i)
                if (!item) continue
                if (item.subMenu) { walk(item.subMenu, path + " › " + item.text, chain.concat([item])); continue }
                if (!item.text || item.text === "") continue
                // Submenu popups do not inherit their parent menu's disabled
                // state. Check the full chain live, including at execution.
                const available = () => item.enabled && chain.every(parent => parent.enabled)
                out.push({ label: item.text, path: path, shortcut: backend.shortcutHint(item.shortcut || ""), tip: item.tip || "",
                           get enabled() { return available() }, run: () => { if (available()) item.triggered() } })
            }
        }
        if (menuBar && typeof menuBar.count === "number")
            for (let m = 0; m < menuBar.count; ++m) { const menu = menuBar.menuAt(m); if (menu) walk(menu, menu.title, [menuBar]) }
        for (const e of extras) out.push(e)
        commands = out
    }
    function runCurrent() {
        if (!root.opened) return
        const c = list.currentIndex >= 0 && list.currentIndex < matches.length ? matches[list.currentIndex] : null
        if (!c || !c.enabled) return
        root.close()
        c.run()
    }
    onOpened: { gather(); query.text = ""; list.currentIndex = 0; query.focusInput() }
    onMatchesChanged: list.currentIndex = matches.length > 0 ? 0 : -1

    contentItem: Column {
        spacing: Theme.s1
        SearchField {
            id: query
            width: parent.width
            placeholder: qsTr("Type a command…")
            tip: qsTr("Search menu commands by name. Use the arrow keys to choose one and Return to run it; dimmed commands are unavailable in the current selection or workspace.")
            live: false
            clearOnEscape: false
            onEscapePressed: root.close()
            onAccepted: root.runCurrent()
            Keys.onDownPressed: list.currentIndex = Math.min(list.count - 1, list.currentIndex + 1)
            Keys.onUpPressed: list.currentIndex = Math.max(0, list.currentIndex - 1)
        }
        ListView {
            id: list
            objectName: "paletteList"
            width: parent.width
            height: Math.min(contentHeight, Theme.hRow * 12)
            clip: true
            model: root.matches
            boundsBehavior: Flickable.StopAtBounds
            C.ScrollBar.vertical: ScrollBar {}
            highlightMoveDuration: 0
            delegate: Rectangle {
                id: row
                required property int index
                required property var modelData
                width: list.width; height: Theme.hRow
                radius: Theme.rControl
                color: commandHover.hovered ? Theme.hoverBg : list.currentIndex === index ? Theme.controlBg : "transparent"
                opacity: modelData.enabled ? 1 : Theme.disabledOpacity
                Text {
                    anchors.left: parent.left; anchors.leftMargin: Theme.s2
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: hint.left; anchors.rightMargin: Theme.s2
                    text: row.modelData.label
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
                    color: list.currentIndex === row.index ? Theme.textPrimary : Theme.textSecondary
                }
                Row {
                    id: hint
                    anchors.right: parent.right; anchors.rightMargin: Theme.s2
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.s2
                    Text { text: row.modelData.path; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted; anchors.verticalCenter: parent.verticalCenter }
                    Text { visible: row.modelData.shortcut !== ""; text: row.modelData.shortcut; font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted; anchors.verticalCenter: parent.verticalCenter }
                }
                TapHandler { onTapped: { list.currentIndex = row.index; root.runCurrent() } }
                HoverHandler { id: commandHover; onHoveredChanged: if (hovered) list.currentIndex = row.index }
                Tooltip {
                    text: row.modelData.label
                    shortcut: row.modelData.shortcut || ""
                    description: (row.modelData.tip || "") + (row.modelData.enabled ? "" : qsTr("\nUnavailable in the current selection or workspace."))
                    visible: root.opened && commandHover.hovered
                }
            }
        }
        Text {
            visible: list.count === 0
            width: parent.width; height: Theme.hRow
            text: qsTr("No command matches")
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
        }
    }
}
