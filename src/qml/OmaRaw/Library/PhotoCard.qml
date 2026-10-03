import QtQuick
import OmaRaw.Ui

// A grid cell: header (filename, format badge), the preview, footer
// (stars, label dots, flag, capture time). 2 px cyan ring when selected.
Rectangle {
    id: root
    property int assetId: 0
    property string filename: ""
    property string format: ""
    property bool isRaw: false
    property string thumb: ""
    property int rating: 0
    property int flag: 0
    property string label: ""
    property string capturedTime: ""
    property bool selected: false
    property bool current: false
    property bool offline: false
    property bool edited: false
    // What the edit holds: 1 crop, 2 local adjustment, 4 retouch spot.
    property int editFlags: 0
    property bool hasGps: false
    property bool sidecarStale: false
    property int variant: 0
    property string variantLabel: ""
    property int stackCount: 0
    property int stackPos: 0
    property bool stackExpanded: false
    objectName: "photoCard"
    property bool showInfo: width >= 150
    signal clicked(int modifiers)
    signal doubleClicked()
    signal rated(int rating)
    signal contextRequested(real x, real y)

    color: selected ? Theme.panelRaised : Theme.panelBg
    border.width: selected ? Theme.selectionRing : Theme.hairline
    border.color: selected ? Theme.accent : Theme.border
    radius: 0
    opacity: offline && img.status !== Image.Ready ? 0.6 : (flag < 0 ? 0.5 : 1)

    HoverHandler { id: hover }
    TapHandler {
        id: selectionTap
        acceptedButtons: Qt.LeftButton
        onTapped: root.clicked(selectionTap.point.modifiers)
        onDoubleTapped: if (!(selectionTap.point.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) root.doubleClicked()
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: ev => { root.clicked(root.selected ? Qt.ControlModifier | 0x80000000 : 0); root.contextRequested(ev.position.x, ev.position.y) }
    }

    // header
    Item {
        id: header
        visible: root.showInfo
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.s2
        height: root.showInfo ? Theme.hRow - 4 : 0
        Text {
            anchors.left: parent.left
            anchors.right: badges.left
            anchors.rightMargin: Theme.s2
            anchors.verticalCenter: parent.verticalCenter
            text: root.variant ? root.filename + "  ·  " + root.variantLabel : root.filename
            textFormat: Text.PlainText
            elide: Text.ElideMiddle
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
            color: root.selected ? Theme.textPrimary : Theme.textSecondary
        }
        // badges: edited, and what the edit holds; a pin for a photo with a place
        Row {
            id: badges
            anchors.right: badge.left; anchors.rightMargin: Theme.s1
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Icon { visible: root.sidecarStale; name: "triangle-alert"; size: 12; color: Theme.warning }
            Icon { visible: root.hasGps; name: "map-pin"; size: 12; color: Theme.textMuted }
            Icon { visible: root.edited && (root.editFlags & 1); name: "crop"; size: 12; color: Theme.accent }
            Icon { visible: root.edited && (root.editFlags & 2); name: "circle-dashed"; size: 12; color: Theme.accent }
            Icon { visible: root.edited && (root.editFlags & 4); name: "bandage"; size: 12; color: Theme.accent }
            Icon { visible: root.edited; name: "sliders-horizontal"; size: 12; color: Theme.accent }
        }
        Rectangle {
            id: badge
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: badgeText.implicitWidth + Theme.s2; height: 16
            radius: Theme.rControl
            color: root.isRaw ? Theme.controlBg : "transparent"
            border.width: Theme.hairline; border.color: Theme.borderStrong
            Text {
                id: badgeText
                anchors.centerIn: parent
                text: root.format
                font.family: Theme.monoFamily; font.pixelSize: 9; font.weight: Theme.wHeading
                color: root.isRaw ? Theme.accent : Theme.textMuted
            }
        }
    }

    // preview
    Rectangle {
        id: frame
        anchors.top: header.bottom
        anchors.bottom: footer.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.s2
        anchors.topMargin: root.showInfo ? Theme.s1 : Theme.s2
        anchors.bottomMargin: root.showInfo ? Theme.s1 : Theme.s2
        color: Theme.windowBg
        Image {
            id: img
            anchors.fill: parent
            source: root.thumb
            asynchronous: true
            cache: true
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
            sourceSize.width: 0
        }
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            visible: img.status === Image.Loading
            Icon { anchors.centerIn: parent; name: "image"; color: Theme.textMuted; size: 20 }
        }
        Column {
            anchors.centerIn: parent
            visible: img.status === Image.Error || (root.offline && img.status !== Image.Ready && img.status !== Image.Loading)
            spacing: Theme.s1
            Icon { anchors.horizontalCenter: parent.horizontalCenter; name: root.offline ? "unplug" : "image-off"; color: Theme.textMuted; size: 20 }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.offline ? qsTr("Offline") : qsTr("No preview")
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
        }
        Rectangle {
            anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: Theme.s1
            visible: root.offline && img.status === Image.Ready
            width: offlineLabel.implicitWidth + Theme.s2; height: offlineLabel.implicitHeight + Theme.s1
            radius: Theme.rControl; color: Theme.scrim
            Text {
                id: offlineLabel
                anchors.centerIn: parent
                text: qsTr("Original offline")
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textPrimary
            }
        }
        // variant marker: a chip in the preview's corner so it reads at every card size
        Rectangle {
            anchors.left: parent.left; anchors.top: parent.top; anchors.margins: Theme.s1
            visible: root.variant > 0
            width: vrow.implicitWidth + Theme.s2; height: 16
            radius: Theme.rControl; color: Theme.scrim
            Row {
                id: vrow
                anchors.centerIn: parent
                spacing: 3
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "copy"; size: 10; color: Theme.accent }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.showInfo ? root.variantLabel : String(root.variant)
                    font.family: Theme.monoFamily; font.pixelSize: 9; font.weight: Theme.wHeading
                    color: Theme.textPrimary
                }
            }
        }
        // stack badge: the top of a collapsed stack says how many it holds;
        // members of an expanded stack carry a small marker
        Rectangle {
            anchors.right: parent.right; anchors.top: parent.top; anchors.margins: Theme.s1
            visible: root.stackCount > 1
            width: srow.implicitWidth + Theme.s2; height: 16
            radius: Theme.rControl
            color: root.stackPos === 0 && !root.stackExpanded ? Theme.accent : Theme.scrim
            Row {
                id: srow
                anchors.centerIn: parent
                spacing: 3
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "layers"; size: 10; color: root.stackPos === 0 && !root.stackExpanded ? Theme.accentText : Theme.accent }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.stackPos === 0 ? String(root.stackCount) : (root.stackPos + 1) + "/" + root.stackCount
                    font.family: Theme.monoFamily; font.pixelSize: 9; font.weight: Theme.wHeading
                    color: root.stackPos === 0 && !root.stackExpanded ? Theme.accentText : Theme.textPrimary
                }
            }
            TapHandler { onTapped: backend.toggleStack(root.assetId) }
            HoverHandler { id: stackHover }
            Tooltip {
                text: root.stackExpanded ? qsTr("Collapse the stack") : qsTr("Expand the stack")
                description: qsTr("%1 photos are stacked here; the top one stands for the rest.").arg(root.stackCount)
                visible: stackHover.hovered
            }
        }
        // current-item marker: a thin inner line so it reads even when
        // several cards are selected
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.width: root.current && root.selected ? 1 : 0
            border.color: Theme.accent
            opacity: 0.6
        }
    }

    // footer
    Item {
        id: footer
        visible: root.showInfo
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.s2
        height: root.showInfo ? Theme.hRow - 4 : 0
        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.s2
            RatingStars {
                anchors.verticalCenter: parent.verticalCenter
                rating: root.rating; interactive: true
                onRated: r => root.rated(r)
            }
            LabelDots { anchors.verticalCenter: parent.verticalCenter; label: root.label }
            FlagMark { anchors.verticalCenter: parent.verticalCenter; flag: root.flag }
        }
        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: root.capturedTime
            font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel
            color: Theme.textMuted
        }
    }
}
