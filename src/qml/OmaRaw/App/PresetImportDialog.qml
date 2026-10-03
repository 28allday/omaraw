import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

ModalPanel {
    id: root
    objectName: "presetImportReportDialog"
    width: Math.min(720, parent ? parent.width - Theme.s4 * 2 : 720)
    height: Math.min(600, parent ? parent.height - Theme.s4 * 2 : 600)
    readonly property var report: backend.presetImportReport
    readonly property bool hasConversions: report.some(row => row.approximate)
    readonly property string reportText: report.map(row => {
        let lines = [row.name || row.file || "", row.status || ""]
        if (row.file) lines.push(row.file)
        if (row.error) lines.push(row.error)
        if (row.summary) lines.push(row.summary)
        if (row.approximate) lines.push(qsTr("%1 settings converted; %2 need attention.").arg((row.converted || []).length).arg((row.warnings || []).length))
        if (row.warnings && row.warnings.length) lines.push(qsTr("Not translated or needs attention:") + "\n" + row.warnings.join("\n"))
        if (row.converted && row.converted.length) lines.push(qsTr("Conversion details:") + "\n" + row.converted.join("\n"))
        return lines.join("\n")
    }).join("\n\n")
    Column {
        anchors.fill: parent
        spacing: Theme.s3
        Item {
            width: parent.width; height: Theme.hControl
            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Preset import report"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading; color: Theme.textPrimary }
            IconButton { anchors.right: parent.right; iconName: "x"; text: qsTr("Close"); onClicked: root.close() }
        }
        Text {
            width: parent.width
            visible: root.hasConversions
            text: qsTr("XMP looks are approximate. OmaRAW uses different processing, and only the converted settings were saved. Check the list below and review the look on a photo.")
            textFormat: Text.PlainText; wrapMode: Text.WordWrap
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl; color: Theme.textSecondary
        }
        C.ScrollView {
            id: scroll
            width: parent.width
            height: parent.height - y - actions.height - Theme.s3
            contentWidth: availableWidth
            clip: true
            C.ScrollBar.vertical: ScrollBar {}
            C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
            C.TextArea {
                id: reportArea
                objectName: "presetImportReportText"
                width: scroll.availableWidth
                readOnly: true; selectByMouse: true
                text: root.reportText
                textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
                color: Theme.textPrimary; selectionColor: Theme.accent; selectedTextColor: Theme.accentText
                background: Rectangle { color: Theme.windowBg; radius: Theme.rControl }
                Accessible.name: qsTr("Preset conversion details")
            }
        }
        Row {
            id: actions
            anchors.right: parent.right; spacing: Theme.s2
            ToolButton { text: qsTr("Copy report"); showLabel: true; tip: qsTr("Copy the complete import report for reference."); onClicked: { reportArea.selectAll(); reportArea.copy(); reportArea.deselect() } }
            ToolButton { text: qsTr("Done"); showLabel: true; checked: true; tip: qsTr("Close this report. Reopen it from File, Presets, Last Import Report."); onClicked: root.close() }
        }
    }
}
