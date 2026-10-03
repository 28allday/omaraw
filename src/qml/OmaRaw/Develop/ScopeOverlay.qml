pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// A nonmodal scope over the viewer: adjustments stay available in the dock.
Rectangle {
    id: root
    objectName: "largeScopeOverlay"
    property real preferredWidth: 760
    property real preferredHeight: 460
    readonly property real maximumWidth: Math.max(0, Math.min(760, parent.width - Theme.s3 * 2))
    readonly property real maximumHeight: Math.max(0, Math.min(460, parent.height - Theme.hControl * 2 - Theme.s3 * 2))
    readonly property real minimumWidth: Math.min(380, maximumWidth)
    readonly property real minimumHeight: Math.min(230, maximumHeight)
    width: Math.min(preferredWidth, maximumWidth)
    height: Math.min(preferredHeight, maximumHeight)
    property real positionX: 0.5
    property real positionY: 0
    readonly property real travelX: Math.max(0, parent.width - width - Theme.s3 * 2)
    readonly property real travelY: Math.max(0, parent.height - height - Theme.hControl * 2 - Theme.s3 * 2)
    // Normalised positions keep the panel in the viewer when docks/window resize.
    x: Theme.s3 + positionX * travelX
    y: Theme.s3 + positionY * travelY
    color: Theme.panelBg; border.color: Theme.borderStrong; border.width: Theme.hairline
    radius: Theme.rMenu
    z: 100
    // Reading or dragging a scope must not pan, zoom or paint on the photo.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }
    Item {
        id: header
        x: Theme.s2; y: Theme.s2; width: parent.width - Theme.s2 * 2; height: Theme.hControl
        MouseArea {
            id: drag
            objectName: "largeScopeDrag"
            anchors.fill: parent
            cursorShape: Qt.SizeAllCursor
            property point start
            property point panelStart
            onPressed: mouse => { start = mapToItem(null, mouse.x, mouse.y); panelStart = Qt.point(root.x, root.y) }
            onPositionChanged: mouse => {
                if (!pressed) return
                const now = mapToItem(null, mouse.x, mouse.y)
                if (root.travelX > 0) root.positionX = Math.max(0, Math.min(1, (panelStart.x + now.x - start.x - Theme.s3) / root.travelX))
                if (root.travelY > 0) root.positionY = Math.max(0, Math.min(1, (panelStart.y + now.y - start.y - Theme.s3) / root.travelY))
            }
        }
        Text {
            id: title
            anchors.left: parent.left; anchors.leftMargin: Theme.s2
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Scopes")
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading
            color: Theme.textPrimary
        }
        ComboField {
            objectName: "largeScopeSelector"
            anchors.left: title.right; anchors.leftMargin: Theme.s4
            anchors.right: compact.left; anchors.rightMargin: Theme.s2
            anchors.verticalCenter: parent.verticalCenter
            model: plot.labels; currentIndex: engine.scopeMode
            tipTitle: qsTr("Scope"); tip: qsTr("Choose the scope shown in both compact and large views.")
            onActivated: index => engine.scopeMode = index
        }
        ToolButton {
            id: compact
            objectName: "compactScopeButton"
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            iconName: "minimize-2"; text: qsTr("Compact"); showLabel: root.width >= 450
            tip: qsTr("Return to the small scope in the adjustment dock. Escape also closes this panel.")
            onClicked: engine.scopeExpanded = false
        }
    }
    ScopeView {
        id: plot
        large: true
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: header.bottom; anchors.topMargin: Theme.s1
        anchors.bottom: parent.bottom
    }
    MouseArea {
        id: resize
        objectName: "largeScopeResize"
        anchors.right: parent.right; anchors.bottom: parent.bottom
        width: Theme.s5; height: Theme.s5
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        cursorShape: Qt.SizeFDiagCursor
        property point start
        property point panelStart
        property size panelSize
        Accessible.role: Accessible.Grip
        Accessible.name: qsTr("Resize scopes")
        onPressed: mouse => {
            start = mapToItem(null, mouse.x, mouse.y)
            panelStart = Qt.point(root.x, root.y)
            panelSize = Qt.size(root.width, root.height)
        }
        onPositionChanged: mouse => {
            if (!pressed) return
            const now = mapToItem(null, mouse.x, mouse.y)
            const rightRoom = root.parent.width - Theme.s3 - panelStart.x
            const bottomRoom = root.parent.height - Theme.hControl * 2 - Theme.s3 - panelStart.y
            root.preferredWidth = Math.max(root.minimumWidth, Math.min(root.maximumWidth, rightRoom, panelSize.width + now.x - start.x))
            root.preferredHeight = Math.max(root.minimumHeight, Math.min(root.maximumHeight, bottomRoom, panelSize.height + now.y - start.y))
            // Keep the opposite corner still while the resize grip moves.
            root.positionX = root.travelX > 0 ? Math.max(0, Math.min(1, (panelStart.x - Theme.s3) / root.travelX)) : 0
            root.positionY = root.travelY > 0 ? Math.max(0, Math.min(1, (panelStart.y - Theme.s3) / root.travelY)) : 0
        }
        Repeater {
            model: 3
            Rectangle {
                required property int index
                x: resize.width - Theme.s1 * (index + 2)
                y: resize.height - Theme.s1 * (index + 2)
                width: Theme.s1 * (index + 1) * Math.SQRT2
                height: Theme.hairline
                rotation: -45
                color: resize.containsMouse || resize.pressed ? Theme.accent : Theme.textMuted
            }
        }
        Tooltip {
            text: qsTr("Resize scopes")
            description: qsTr("Drag this corner to resize the panel down to half size.")
            visible: resize.containsMouse && !resize.pressed
        }
    }
}
