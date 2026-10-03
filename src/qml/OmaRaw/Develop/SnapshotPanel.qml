pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as C
import OmaRaw.Ui

// Snapshots: named captures of the current photo's settings with a
// preview of the render. Click restores (one render), the eye compares
// it against the live render in the viewer, + saves, right-click renames
// or deletes. Snapshots belong to the photo (or variant) in the catalog.
Column {
    id: root
    width: parent ? parent.width : 260
    spacing: 0
    property var dockPanel: null
    readonly property real listHeight: snapList.height
    property real maximumListHeight: 4 * (Theme.hRow * 2 + Theme.s1)
    // The snapshot the viewer is comparing against (0 = none).
    property int comparing: 0
    signal compareRequested(int snapshotId, string name, string preview)
    signal compareCleared()
    signal compareOriginalRequested()

    property var snapshots: []
    property int assetId: 0
    function reload() {
        if (assetId !== backend.currentId) {
            assetId = backend.currentId
            comparing = 0; compareCleared()
        }
        snapshots = backend.currentId > 0 ? backend.snapshots(backend.currentId) : []
        if (comparing && !snapshots.some(s => s.id === comparing)) { comparing = 0; compareCleared() }
    }
    Connections {
        target: backend
        function onSnapshotsChanged(assetId) { if (assetId === backend.currentId || assetId === 0) root.reload() }
        function onSelectionChanged() { root.reload() }
    }
    Component.onCompleted: reload()

    function saveNamed(name) {
        if (engine.imageId < 0) return
        backend.saveSnapshot(backend.currentId, name)
    }

    DockHeader {
        dockPanel: root.dockPanel
        width: parent.width
        tabs: [qsTr("Snapshots")]
        helpSection: "develop"
        trailing: Row {
            anchors.verticalCenter: parent.verticalCenter
            IconButton { objectName: "snapshotCompareOriginal"; iconName: "columns-2"; text: qsTr("Compare with the original (\\)"); tip: qsTr("Shows the photo as it came from the camera beside the current render."); enabled: engine.imageId >= 0 && engine.historyEnd > 0; onClicked: { root.comparing = 0; root.compareOriginalRequested() } }
            IconButton { objectName: "snapshotSave"; iconName: "plus"; text: qsTr("Save a snapshot of the current settings (Ctrl+N)"); tip: qsTr("Keeps this state to come back to or compare against, without touching History."); enabled: engine.imageId >= 0 && !engine.busy; onClicked: saveDialog.open() }
        }
    }
    // Four snapshots by default; the dock divider can change the height.
    ListView {
        id: snapList
        objectName: "snapshotList"
        // Shows there is more above or below once the list is capped.
        C.ScrollBar.vertical: ScrollBar { id: listBar; policy: snapList.contentHeight > snapList.height ? C.ScrollBar.AlwaysOn : C.ScrollBar.AlwaysOff }
        width: parent.width
        height: Math.min(count * (Theme.hRow * 2 + Theme.s1), root.maximumListHeight)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.snapshots
        delegate: Rectangle {
            id: row
            required property int index
            required property var modelData
            readonly property bool active: root.comparing === modelData.id
            Accessible.role: Accessible.ListItem
            Accessible.name: modelData.name + (active ? qsTr(", comparing") : "")
            width: ListView.view.width - (listBar.visible ? Theme.s2 : 0); height: Theme.hRow * 2 + Theme.s1
            color: hh.hovered ? Theme.hovered(active ? Theme.controlBg : Theme.panelBg)
                              : active ? Theme.controlBg : "transparent"
            HoverHandler { id: hh }
            Rectangle {
                id: thumbFrame
                anchors.left: parent.left; anchors.leftMargin: Theme.s3
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.hRow * 2 - 2; height: Theme.hRow * 2 - 6
                color: Theme.windowBg
                border.width: Theme.hairline; border.color: row.active ? Theme.accent : Theme.border
                Image {
                    anchors.fill: parent; anchors.margins: 1
                    source: row.modelData.hasPreview ? row.modelData.preview : ""
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true; cache: false; smooth: true
                    sourceSize.width: 96
                }
                Icon { anchors.centerIn: parent; visible: !row.modelData.hasPreview; name: "image-off"; size: 12; color: Theme.textMuted }
            }
            Column {
                anchors.left: thumbFrame.right; anchors.leftMargin: Theme.s2
                anchors.right: eye.left; anchors.rightMargin: Theme.s1
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text {
                    width: parent.width
                    textFormat: Text.PlainText
                    text: row.modelData.name
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
                    color: engine.imageId >= 0 ? Theme.textPrimary : Theme.textMuted
                }
                Text {
                    width: parent.width
                    text: qsTr("%1 · step %2").arg(String(row.modelData.createdAt).replace("T", " ").substring(0, 16)).arg(row.modelData.historyEnd)
                    elide: Text.ElideRight
                    font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                }
            }
            IconButton {
                id: eye
                anchors.right: parent.right; anchors.rightMargin: Theme.s2
                anchors.verticalCenter: parent.verticalCenter
                iconName: row.active ? "eye-off" : "eye"
                text: row.active ? qsTr("Stop comparing") : qsTr("Compare with the current render")
                tip: qsTr("Compares the saved settings with the current picture. Zoom to 100% to inspect native detail; History stays unchanged.")
                checked: row.active
                visible: row.modelData.hasPreview && (hh.hovered || row.active)
                onClicked: {
                    if (row.active) { root.comparing = 0; root.compareCleared() }
                    else { root.comparing = row.modelData.id; root.compareRequested(row.modelData.id, row.modelData.name, row.modelData.preview) }
                }
            }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                enabled: engine.imageId >= 0
                onTapped: { engine.applyValues(row.modelData.values); backend.setStatus(qsTr("Restored snapshot %1").arg(row.modelData.name)) }
                onDoubleTapped: renameDialog.openFor(row.modelData.id, row.modelData.name)
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: rowMenu.popup()
            }
            ContextMenu {
                id: rowMenu
                MenuAction { text: qsTr("Restore"); iconName: "rotate-ccw"; enabled: engine.imageId >= 0; onTriggered: engine.applyValues(row.modelData.values) }
                MenuAction {
                    text: row.active ? qsTr("Stop comparing") : qsTr("Compare with current"); iconName: "columns-2"
                    enabled: row.modelData.hasPreview
                    onTriggered: eye.clicked()
                }
                MenuAction { text: qsTr("Rename…"); iconName: "pencil"; onTriggered: renameDialog.openFor(row.modelData.id, row.modelData.name) }
                MenuAction { text: qsTr("Delete snapshot"); iconName: "trash-2"; onTriggered: backend.deleteSnapshot(row.modelData.id) }
            }
        }
    }
    Text {
        visible: root.snapshots.length === 0
        x: Theme.s3; topPadding: Theme.s2
        width: parent.width - Theme.s3 * 2; wrapMode: Text.WordWrap
        text: engine.imageId >= 0 ? qsTr("No snapshots yet.")
                                  : qsTr("Open a photo to save a snapshot.")
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
    }

    C.Popup {
        id: saveDialog
        modal: true
        parent: C.Overlay.overlay
        anchors.centerIn: parent
        width: 360; padding: Theme.s4
        background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }
        onOpened: { snapName.text = ""; snapName.forceActiveFocus() }
        Column {
            width: parent.width
            spacing: Theme.s3
            Text { text: qsTr("Save a snapshot"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading; color: Theme.textPrimary }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: qsTr("Keeps every slider, the local adjustments and the render as they are now. Restoring later applies the lot in one step.")
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
            SearchField {
                id: snapName
                width: parent.width
                placeholder: qsTr("Snapshot name (optional)")
                tip: qsTr("A name to find this state by later; leave it blank for the time.")
                live: false
                onAccepted: t => { root.saveNamed(t.trim()); saveDialog.close() }
            }
            Row {
                anchors.right: parent.right
                spacing: Theme.s2
                ToolButton { text: qsTr("Cancel"); showLabel: true; onClicked: saveDialog.close() }
                ToolButton { text: qsTr("Save"); showLabel: true; onClicked: { root.saveNamed(snapName.text.trim()); saveDialog.close() } }
            }
        }
    }
    C.Popup {
        id: renameDialog
        property int snapshotId: 0
        function openFor(id, name) { snapshotId = id; renameField.text = name; open() }
        modal: true
        parent: C.Overlay.overlay
        anchors.centerIn: parent
        width: 360; padding: Theme.s4
        background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }
        onOpened: renameField.forceActiveFocus()
        Column {
            width: parent.width
            spacing: Theme.s3
            Text { text: qsTr("Rename snapshot"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading; color: Theme.textPrimary }
            SearchField {
                id: renameField
                width: parent.width
                placeholder: qsTr("Snapshot name")
                tip: qsTr("The new name for this snapshot.")
                live: false
                onAccepted: t => { if (t.trim() !== "") { backend.renameSnapshot(renameDialog.snapshotId, t.trim()); renameDialog.close() } }
            }
            Row {
                anchors.right: parent.right
                spacing: Theme.s2
                ToolButton { text: qsTr("Cancel"); showLabel: true; onClicked: renameDialog.close() }
                ToolButton {
                    text: qsTr("Rename"); showLabel: true
                    enabled: renameField.text.trim() !== ""
                    onClicked: { backend.renameSnapshot(renameDialog.snapshotId, renameField.text.trim()); renameDialog.close() }
                }
            }
        }
    }
}
