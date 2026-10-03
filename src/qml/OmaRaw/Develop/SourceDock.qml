import QtQuick
import QtQuick.Controls.Basic as C
import QtCore
import OmaRaw.Ui

// Keep panel instances alive while changing their order: presets and snapshot
// comparisons own state which must survive a layout change.
Item {
    id: root
    objectName: "developSourceDock"
    default property alias panels: content.data
    readonly property var panelItems: content.children.filter(p => p.panelKey !== undefined)
    readonly property var defaultOrder: panelItems.map(p => p.panelKey)
    property var panelOrder: []
    property var sizes: ({})
    // Reserve the scrollbar gutter even when it is hidden. Tying width to
    // content height makes wrapped messages feed back into the panel heights.
    readonly property real panelWidth: Math.max(0, scroll.width - Theme.s2)
    property bool restored: false
    property var movingPanel: null
    property bool resizing: false
    property real pointerY: 0
    property int insertionIndex: 0
    property real insertionY: 0
    Settings { id: settings; category: "develop-source-dock"; property string layout: "" }

    function panel(key) { return panelItems.find(p => p.panelKey === key) }
    function panelY(item) {
        let y = 0
        for (const key of panelOrder) {
            if (key === item.panelKey) break
            const p = panel(key)
            if (p) y += p.height
        }
        return y
    }
    function limit(key, fallback) {
        return sizes[key] !== undefined ? sizes[key] * Theme.hRow : fallback
    }
    function resizePanel(key, height) {
        const next = Object.assign({}, sizes)
        next[key] = Math.max(2, Math.min(32, height / Theme.hRow))
        sizes = next
    }
    function persist() {
        if (restored) { settings.layout = JSON.stringify({order: panelOrder, sizes: sizes}); settings.sync() }
    }
    function resetLayout() {
        movingPanel = null
        panelOrder = defaultOrder.slice(); sizes = ({})
        scroll.contentY = 0
        persist()
    }
    function movePanel(key, offset) {
        const next = panelOrder.slice(), from = next.indexOf(key)
        const to = Math.max(0, Math.min(next.length - 1, from + offset))
        if (from < 0 || from === to) return
        next.splice(from, 1); next.splice(to, 0, key); panelOrder = next
        persist()
        const item = panel(key)
        scroll.contentY = Math.max(0, Math.min(item.y, scroll.contentHeight - scroll.height))
    }
    function beginMove(item, y) { scroll.cancelFlick(); movingPanel = item; updateMove(y) }
    function updateMove(y) {
        if (!movingPanel) return
        pointerY = y
        const at = y + scroll.contentY
        const others = panelOrder.filter(key => key !== movingPanel.panelKey)
        let index = 0
        while (index < others.length) {
            const p = panel(others[index])
            if (at < p.y + p.height / 2) break
            ++index
        }
        insertionIndex = index
        insertionY = index < others.length ? panel(others[index]).y : content.height
    }
    function finishMove() {
        if (!movingPanel) return
        const next = panelOrder.filter(key => key !== movingPanel.panelKey)
        next.splice(insertionIndex, 0, movingPanel.panelKey)
        movingPanel = null; panelOrder = next
        persist()
    }
    Component.onCompleted: {
        let saved = ({})
        try { saved = JSON.parse(settings.layout) || ({}) } catch (_) {}
        const order = Array.isArray(saved.order) ? saved.order : []
        panelOrder = order.filter((key, i) => defaultOrder.indexOf(key) >= 0 && order.indexOf(key) === i)
            .concat(defaultOrder.filter(key => order.indexOf(key) < 0))
        const valid = ({})
        for (const key of defaultOrder) {
            const value = saved.sizes ? saved.sizes[key] : undefined
            if (typeof value === "number" && isFinite(value)) valid[key] = Math.max(2, Math.min(32, value))
        }
        sizes = valid; restored = true
    }
    onVisibleChanged: if (!visible) movingPanel = null
    Flickable {
        id: scroll
        objectName: "developSourceScroll"
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.height
        clip: true
        interactive: !root.movingPanel && !root.resizing
        boundsBehavior: Flickable.StopAtBounds
        onContentHeightChanged: contentY = Math.max(0, Math.min(contentY, contentHeight - height))
        onHeightChanged: contentY = Math.max(0, Math.min(contentY, contentHeight - height))
        C.ScrollBar.vertical: ScrollBar { policy: scroll.contentHeight > scroll.height ? C.ScrollBar.AlwaysOn : C.ScrollBar.AlwaysOff }
        Item {
            id: content
            width: root.panelWidth
            height: root.panelItems.reduce((total, p) => total + p.height, 0)
        }
    }
    // A header follows the pointer; the line marks the final insertion point.
    Rectangle {
        visible: root.movingPanel !== null
        y: Math.max(0, Math.min(root.height - height, root.pointerY - height / 2))
        width: root.panelWidth; height: Theme.hDockHeader
        color: Theme.panelRaised; border.color: Theme.accent; border.width: Theme.hairline
        Text { anchors.centerIn: parent; text: root.movingPanel ? root.movingPanel.title : ""; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl }
    }
    Rectangle {
        visible: root.movingPanel !== null
        y: Math.max(0, Math.min(root.height - height, root.insertionY - scroll.contentY))
        width: root.panelWidth; height: Theme.hairline * 2; color: Theme.accent
    }
    Timer {
        interval: 16; repeat: true; running: root.movingPanel !== null
        onTriggered: {
            const edge = Theme.hRow
            const delta = root.pointerY < edge ? -Theme.s2 : root.pointerY > root.height - edge ? Theme.s2 : 0
            scroll.contentY = Math.max(0, Math.min(scroll.contentHeight - scroll.height, scroll.contentY + delta))
            root.updateMove(root.pointerY)
        }
    }
}
