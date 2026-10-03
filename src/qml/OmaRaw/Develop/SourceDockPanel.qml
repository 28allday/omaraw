import QtQuick
import OmaRaw.Ui

Item {
    id: root
    required property var dock
    required property string panelKey
    required property string title
    property real defaultListHeight: Theme.hRow * 8
    property real listHeight: 0
    property bool resizable: true
    readonly property real maximumListHeight: dock.limit(panelKey, defaultListHeight)
    default property alias content: body.data
    objectName: "sourcePanel_" + panelKey
    width: dock.panelWidth
    height: body.childrenRect.height + Theme.s3
    y: dock.panelY(root)
    opacity: dock.movingPanel === root ? 0.4 : 1
    Item { id: body; width: parent.width; height: childrenRect.height }
    MouseArea {
        id: resizeHandle
        objectName: "sourceResize_" + root.panelKey
        y: body.height; width: parent.width; height: Theme.s3
        enabled: root.resizable && root.listHeight > 0
        hoverEnabled: true; preventStealing: true
        cursorShape: Qt.SizeVerCursor
        property real startY: 0
        property real startHeight: 0
        onPressed: mouse => {
            startY = mapToItem(root.dock, mouse.x, mouse.y).y
            startHeight = root.listHeight
            root.dock.resizing = true
        }
        onPositionChanged: mouse => {
            if (pressed) root.dock.resizePanel(root.panelKey, startHeight + mapToItem(root.dock, mouse.x, mouse.y).y - startY)
        }
        onReleased: { root.dock.resizing = false; root.dock.persist() }
        onCanceled: { root.dock.resizing = false; root.dock.persist() }
        onDoubleClicked: {
            const next = Object.assign({}, root.dock.sizes); delete next[root.panelKey]
            root.dock.sizes = next; root.dock.persist()
        }
        Rectangle {
            anchors.centerIn: parent; width: Theme.s5 * 2; height: Theme.hairline
            color: resizeHandle.pressed || resizeHandle.containsMouse ? Theme.accent : Theme.borderStrong
            visible: resizeHandle.enabled
        }
        Tooltip { text: qsTr("Drag to resize %1").arg(root.title); description: qsTr("Double-click to restore its default height."); visible: resizeHandle.containsMouse && !resizeHandle.pressed }
    }
}
