pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui
import OmaRaw.Library

C.Dialog {
    id: root
    objectName: "catalogMaintenanceDialog"
    required property var shell
    property var info: ({})
    property string backupResult: ""
    property bool backupBusy: false
    property bool showReport: false
    readonly property var check: backend.maintenance
    parent: C.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(740, parent ? parent.width - 32 : 740)
    height: Math.min(760, parent ? parent.height - 40 : 760)
    modal: true; title: qsTr("Catalog Maintenance")
    standardButtons: C.Dialog.Close
    palette.window: Theme.panelBg; palette.windowText: Theme.textPrimary
    palette.button: Theme.controlBg; palette.buttonText: Theme.textPrimary; palette.dark: Theme.borderStrong
    Binding { target: root.header; property: "palette"; value: root.palette }
    Binding { target: root.footer; property: "palette"; value: root.palette }
    background: Rectangle { color: Theme.panelBg; border.color: Theme.borderStrong; radius: Theme.rMenu }

    function refresh() {
        const next = backend.catalogMaintenanceInfo()
        if (info.path !== next.path) { backupResult = ""; showReport = false }
        info = next
    }
    function runCheck() { showReport = false; backend.startCatalogCheck() }
    function backUp() {
        if (backupBusy || check.busy) return
        backupBusy = true; backupResult = qsTr("Creating and verifying a backup…")
        backupTimer.start()
    }
    function openFor(action) {
        refresh(); open()
        if (action === "check") runCheck()
        if (action === "backup") backUp()
    }
    onOpened: { refresh(); backend.resources.refreshCache() }
    Timer {
        id: backupTimer; interval: 60
        onTriggered: {
            backend.backupCatalog()
            root.backupResult = backend.statusMessage
            root.backupBusy = false; root.refresh()
        }
    }
    component Note: Text {
        width: parent.width; textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
    }
    component Action: ToolButton { showLabel: true }

    contentItem: C.ScrollView {
        id: scroll
        objectName: "catalogMaintenanceScroll"
        contentWidth: availableWidth; clip: true
        C.ScrollBar.vertical: ScrollBar {}
        C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
        Column {
            width: scroll.availableWidth; spacing: Theme.s3
            Note { text: backend.catalogName; color: Theme.textPrimary; font.bold: true }
            Note { text: root.info.path || ""; wrapMode: Text.WrapAnywhere }
            Note { text: qsTr("%1 photos and versions · %2 catalog data").arg(root.info.count || 0).arg(root.info.size || "") }
            SectionHeader { width: parent.width; title: qsTr("Integrity check") }
            Note { text: qsTr("Check the catalog and Develop databases for errors. You can keep using OmaRAW while the check runs.") }
            Flow {
                width: parent.width; spacing: Theme.s2
                Action { objectName: "runCatalogCheck"; text: root.check.busy ? qsTr("Checking…") : qsTr("Check integrity"); iconName: "check"; enabled: backend.catalogOpen && !root.check.busy && !root.backupBusy; onClicked: root.runCheck() }
                Action { objectName: "copyCatalogReport"; text: qsTr("Copy report"); iconName: "copy"; enabled: root.check.report !== ""; onClicked: root.check.copyReport() }
                Action { objectName: "toggleCatalogReport"; text: root.showReport ? qsTr("Hide report") : qsTr("Show report"); iconName: "file-text"; enabled: root.check.report !== ""; onClicked: root.showReport = !root.showReport }
            }
            Note {
                objectName: "catalogCheckSummary"
                text: root.check.summary || qsTr("No check has been run in this session.")
                color: Theme.textPrimary; font.bold: root.check.summary !== ""
                Accessible.role: Accessible.StaticText
            }
            Note { visible: root.check.report !== ""; text: qsTr("The report includes the check time, database locations and results. Original photo files and the appearance of edits are outside this check.") }
            C.TextArea {
                objectName: "catalogCheckReport"
                visible: root.showReport && root.check.report !== ""
                width: parent.width
                text: root.check.report; textFormat: TextEdit.PlainText
                readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
                background: Rectangle { color: Theme.controlBg; radius: Theme.rControl }
                padding: Theme.s2
            }
            SectionHeader { width: parent.width; title: qsTr("Backups and recovery") }
            Note { text: root.info.lastBackup ? qsTr("Latest backup folder: %1").arg(root.info.lastBackup) : qsTr("No backup bundles found for this catalog.") }
            Note { text: root.info.backupFolder || ""; wrapMode: Text.WrapAnywhere }
            Flow {
                width: parent.width; spacing: Theme.s2
                Action { objectName: "maintenanceBackup"; text: qsTr("Back up now"); iconName: "save"; enabled: backend.catalogOpen && !root.check.busy && !root.backupBusy; onClicked: root.backUp() }
                Action { text: qsTr("Show backups"); iconName: "folder-open"; onClicked: backend.revealBackups() }
                Action { text: qsTr("Restore backup…"); iconName: "history"; enabled: !root.backupBusy; onClicked: { root.close(); root.shell.restoreBackup() } }
            }
            Note { objectName: "catalogBackupResult"; visible: text !== ""; text: root.backupResult; color: Theme.textPrimary }
            Note { text: qsTr("Backups include the catalog and Develop data. Back up original photos and external profiles separately. Restoring creates a separate recovered catalog.") }
            SectionHeader { width: parent.width; title: qsTr("Browsing cache") }
            Note { text: backend.resources.cacheUsage + qsTr(" · Limit: %1 GiB").arg(backend.resources.diskCacheGiB) }
            Note { text: qsTr("Previews are saved on disk and reused between sessions. Older cached previews are removed automatically when the limit is reached.") }
            Action { objectName: "maintenanceCacheSettings"; text: qsTr("Cache and memory settings…"); iconName: "settings"; onClicked: { root.close(); root.shell.performanceSettings() } }
        }
    }
}
