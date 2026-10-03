pragma ComponentBehavior: Bound
// The catalog OmaRAW should work in, asked before the main window exists.
// It is shown when the user has turned on the startup question, and whenever
// the remembered catalog cannot be opened. Quitting is always one click away.
import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

C.ApplicationWindow {
    id: root
    objectName: "catalogChooserWindow"
    visible: true
    title: qsTr("OmaRAW — Choose a Catalog")
    width: 560
    height: Math.min(620, contents.implicitHeight + Theme.s5 * 2)
    minimumWidth: 420
    minimumHeight: 240
    color: Theme.windowBg
    palette: Theme.colourCritical ? criticalPalette : defaultPalette
    Palette { id: defaultPalette }
    Palette {
        id: criticalPalette
        window: Theme.windowBg; windowText: Theme.textPrimary
        base: Theme.inputBg; alternateBase: Theme.panelRaised; text: Theme.textPrimary
        button: Theme.controlBg; buttonText: Theme.textPrimary; brightText: Theme.textPrimary
        highlight: Theme.accent; highlightedText: Theme.accentText; accent: Theme.accent
        light: Theme.borderStrong; midlight: Theme.borderStrong; mid: Theme.border; dark: Theme.border
        shadow: Theme.windowBg
        toolTipBase: Theme.panelRaised; toolTipText: Theme.textPrimary
        link: Theme.accent; linkVisited: Theme.textSecondary; placeholderText: Theme.textMuted
        disabled {
            text: Theme.textMuted; windowText: Theme.textMuted; buttonText: Theme.textMuted
            highlight: Theme.controlBg; highlightedText: Theme.textMuted; placeholderText: Theme.textMuted
        }
    }
    Binding { target: Theme; property: "colourCritical"; value: backend.colourCritical }
    Binding { target: Theme; property: "highContrast"; value: backend.highContrast }
    Binding { target: Theme; property: "reducedMotion"; value: backend.reducedMotion }

    readonly property var recents: backend.recentCatalogs()

    Column {
        id: contents
        anchors.fill: parent
        anchors.margins: Theme.s5
        spacing: Theme.s3

        Text {
            width: parent.width
            text: chooser.message !== "" ? qsTr("OmaRAW could not open that catalog") : qsTr("Choose a catalog")
            color: Theme.textPrimary
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsTitle; font.bold: true
        }
        Text {
            width: parent.width
            visible: chooser.message !== ""
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            text: chooser.failedPath !== "" ? chooser.message + "\n\n" + chooser.failedPath : chooser.message
            color: Theme.textMuted
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase
        }
        Text {
            width: parent.width
            visible: chooser.message === ""
            wrapMode: Text.Wrap
            text: qsTr("A catalog holds your photos' edits, keywords and albums — not the photo files themselves.")
            color: Theme.textMuted
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase
        }

        Text {
            width: parent.width
            visible: recentList.count > 0
            text: qsTr("Recent catalogs")
            color: Theme.textMuted
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        }
        Rectangle {
            width: parent.width
            visible: recentList.count > 0
            height: Math.min(240, recentList.contentHeight + 2)
            color: Theme.panelBg
            border.color: Theme.borderStrong
            radius: Theme.rMenu
            clip: true
            ListView {
                id: recentList
                objectName: "recentCatalogList"
                anchors.fill: parent
                anchors.margins: 1
                model: root.recents
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    width: recentList.width
                    height: 46
                    color: hover.hovered ? Theme.hoverBg : "transparent"
                    HoverHandler { id: hover }
                    TapHandler { onTapped: chooser.choose(row.modelData.path) }
                    Tooltip {
                        text: row.modelData.name
                        description: qsTr("Open this catalog: %1").arg(row.modelData.path)
                        visible: hover.hovered
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.leftMargin: Theme.s3; anchors.rightMargin: Theme.s3
                        spacing: 1
                        Text {
                            width: parent.width; elide: Text.ElideMiddle; textFormat: Text.PlainText
                            text: row.modelData.current
                                  ? qsTr("%1 (open in another window)").arg(row.modelData.name)
                                  : row.modelData.name
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase
                        }
                        Text {
                            width: parent.width; elide: Text.ElideMiddle; textFormat: Text.PlainText
                            text: row.modelData.folder
                            color: Theme.textMuted
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
                        }
                    }
                }
                C.ScrollBar.vertical: ScrollBar {}
            }
        }

        Row {
            spacing: Theme.s2
            ToolButton {
                objectName: "chooserNewCatalog"
                text: qsTr("New Catalog…"); tip: qsTr("Create a catalog with its own edits, keywords and albums."); iconName: "plus"; showLabel: true
                onClicked: newCatalogDialog.open()
            }
            ToolButton {
                objectName: "chooserOpenCatalog"
                text: qsTr("Open Catalog…"); tip: qsTr("Choose an existing catalog database to open."); iconName: "folder-open"; showLabel: true
                onClicked: openDialog.open()
            }
            ToolButton {
                objectName: "chooserQuit"
                text: qsTr("Quit"); tip: qsTr("Leave OmaRAW without opening a catalog."); iconName: "x"; showLabel: true
                onClicked: chooser.quit()
            }
        }
    }

    FilePicker {
        id: openDialog
        objectName: "openDialog"
        title: qsTr("Open a catalog")
        nameFilters: [qsTr("OmaRAW catalogs (*.db)")]
        onAccepted: chooser.choose(selectedFile.toString())
    }

    NewCatalogDialog {
        id: newCatalogDialog
        onCreated: path => chooser.choose(path)
    }
}
