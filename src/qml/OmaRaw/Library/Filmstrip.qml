pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

// Bottom strip: the same assets model as the grid, one thumbnail per
// photo, following the selection. Shared by every workspace.
Rectangle {
    id: root
    property var shell: null
    color: Theme.panelBg
    Rectangle { width: parent.width; height: Theme.hairline; color: Theme.border }

    IconButton {
        id: prev
        objectName: "filmstripPrevious"
        anchors.left: parent.left
        anchors.leftMargin: Theme.s1
        anchors.verticalCenter: parent.verticalCenter
        iconName: "chevron-left"; text: qsTr("Previous"); shortcut: "←"
        tip: qsTr("Makes the photo before this one current.")
        onClicked: backend.step(-1)
    }
    IconButton {
        id: next
        objectName: "filmstripNext"
        anchors.right: parent.right
        anchors.rightMargin: Theme.s1
        anchors.verticalCenter: parent.verticalCenter
        iconName: "chevron-right"; text: qsTr("Next"); shortcut: "→"
        tip: qsTr("Makes the photo after this one current.")
        onClicked: backend.step(1)
    }
    ListView {
        id: strip
        objectName: "filmstripView"
        anchors.left: prev.right
        anchors.right: next.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: Theme.s2
        anchors.bottomMargin: Theme.s2 + scrollBar.height
        orientation: ListView.Horizontal
        clip: true
        model: assets
        spacing: Theme.s1
        boundsBehavior: Flickable.StopAtBounds
        function revealCurrent() {
            forceLayout()
            const row = assets.rowOf(backend.currentId)
            currentIndex = row
            if (row >= 0) positionViewAtIndex(row, ListView.Contain)
        }
        // The selection may already exist when a workspace is opened.
        // Wait for the delegates' first layout before locating its thumbnail.
        Component.onCompleted: Qt.callLater(revealCurrent)
        onCountChanged: Qt.callLater(revealCurrent)
        onWidthChanged: Qt.callLater(revealCurrent)
        onHeightChanged: Qt.callLater(revealCurrent)
        readonly property int thumbW: Math.round((height - Theme.s2) * 1.2)
        C.ScrollBar.horizontal: ScrollBar {
            id: scrollBar
            objectName: "filmstripScrollBar"
            parent: root
            anchors.left: strip.left
            anchors.right: strip.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.s1
            height: Theme.s4
            policy: C.ScrollBar.AlwaysOn
            enabled: size < 1
            padding: Theme.s1
            Accessible.name: qsTr("Scroll photo previews")
            contentItem: Rectangle {
                implicitWidth: Theme.wScrollBar
                implicitHeight: Theme.wScrollBar
                radius: height / 2
                color: scrollBar.pressed ? Theme.textPrimary
                     : scrollBar.hovered ? Theme.textSecondary : Theme.textMuted
                opacity: scrollBar.enabled ? 1 : Theme.disabledOpacity
            }
            background: Rectangle {
                color: Theme.windowBg
                radius: Theme.rControl
            }
        }
        Connections {
            target: backend
            function onSelectionChanged() { strip.revealCurrent() }
        }
        Connections {
            target: assets
            function onModelReset() { Qt.callLater(strip.revealCurrent) }
        }
        delegate: Rectangle {
            id: cell
            required property int index
            required property int assetId
            required property string thumb
            required property int rating
            required property string label
            required property int flag
            required property bool selected
            required property bool current
            required property int variant
            required property int stackId
            required property int stackPos
            required property int stackCount
            // A stack is outlined as one group: a collapsed one alone, an open
            // one across each unbroken run of its members.
            readonly property bool stacked: stackCount > 1
            readonly property bool expanded: stackId ? backend.isStackExpanded(stackId) : false
            readonly property bool joinsLeft: stacked && index > 0 && assets.stackIdAt(index - 1) === stackId
            readonly property bool joinsRight: stacked && index < assets.count - 1 && assets.stackIdAt(index + 1) === stackId
            width: strip.thumbW; height: strip.height
            color: selected ? Theme.panelRaised : "transparent"
            border.width: selected ? Theme.selectionRing : 0
            border.color: Theme.accent
            opacity: flag < 0 ? 0.45 : 1
            Image {
                anchors.fill: parent
                anchors.margins: Theme.s1
                anchors.bottomMargin: Theme.s3 + 2
                source: cell.thumb
                asynchronous: true
                // Edited previews arrive repeatedly during a slider drag.
                // Swap when ready instead of clearing the current thumbnail.
                retainWhileLoading: true
                fillMode: Image.PreserveAspectFit
                smooth: true; mipmap: true
            }
            Rectangle {
                anchors.left: parent.left; anchors.top: parent.top; anchors.margins: Theme.s1 + 1
                visible: cell.variant > 0
                width: 14; height: 14; radius: Theme.rControl; color: Theme.scrim
                Icon { anchors.centerIn: parent; name: "copy"; size: 9; color: Theme.accent }
            }
            StackBadge {
                anchors.right: parent.right; anchors.top: parent.top; anchors.margins: Theme.s1 + 1
                assetId: cell.assetId; stackCount: cell.stackCount; stackPos: cell.stackPos; expanded: cell.expanded
            }
            Item {
                objectName: "stackOutline_" + cell.index
                visible: cell.stacked
                anchors.fill: parent
                anchors.topMargin: 1; anchors.bottomMargin: 1
                // Bridge the gap to the next member so a run reads as one outline.
                anchors.leftMargin: cell.joinsLeft ? 0 : 1; anchors.rightMargin: cell.joinsRight ? -strip.spacing : 1
                readonly property real line: 1.5
                readonly property color ink: Theme.textSecondary
                Rectangle { width: parent.width; height: parent.line; color: parent.ink }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: parent.line; color: parent.ink }
                Rectangle { visible: !cell.joinsLeft; width: parent.line; height: parent.height; color: parent.ink }
                Rectangle { visible: !cell.joinsRight; anchors.right: parent.right; width: parent.line; height: parent.height; color: parent.ink }
            }
            Row {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 3
                anchors.left: parent.left
                anchors.leftMargin: Theme.s1
                spacing: Theme.s1
                RatingStars { rating: cell.rating; size: 8 }
                LabelDots { label: cell.label; size: 7 }
                FlagMark { flag: cell.flag; size: 9 }
            }
            TapHandler {
                id: selectionTap
                onTapped: backend.select(cell.assetId, (selectionTap.point.modifiers & Qt.ShiftModifier) ? 2 : (selectionTap.point.modifiers & Qt.ControlModifier) ? 1 : 0)
                onDoubleTapped: if (!(selectionTap.point.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) { backend.select(cell.assetId, 0); root.shell.browserMode = "loupe" }
            }
        }
    }
}
