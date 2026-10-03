pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

// The persistent output queue along the bottom: one row per file with its
// state, a progress bar while a batch runs, cancel/clear.
Rectangle {
    id: root
    color: Theme.panelBg
    Rectangle { width: parent.width; height: Theme.hairline; color: Theme.border }
    Item {
        id: head
        width: parent.width; height: Theme.hDockHeader
        Row {
            anchors.left: parent.left; anchors.leftMargin: Theme.s3
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.s3
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("QUEUE")
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; font.weight: Theme.wHeading; font.letterSpacing: 0.6
                color: Theme.textSecondary
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: engine.exportQueue.length === 0 ? qsTr("empty")
                    : qsTr("%1 done · %2 failed · %3 of %4").arg(engine.exportDone).arg(engine.exportFailed).arg(engine.exportDone + engine.exportFailed).arg(engine.exportQueue.length)
                font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: engine.exporting
                width: 160; height: 4; radius: 2; color: Theme.controlBg
                Rectangle {
                    width: engine.exportQueue.length ? parent.width * (engine.exportDone + engine.exportFailed) / engine.exportQueue.length : 0
                    height: parent.height; radius: 2; color: Theme.accent
                }
            }
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: Theme.s2
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.s1
            ToolButton {
                objectName: "exportPauseResume"
                text: engine.exportPaused ? qsTr("Resume") : qsTr("Pause")
                showLabel: true
                tip: engine.exportPaused ? qsTr("Carries on with the queued exports.") : qsTr("Finishes the file being written, then waits.")
                enabled: engine.ready && (engine.exporting || engine.exportQueue.some(r => r.status === "queued"))
                onClicked: engine.exportPaused ? engine.resumeExport() : engine.pauseExport()
            }
            ToolButton { iconName: "rotate-ccw"; text: qsTr("Retry failed"); showLabel: true; tip: qsTr("Queues every failed or cancelled export again."); enabled: engine.exportFailed > 0 || engine.exportQueue.some(r => r.status === "cancelled"); onClicked: engine.retryFailed() }
            ToolButton { iconName: "x"; text: qsTr("Cancel remaining"); showLabel: true; tip: qsTr("Stops after the file being written; the rest are marked cancelled."); enabled: engine.exporting || engine.exportQueue.some(r => r.status === "queued"); onClicked: engine.cancelExport() }
            ToolButton { iconName: "trash-2"; text: qsTr("Clear"); showLabel: true; tip: qsTr("Empties the list; the files already written stay."); enabled: !engine.exporting && (engine.exportQueue.length > 0 || engine.exportQueueError !== ""); onClicked: engine.clearExportQueue() }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.hairline; color: Theme.border }
    }
    Text {
        id: queueMessage
        anchors.top: head.bottom; anchors.left: parent.left; anchors.right: parent.right
        anchors.margins: Theme.s2
        height: visible ? implicitHeight : 0
        visible: text !== ""
        text: engine.exportQueueError || (engine.exportPaused ? (engine.exporting ? qsTr("Pausing after the current photo…") : qsTr("Queue paused. Review the jobs, then Resume to continue.")) : "")
        wrapMode: Text.WordWrap
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.warning
    }
    ListView {
        anchors.top: queueMessage.bottom; anchors.bottom: parent.bottom
        anchors.left: parent.left; anchors.right: parent.right
        clip: true
        model: engine.exportQueue
        C.ScrollBar.vertical: ScrollBar {}
        delegate: Item {
            id: row
            required property var modelData
            required property int index
            width: ListView.view.width; height: Theme.hRow
            HoverHandler { id: rowHover }
            // Row actions on hover: reveal a finished file, retry a failed one, move a queued one, copy or drop.
            Row {
                anchors.right: parent.right; anchors.rightMargin: Theme.s2
                anchors.verticalCenter: parent.verticalCenter
                spacing: 0
                visible: rowHover.hovered
                IconButton { visible: row.modelData.status === "done"; iconName: "folder-open"; text: qsTr("Reveal in file manager"); tip: qsTr("Opens the folder with this file selected."); onClicked: backend.revealPath(row.modelData.out) }
                IconButton { visible: row.modelData.status === "queued"; iconName: "chevron-up"; text: qsTr("Earlier"); tip: qsTr("Moves it up the queue so it is written sooner."); onClicked: engine.moveQueueRow(row.index, -1) }
                IconButton { visible: row.modelData.status === "queued"; iconName: "chevron-down"; text: qsTr("Later"); tip: qsTr("Moves it down the queue."); onClicked: engine.moveQueueRow(row.index, 1) }
                IconButton { visible: row.modelData.status !== "rendering"; iconName: "copy"; text: qsTr("Export again"); tip: qsTr("Queues the same photo with the same settings once more."); onClicked: engine.duplicateQueueRow(row.index) }
                IconButton { visible: row.modelData.status !== "rendering"; iconName: "x"; text: qsTr("Remove from the queue"); tip: qsTr("Drops this row; a file already written stays."); onClicked: engine.removeQueueRow(row.index) }
            }
            readonly property color tone: modelData.status === "done" ? (modelData.error ? Theme.warning : Theme.success) : modelData.status === "failed" ? Theme.danger
                                        : modelData.status === "rendering" ? Theme.accent : Theme.textMuted
            Rectangle { x: Theme.s3; width: 6; height: 6; radius: 3; anchors.verticalCenter: parent.verticalCenter; color: row.tone }
            Text {
                x: Theme.s3 + 14; width: 260
                anchors.verticalCenter: parent.verticalCenter
                text: row.modelData.name; elide: Text.ElideMiddle
                textFormat: Text.PlainText   // names and filenames are not markup
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textPrimary
            }
            Text {
                x: Theme.s3 + 14 + 268; width: 80
                anchors.verticalCenter: parent.verticalCenter
                text: row.modelData.status === "queued" && engine.exportPaused ? qsTr("paused")
                    : row.modelData.status === "done" && row.modelData.error ? qsTr("warning") : row.modelData.status
                font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel; color: row.tone
            }
            Text {
                x: Theme.s3 + 14 + 356; width: parent.width - x - Theme.s3 - (rowHover.hovered ? 5 * Theme.szIconHit : 0)
                anchors.verticalCenter: parent.verticalCenter
                text: row.modelData.error || row.modelData.out
                elide: Text.ElideMiddle
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
        }
    }
}
