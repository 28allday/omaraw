import QtQuick
import QtQuick.Layouts
import OmaRaw.Ui

Rectangle {
    id: root
    objectName: "statusBar"
    property string message: ""
    property var browseInfo: null
    implicitHeight: Theme.hStatusBar
    color: Theme.panelBg
    Rectangle { width: parent.width; height: Theme.hairline; color: Theme.border }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.s3; anchors.rightMargin: Theme.s3
        spacing: Theme.s3
        Text {
            text: root.browseInfo ? qsTr("%1 photos to review").arg(root.browseInfo.count) : qsTr("%1 photos").arg(Number(backend.totalCount).toLocaleString(Qt.locale(), "f", 0))
            font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
            HoverHandler { id: totalHover }
            Tooltip { visible: totalHover.hovered; text: root.browseInfo ? qsTr("Folder photos") : qsTr("Catalog total"); description: root.browseInfo ? qsTr("Photos available to review in the selected folder.") : qsTr("The number of photos in this catalog, across all folders and collections.") }
        }
        Text {
            text: root.browseInfo ? qsTr("%1 selected for import").arg(root.browseInfo.selected) : qsTr("%1 selected").arg(backend.selectedCount)
            font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel
            color: (root.browseInfo ? root.browseInfo.selected : backend.selectedCount) > 0 ? Theme.accent : Theme.textMuted
            HoverHandler { id: selectedHover }
            Tooltip { visible: selectedHover.hovered; text: qsTr("Selection"); description: root.browseInfo ? qsTr("Ticked photos will be imported when you choose Import.") : qsTr("%1 photos selected for rating, metadata changes and batch actions.").arg(backend.selectedCount) }
        }
        Text {
            textFormat: Text.PlainText
            text: root.browseInfo ? root.browseInfo.name : backend.currentFilename
            Layout.maximumWidth: 140; Layout.minimumWidth: 0
            elide: Text.ElideMiddle
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
            HoverHandler { id: filenameHover }
            Tooltip { visible: filenameHover.hovered && parent.text !== ""; text: qsTr("Current photo"); description: parent.text }
        }
        Text {
            objectName: "statusMessage"
            textFormat: Text.PlainText
            text: root.message
            Layout.fillWidth: true; Layout.minimumWidth: 0
            elide: Text.ElideRight
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            HoverHandler { id: messageHover }
            Tooltip { visible: messageHover.hovered && root.message.length > 0; text: qsTr("Latest activity"); description: root.message }
        }
        Text {
            objectName: "statusSource"
            text: root.browseInfo ? qsTr("Browse · original previews") : backend.sourceTitle + (backend.filterActive ? qsTr(" (filtered)") : "") + qsTr(" · %1 shown").arg(assets.count)
            textFormat: Text.PlainText
            Layout.maximumWidth: 180; Layout.minimumWidth: 0
            elide: Text.ElideRight
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
            HoverHandler { id: sourceHover }
            Tooltip {
                visible: sourceHover.hovered
                text: root.browseInfo ? qsTr("Browse") : backend.sourceTitle
                description: root.browseInfo ? qsTr("Reviewing files on disk. Browsing does not import them.") : backend.filterActive ? qsTr("%1 photos shown after applying the current filters.").arg(assets.count)
                                                  : qsTr("%1 photos shown in this source.").arg(assets.count)
            }
        }
        Text {
            text: backend.colourPipeline.error ? qsTr("OCIO · check config") : "OCIO · " + backend.colourPipeline.view
            Layout.maximumWidth: 180; Layout.minimumWidth: 0
            elide: Text.ElideRight
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
            color: backend.colourPipeline.error ? Theme.warning : Theme.textSecondary
            HoverHandler { id: colourHover }
            Tooltip {
                visible: colourHover.hovered
                text: qsTr("Viewing colour")
                description: backend.colourPipeline.error || qsTr("%1, %2, %3 EV preview. Change the viewing transform under View > Colour Management; it does not change exports.").arg(backend.colourPipeline.configName).arg(backend.colourPipeline.view).arg(backend.colourPipeline.exposure)
            }
        }
        Text {
            visible: backend.importing || engine.offlineBusy
            text: engine.offlineBusy ? qsTr("Offline copies") : qsTr("Importing")
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
            HoverHandler { id: jobHover }
            Tooltip { visible: jobHover.hovered; text: qsTr("Background job"); description: engine.offlineBusy ? engine.offlineStatus : backend.importStatus }
        }
        Rectangle {
            visible: backend.importing || engine.offlineBusy
            Layout.preferredWidth: 80; Layout.preferredHeight: 4
            radius: 2; color: Theme.controlBg
            HoverHandler { id: progressHover }
            Tooltip { visible: progressHover.hovered; text: qsTr("Progress"); description: engine.offlineBusy ? engine.offlineStatus : backend.importStatus }
            Rectangle {
                width: parent.width * (engine.offlineBusy ? engine.offlineProgress : backend.importProgress)
                height: parent.height; radius: 2; color: Theme.accent
            }
        }
        IconButton {
            visible: engine.offlineBusy
            Layout.preferredWidth: Theme.hStatusBar - 4; Layout.preferredHeight: Theme.hStatusBar - 4
            iconName: "x"; text: qsTr("Cancel offline copy job")
            tip: qsTr("Stop the copy operation. Completed copies remain available.")
            onClicked: engine.cancelOfflineCopies()
        }
        // Said only when processing is not ready: the engine behind it is
        // not something to read while working (Help ▸ About credits it).
        Text {
            visible: !engine.ready
            text: engine.available ? qsTr("starting") : qsTr("previews only")
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
            color: engine.ready ? Theme.success : Theme.warning
            HoverHandler { id: engineHover }
            Tooltip { visible: engineHover.hovered; text: qsTr("Photo processing"); description: engine.status }
        }
        Text {
            text: qsTr("v%1").arg(backend.version)
            font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            HoverHandler { id: versionHover }
            Tooltip { visible: versionHover.hovered; text: qsTr("OmaRAW %1").arg(backend.version); description: qsTr("The app version. Help > About OmaRAW also lists credits and the licence.") }
        }
    }
}
