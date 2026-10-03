import QtQuick

// Header strip of a dock group: a row of TabButtons and an optional
// trailing action slot (the ≡ menu). Exposes currentIndex.
Rectangle {
    id: root
    property var tabs: []
    // One description per tab, for the tooltips; may be shorter than tabs.
    property var tips: []
    property int currentIndex: 0
    property alias trailing: trailingSlot.data
    // Names a help page; when set, a ? before the trailing slot opens it.
    property string helpSection: ""
    // Opt-in drag grip for the configurable Develop source dock.
    property var dockPanel: null
    signal activated(int index)

    implicitHeight: Theme.hDockHeader
    color: Theme.panelBg

    // Compress padding, then use a selector when labels cannot remain distinct.
    // Measure with the bold face, which the active tab wears.
    FontMetrics { id: fm; font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl; font.weight: Theme.wHeading }
    readonly property real labelSum: { let s = 0; for (const t of root.tabs) s += fm.advanceWidth(t); return s }
    readonly property real available: root.width - Theme.s1 * 2 - moveGrip.width - trailingSlot.width - (helpButton.visible ? helpButton.width : 0)
    readonly property bool compact: root.tabs.length > 1 && available < labelSum + root.tabs.length * 4
    readonly property int hPad: root.tabs.length ? Math.max(2, Math.min(Theme.s3, Math.floor((available - labelSum) / (2 * root.tabs.length)))) : Theme.s3

    Row {
        visible: !root.compact
        anchors.left: parent.left
        anchors.leftMargin: Theme.s1 + moveGrip.width
        anchors.bottom: parent.bottom
        height: parent.height
        Repeater {
            model: root.tabs
            TabButton {
                required property int index
                required property var modelData
                text: modelData
                tip: index < root.tips.length ? root.tips[index] : ""
                uppercase: false
                hPad: root.hPad
                height: root.height
                checked: root.currentIndex === index
                // The caller owns currentIndex; a click on the lit tab must not
                // leave it unlit, so the binding is re-made before handing over.
                onClicked: { checked = Qt.binding(() => root.currentIndex === index); root.activated(index) }
            }
        }
    }
    ComboField {
        visible: root.compact
        x: Theme.s1 + moveGrip.width; width: root.available
        anchors.verticalCenter: parent.verticalCenter
        model: root.tabs; currentIndex: root.currentIndex
        tipTitle: qsTr("Adjustment group")
        onActivated: i => root.activated(i)
    }
    Item {
        id: moveGrip
        objectName: root.dockPanel ? "sourceMove_" + root.dockPanel.panelKey : ""
        width: root.dockPanel ? Theme.s5 : 0; height: parent.height
        visible: root.dockPanel !== null
        activeFocusOnTab: visible
        Accessible.role: Accessible.Button
        Accessible.name: root.dockPanel ? qsTr("Move %1 panel").arg(root.dockPanel.title) : ""
        Accessible.onPressAction: moveMenu.popup()
        Keys.onUpPressed: root.dockPanel.dock.movePanel(root.dockPanel.panelKey, -1)
        Keys.onDownPressed: root.dockPanel.dock.movePanel(root.dockPanel.panelKey, 1)
        Icon { anchors.centerIn: parent; name: "grip-vertical"; size: Theme.s4; color: moveArea.containsMouse || moveGrip.activeFocus ? Theme.accent : Theme.textMuted }
        MouseArea {
            id: moveArea
            anchors.fill: parent
            hoverEnabled: true; preventStealing: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            onPressed: mouse => {
                if (mouse.button === Qt.RightButton) { moveMenu.popup(); return }
                moveGrip.forceActiveFocus()
                root.dockPanel.dock.beginMove(root.dockPanel, mapToItem(root.dockPanel.dock, mouse.x, mouse.y).y)
            }
            onPositionChanged: mouse => { if (pressedButtons & Qt.LeftButton) root.dockPanel.dock.updateMove(mapToItem(root.dockPanel.dock, mouse.x, mouse.y).y) }
            onReleased: mouse => { if (mouse.button === Qt.LeftButton) root.dockPanel.dock.finishMove() }
            onCanceled: if (root.dockPanel) root.dockPanel.dock.movingPanel = null
        }
        Tooltip { text: qsTr("Drag to move this panel"); description: qsTr("Use Up/Down when focused, or right-click for layout options."); visible: moveArea.containsMouse && !moveArea.pressed }
        ContextMenu {
            id: moveMenu
            objectName: root.dockPanel ? "sourceMenu_" + root.dockPanel.panelKey : ""
            MenuAction { text: qsTr("Move up"); enabled: root.dockPanel && root.dockPanel.dock.panelOrder.indexOf(root.dockPanel.panelKey) > 0; onTriggered: root.dockPanel.dock.movePanel(root.dockPanel.panelKey, -1) }
            MenuAction { text: qsTr("Move down"); enabled: root.dockPanel && root.dockPanel.dock.panelOrder.indexOf(root.dockPanel.panelKey) < root.dockPanel.dock.panelOrder.length - 1; onTriggered: root.dockPanel.dock.movePanel(root.dockPanel.panelKey, 1) }
            MenuAction { objectName: "resetSourceLayout"; text: qsTr("Reset panel layout"); onTriggered: root.dockPanel.dock.resetLayout() }
        }
    }
    IconButton {
        id: helpButton
        objectName: "dockHelp"
        anchors.right: trailingSlot.left
        anchors.verticalCenter: parent.verticalCenter
        visible: root.helpSection !== ""
        flat: true
        iconName: "circle-help"; text: qsTr("Help for this panel"); shortcut: "F1"
        tip: qsTr("Opens the guide for this panel.")
        onClicked: { const w = root.Window.window; if (w && w.help) w.help(root.helpSection) }
    }
    Item {
        id: trailingSlot
        anchors.right: parent.right
        anchors.rightMargin: Theme.s1
        anchors.verticalCenter: parent.verticalCenter
        width: childrenRect.width; height: parent.height
    }
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width; height: Theme.hairline
        color: Theme.border
    }
}
