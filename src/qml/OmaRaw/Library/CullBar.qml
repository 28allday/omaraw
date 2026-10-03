import QtQuick
import QtQuick.Layouts
import OmaRaw.Ui

// Under the grid: view modes and the culling
// controls for the selection — stars, labels, pick/reject.
Rectangle {
    id: root
    property var shell: null
    property var currentInfo: ({})
    implicitHeight: Theme.hToolbar
    color: Theme.panelBg
    Rectangle { width: parent.width; height: Theme.hairline; color: Theme.border }

    RowLayout {
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: Theme.s3; anchors.rightMargin: Theme.s3
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.s2
        Row {
        visible: root.width >= 1050
        spacing: Theme.s2
        IconButton { anchors.verticalCenter: parent.verticalCenter; iconName: "layout-grid"; text: qsTr("Grid"); shortcut: "G"; checked: root.shell.browserMode === "grid"; onClicked: root.shell.browserMode = "grid" }
        IconButton { anchors.verticalCenter: parent.verticalCenter; iconName: "image"; text: qsTr("Loupe"); shortcut: "E"; checked: root.shell.browserMode === "loupe"; onClicked: root.shell.browserMode = "loupe" }
        Rectangle { width: Theme.hairline; height: Theme.hControl; color: Theme.border; anchors.verticalCenter: parent.verticalCenter }
        IconButton { anchors.verticalCenter: parent.verticalCenter; iconName: "columns-2"; text: qsTr("Compare"); shortcut: "C"; checked: root.shell.browserMode === "compare"; onClicked: root.shell.browserMode = "compare" }
        IconButton { anchors.verticalCenter: parent.verticalCenter; iconName: "layout-dashboard"; text: qsTr("Survey"); shortcut: "N"; checked: root.shell.browserMode === "survey"; onClicked: root.shell.browserMode = "survey" }
        Rectangle { width: Theme.hairline; height: Theme.hControl; color: Theme.border; anchors.verticalCenter: parent.verticalCenter }
        }
        ToolButton {
            iconName: "flag"; text: qsTr("Pick"); showLabel: root.width > 550; shortcut: "P"
            tip: qsTr("Flags the photo as a keeper; press again to unflag.")
            enabled: backend.currentId > 0
            checked: (root.currentInfo.flag || 0) > 0
            onClicked: backend.toggleFlag(1)
        }
        ToolButton {
            iconName: "x"; text: qsTr("Reject"); showLabel: root.width > 550; shortcut: "X"
            tip: qsTr("Flags the photo as a reject; press again to unflag.")
            enabled: backend.currentId > 0
            checked: (root.currentInfo.flag || 0) < 0
            onClicked: backend.toggleFlag(-1)
        }
        Item { Layout.fillWidth: true }
        RatingStars {
            size: 14; interactive: backend.currentId > 0
            rating: root.currentInfo.rating || 0
            onRated: r => backend.setRating(r)
        }
        Rectangle { width: Theme.hairline; height: Theme.hControl; color: Theme.border }
        LabelDots {
            picker: true; size: 12
            label: root.currentInfo.label || ""
            onPicked: l => backend.toggleLabel(l)
        }
        Item { Layout.fillWidth: true }
        Text {
            visible: root.width > 900 && backend.selectedCount > 1
            text: qsTr("%1 selected").arg(backend.selectedCount)
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
        }
        ToolButton {
            iconName: "plus"; text: qsTr("Quick Collection"); showLabel: root.width > 700; shortcut: "B"
            tip: qsTr("Adds the selection to the Quick Collection, a scratch album for the session.")
            enabled: backend.currentId > 0
            onClicked: backend.addSelectionToQuickCollection()
        }
    }
}
