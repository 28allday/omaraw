pragma ComponentBehavior: Bound
import QtCore
import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui
import OmaRaw.Library

C.Dialog {
    id: root
    objectName: "preferencesDialog"
    required property var shell
    property int page: 0
    parent: C.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(620, parent ? parent.width - 32 : 620)
    height: Math.min(720, parent ? parent.height - 40 : 720)
    modal: true; title: qsTr("Preferences")
    standardButtons: C.Dialog.Close
    palette.window: Theme.panelBg; palette.windowText: Theme.textPrimary
    palette.button: Theme.controlBg; palette.buttonText: Theme.textPrimary; palette.dark: Theme.borderStrong
    // Keep overlay chrome on the dialog palette when Colour Critical changes.
    Binding { target: root.header; property: "palette"; value: root.palette }
    Binding { target: root.footer; property: "palette"; value: root.palette }
    background: Rectangle { color: Theme.panelBg; border.color: Theme.borderStrong; radius: Theme.rMenu }
    onPageChanged: if (scroll.contentItem) scroll.contentItem.contentY = 0
    onOpened: backend.resources.refreshCache()
    Settings { id: developPrefs; category: "develop"; property bool autoFirstEdit: true }

    component Note: Text {
        width: parent.width
        textFormat: Text.PlainText; wrapMode: Text.WordWrap
        color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
    }
    component Setting: Column {
        id: setting
        property string controlName: ""
        property string label: ""
        property string description: ""
        property alias checked: toggle.checked
        signal clicked()
        width: parent.width; spacing: Theme.s1
        Toggle {
            id: toggle
            objectName: setting.controlName
            width: parent.width; label: setting.label; tip: setting.description
            onClicked: setting.clicked()
        }
        Note { visible: text !== ""; text: setting.description }
    }
    component Heading: SectionHeader { width: parent.width }
    component Action: ToolButton {
        // Short, bounded labels remain readable at the minimum window width.
        showLabel: true
    }

    contentItem: Item {
        Row {
            id: tabs
            width: parent.width
            Repeater {
                model: [qsTr("Appearance"), qsTr("Library"), qsTr("Processing"), qsTr("Performance")]
                TabButton {
                    required property int index
                    required property string modelData
                    objectName: "preferencesTab_" + index
                    width: tabs.width / 4; text: modelData; uppercase: false
                    checked: root.page === index
                    onClicked: root.page = index
                }
            }
        }
        C.ScrollView {
            id: scroll
            objectName: "preferencesScroll"
            anchors.top: tabs.bottom; anchors.topMargin: Theme.s3
            anchors.bottom: saved.top; anchors.bottomMargin: Theme.s3
            anchors.left: parent.left; anchors.right: parent.right
            contentWidth: availableWidth; clip: true
            C.ScrollBar.vertical: ScrollBar {}
            C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
            Column {
                width: scroll.availableWidth
                Column {
                    objectName: "preferencesAppearance"
                    visible: root.page === 0; width: parent.width; spacing: Theme.s3
                    Heading { title: qsTr("Interface") }
                    Setting { controlName: "colourCriticalPreference"; label: qsTr("Colour Critical"); description: qsTr("Use a neutral grey interface and a fixed dark grey surround for judging colour."); checked: backend.colourCritical; onClicked: backend.colourCritical = !backend.colourCritical }
                    Setting { controlName: "highContrastPreference"; label: qsTr("High contrast"); description: qsTr("Make interface text and borders easier to distinguish."); checked: backend.highContrast; onClicked: backend.highContrast = !backend.highContrast }
                    Setting { controlName: "reducedMotionPreference"; label: qsTr("Reduced motion"); description: qsTr("Open menus and panels without animation."); checked: backend.reducedMotion; onClicked: backend.reducedMotion = !backend.reducedMotion }
                    Heading { title: qsTr("Photo viewer") }
                    ComboField {
                        objectName: "viewerBackgroundPreference"
                        width: parent.width; caption: qsTr("Background")
                        enabled: !backend.colourCritical
                        tip: qsTr("Choose the shade behind the photograph.")
                        readonly property var choices: ["dark", "black", "grey", "light"]
                        model: [qsTr("Dark"), qsTr("Black"), qsTr("Mid grey"), qsTr("Light")]
                        currentIndex: choices.indexOf(root.shell.viewerBackground)
                        onActivated: i => root.shell.viewerBackground = choices[i]
                    }
                    Note {
                        objectName: "colourCriticalSurroundNote"
                        visible: backend.colourCritical
                        text: qsTr("Colour Critical fixes the surround to dark grey. Turning it off restores your saved background.")
                    }
                    Action { text: qsTr("Colour management…"); tip: qsTr("Choose display colour settings and monitor calibration."); iconName: "palette"; onClicked: { root.close(); root.shell.displayColourSettings() } }
                    Heading { title: qsTr("Workspace") }
                    Action { text: qsTr("Keyboard shortcuts…"); iconName: "keyboard"; onClicked: { root.close(); root.shell.editShortcuts() } }
                    Action { text: qsTr("Reset panel layout"); tip: qsTr("Restore default panel sizes and show the source sidebar, inspector and filmstrip."); iconName: "layout-dashboard"; onClicked: root.shell.resetLayout() }
                }
                Column {
                    objectName: "preferencesLibrary"
                    visible: root.page === 1; width: parent.width; spacing: Theme.s3
                    Heading { title: qsTr("Startup and backups") }
                    Setting { controlName: "askCatalogPreference"; label: qsTr("Choose a library at startup"); description: qsTr("When off, reopen the last library. Ask if it is missing or already open in another window."); checked: backend.askCatalogAtStartup; onClicked: backend.askCatalogAtStartup = !backend.askCatalogAtStartup }
                    Setting { controlName: "autoBackupPreference"; label: qsTr("Back up the library daily"); description: qsTr("Back up the catalog and Develop data on opening. Keep the newest seven daily backups. Original photos and external profiles need separate backups."); checked: backend.autoBackup; onClicked: backend.autoBackup = !backend.autoBackup }
                    Action { text: qsTr("Show backups"); iconName: "folder-open"; onClicked: backend.revealBackups() }
                    Heading { title: qsTr("Organising photos") }
                    Setting { controlName: "autoAdvancePreference"; label: qsTr("Advance after rating or flagging"); description: qsTr("Move to the next photo after rating, flagging or colour-labelling a single photo."); checked: backend.autoAdvance; onClicked: backend.autoAdvance = !backend.autoAdvance }
                    Action { text: qsTr("Colour label names…"); tip: qsTr("Give each colour label a name that describes how you use it."); iconName: "tag"; onClicked: { root.close(); root.shell.editLabelNames() } }
                    Heading { title: qsTr("Metadata") }
                    Setting { controlName: "writeSidecarsPreference"; label: qsTr("Write XMP sidecars automatically"); description: qsTr("Save ratings, keywords, text and edits in XMP files beside the originals, including an approximate XMP edit."); checked: backend.writeSidecars; onClicked: backend.writeSidecars = !backend.writeSidecars }
                    Setting { controlName: "embedXmpPreference"; label: qsTr("Write metadata inside DNG, JPEG and TIFF"); description: qsTr("For these formats, store metadata and edits inside the original files instead of separate XMP sidecars."); checked: backend.embedXmp; onClicked: backend.embedXmp = !backend.embedXmp }
                }
                Column {
                    objectName: "preferencesProcessing"
                    visible: root.page === 2; width: parent.width; spacing: Theme.s3
                    Heading { title: qsTr("New RAW photos") }
                    Setting { controlName: "autoFirstEditPreference"; label: qsTr("Automatic first edit"); description: qsTr("Start newly imported RAWs with capture sharpening, colour noise reduction and a recognised lens profile. Keep brightness and colour as shot. Existing photos are unchanged."); checked: developPrefs.autoFirstEdit; onClicked: developPrefs.autoFirstEdit = !developPrefs.autoFirstEdit }
                    Heading { title: qsTr("Imported presets") }
                    Action { text: qsTr("Camera profiles folder…"); iconName: "folder-open"; onClicked: { root.close(); root.shell.chooseCameraProfilesFolder() } }
                    Note { text: qsTr("Choose the DCP profiles supplied with imported XMP presets.") }
                    Heading { title: qsTr("Speed and quality") }
                    Setting { controlName: "gpuComputePreference"; label: qsTr("Use the graphics card when faster"); description: qsTr("Use supported graphics processing after quality and speed checks pass. CPU processing remains available."); checked: backend.colourPipeline.gpuEnabled; onClicked: backend.colourPipeline.gpuEnabled = !backend.colourPipeline.gpuEnabled }
                    Setting { controlName: "reducedPreviewsPreference"; label: qsTr("Faster fitted previews"); description: qsTr("Use faster previews when zoomed out or dragging a control. Full zoomed detail returns when you release. Exports keep full quality."); checked: engine.reducedPreviews; onClicked: engine.reducedPreviews = !engine.reducedPreviews }
                    Setting { controlName: "exportFullResolutionPreference"; label: qsTr("Process exports at full resolution"); description: qsTr("Apply every edit before resizing smaller exports. This is slower and can change fine detail. Full-size exports are unchanged."); checked: engine.exportFullResolution; onClicked: engine.exportFullResolution = !engine.exportFullResolution }
                }
                Column {
                    objectName: "preferencesPerformance"
                    visible: root.page === 3; width: parent.width; spacing: Theme.s3
                    Heading { title: qsTr("System memory") }
                    Note { text: qsTr("%1 GiB of system memory detected.").arg((backend.resources.systemMemoryMiB / 1024).toFixed(1)) }
                    Setting {
                        controlName: "automaticMemoryPreference"
                        label: qsTr("Automatic memory allocation")
                        description: qsTr("Choose a memory budget for this computer, leaving room for other applications.")
                        checked: backend.resources.memoryMiB === 0
                        onClicked: backend.resources.memoryMiB = backend.resources.memoryMiB === 0 ? backend.resources.effectiveMemoryMiB : 0
                    }
                    SliderField {
                        objectName: "memoryBudgetPreference"
                        width: parent.width; stacked: true; label: qsTr("Memory budget")
                        from: 1; to: backend.resources.maximumMemoryMiB / 1024
                        stepSize: 0.25; decimals: 2; adaptiveDecimals: true; suffix: qsTr("GiB")
                        value: backend.resources.effectiveMemoryMiB / 1024
                        enabled: backend.resources.memoryMiB !== 0
                        onEditingFinished: v => backend.resources.memoryMiB = Math.round(v * 1024)
                    }
                    Note { text: qsTr("Shared by image processing, decoded photos and reusable previews. More memory can speed up photo switching and editing. Large photos, exports and AI can temporarily use additional memory. Changes apply after the current processing step.") }
                    Heading { title: qsTr("Disk cache") }
                    SliderField {
                        objectName: "diskCachePreference"
                        width: parent.width; stacked: true; label: qsTr("Maximum cache size")
                        from: 1; to: 1024; stepSize: 1; decimals: 0; suffix: qsTr("GiB")
                        value: backend.resources.diskCacheGiB
                        onEditingFinished: v => backend.resources.diskCacheGiB = Math.round(v)
                    }
                    Note { text: qsTr("Store browsing previews and temporary processing data on disk. Large image buffers stay in RAM while there is room, and gain disk backing when needed during editing and export. Older previews are removed automatically; space is used only as needed.") }
                    Note { objectName: "previewCacheUsage"; text: backend.resources.cacheUsage }
                    Note { text: backend.resources.cachePath; wrapMode: Text.WrapAnywhere }
                    Flow {
                        width: parent.width; spacing: Theme.s2
                        Action { text: qsTr("Open cache folder"); iconName: "folder-open"; onClicked: backend.resources.revealCache() }
                        Action { objectName: "clearPreviewCache"; text: qsTr("Clear preview cache"); iconName: "trash-2"; enabled: !backend.resources.cacheBusy; onClicked: backend.resources.clearCache() }
                    }
                    Note { text: backend.resources.cacheStatus; visible: text !== "" }
                    Note { text: qsTr("Clearing the cache keeps your photos, edits, edited thumbnails, Smart Previews and offline originals. Browsing previews rebuild when needed.") }
                }
            }
        }
        Note {
            id: saved
            anchors.bottom: parent.bottom
            height: Math.max(implicitHeight, Theme.hControl); verticalAlignment: Text.AlignVCenter
            width: parent.width - help.width - Theme.s2
            text: qsTr("Changes are saved automatically.")
        }
        ToolButton {
            id: help
            anchors.right: parent.right; anchors.verticalCenter: saved.verticalCenter
            text: qsTr("Help"); iconName: "circle-help"; showLabel: true
            onClicked: { root.close(); root.shell.help("preferences") }
        }
    }
}
