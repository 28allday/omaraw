import QtCore
import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui
import OmaRaw.App
import OmaRaw.Library
import OmaRaw.Develop
import OmaRaw.Capture
import OmaRaw.Output

// The application shell: frameless window, title bar with menus and the
// workspace bar, one workspace at a time, status bar. Layout persists.
C.ApplicationWindow {
    id: win
    visible: true
    width: 1600; height: 960
    minimumWidth: 1280; minimumHeight: 720
    flags: Qt.Window | Qt.FramelessWindowHint
    title: (backend.currentFilename ? backend.currentFilename + " — " : "") + Theme.appName
    color: Theme.windowBg
    // An unresolved palette keeps the normal Qt style; critical mode also
    // makes standard dialog controls use the neutral interface colours.
    palette: (Theme.light || Theme.colourCritical) ? criticalPalette : defaultPalette
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

    // ── shell state (persisted) ─────────────────────────────────────────
    property string workspace: "Library"
    property string browserMode: "grid"
    // Retired views in saved preferences must reopen a usable photo browser.
    onBrowserModeChanged: if (!["grid", "list", "loupe", "compare", "survey"].includes(browserMode)) browserMode = "grid"
    property int cardSize: 240
    property bool sourceDockVisible: true
    property bool inspectorVisible: true
    property bool filmstripVisible: true
    property int sourceDockWidth: Theme.wSourceDock
    property int inspectorWidth: Theme.wInspector
    property int filmstripHeight: Theme.hFilmstrip
    // Develop viewer: the canvas behind the picture (dark | black | grey | light)
    // and lights out, which leaves the picture alone on black until Escape.
    property string viewerBackground: "dark"
    property bool lightsOut: false
    onWorkspaceChanged: if (workspace !== "Develop") lightsOut = false
    Settings {
        category: "shell"
        property alias viewerBackground: win.viewerBackground
        property alias windowWidth: win.width
        property alias windowHeight: win.height
        property alias workspace: win.workspace
        property alias browserMode: win.browserMode
        property alias cardSize: win.cardSize
        property alias sourceDockVisible: win.sourceDockVisible
        property alias inspectorVisible: win.inspectorVisible
        property alias filmstripVisible: win.filmstripVisible
        property alias sourceDockWidth: win.sourceDockWidth
        property alias inspectorWidth: win.inspectorWidth
        property alias filmstripHeight: win.filmstripHeight
    }
    // Accessibility modes live in the settings; the theme follows them.
    Binding { target: Theme; property: "reducedMotion"; value: backend.reducedMotion }
    Binding { target: Theme; property: "highContrast"; value: backend.highContrast }
    Binding { target: Theme; property: "colourCritical"; value: backend.colourCritical }
    Binding { target: Theme; property: "light"; value: backend.lightInterface && !backend.colourCritical }
    function resetLayout() {
        sourceDockVisible = true; inspectorVisible = true; filmstripVisible = true
        sourceDockWidth = Theme.wSourceDock; inspectorWidth = Theme.wInspector
        filmstripHeight = Theme.hFilmstrip; cardSize = 240
    }
    function about() { aboutDialog.open() }
    // The help, at a section: a panel's page from its ? button, or the
    // current workspace's page from F1 and the Help menu.
    function help(section) {
        const byWorkspace = { Library: "library", Develop: "develop", Capture: "capture", Output: "output" }
        helpDialog.openSection(section || byWorkspace[win.workspace] || "start")
    }
    function autoStackPhotos() { autoStackDialog.open() }
    function openDevelopTool(tool) { workspace = "Develop"; developWs.openTool(tool) }
    function newAlbum() { albumDialog.parentId = 0; albumDialog.open() }
    function newAlbumInside(parentId) { albumDialog.parentId = parentId; albumDialog.open() }
    function editLabelNames() { labelNamesDialog.open() }
    function renameVariant() { if (win.currentIsVariant) variantDialog.open() }
    function renameFile() { if (backend.currentId > 0) renameFileDialog.open() }
    function moveSelection() { if (backend.selectedCount > 0) moveDialog.open() }
    function trashSelection() { if (backend.selectedCount > 0) trashDialog.open() }
    function restoreBackup() { backupManifestDialog.open() }
    function openCatalogFile() { catalogFileDialog.open() }
    function newCatalog() { newCatalogDialog.open() }
    // Reopens this window on another catalog, asking first if work is running.
    function switchCatalog(path) {
        if (backend.importing || engine.exporting || engine.offlineBusy) {
            switchBusyDialog.pending = path
            switchBusyDialog.open()
            return
        }
        backend.switchCatalog(path)
    }
    function displayColourSettings() { displayColourDialog.open() }
    function preferences() { preferencesDialog.open() }
    function performanceSettings() { preferencesDialog.page = 3; preferencesDialog.open() }
    function showCatalogMaintenance(action) { catalogMaintenanceDialog.openFor(action || "") }
    PreferencesDialog { id: preferencesDialog; shell: win }
    CatalogMaintenanceDialog { id: catalogMaintenanceDialog; shell: win }
    AboutDialog { id: aboutDialog }
    FilePicker {
        id: ocioConfigDialog
        objectName: "ocioConfigDialog"
        title: qsTr("Choose an OpenColorIO configuration")
        nameFilters: [qsTr("OCIO configurations (*.ocio *.ocioz)")]
        onAccepted: displayColourDialog.stage(selectedFile.toString(), "", "")
    }
    FilePicker {
        id: monitorProfileDialog
        objectName: "monitorProfileDialog"
        title: qsTr("Choose the profile for this monitor")
        nameFilters: [qsTr("RGB ICC profiles (*.icc *.icm *.ICC *.ICM)")]
        onAccepted: backend.displayColour.profile = selectedFile.toString()
    }
    C.Dialog {
        id: displayColourDialog
        objectName: "displayColourDialog"
        parent: C.Overlay.overlay
        anchors.centerIn: parent
        width: 560; modal: true
        title: qsTr("Colour management")
        property var configInfo: ({})
        property string inspectError: ""
        function stage(path, display, view) {
            ocioPath.text = path
            configInfo = backend.colourPipeline.inspect(path, display)
            inspectError = configInfo.error || ""
            ocioDisplay.currentIndex = Math.max(0, (configInfo.displays || []).indexOf(display || configInfo.display))
            ocioView.currentIndex = Math.max(0, (configInfo.views || []).indexOf(view || configInfo.view))
        }
        onOpened: {
            stage(backend.colourPipeline.configPath, backend.colourPipeline.display, backend.colourPipeline.view)
            ocioExposure.text = String(backend.colourPipeline.exposure)
        }
        standardButtons: C.Dialog.Close
        palette.window: Theme.panelBg
        palette.windowText: Theme.textPrimary
        palette.button: Theme.controlBg
        palette.buttonText: Theme.textPrimary
        palette.dark: Theme.borderStrong
        background: Rectangle { color: Theme.panelBg; border.color: Theme.borderStrong; radius: 4 }
        contentItem: C.ScrollView {
          id: colourScroll
          implicitHeight: Math.min(colourColumn.implicitHeight, win.height - 200)
          contentWidth: availableWidth
          Column {
            id: colourColumn
            width: colourScroll.availableWidth
            spacing: Theme.s3
            Text { width: parent.width; text: backend.colourPipeline.configName + " · OpenColorIO " + backend.colourPipeline.version; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading }
            Text { width: parent.width; wrapMode: Text.WordWrap; text: qsTr("32-bit float development with linear RGB working spaces. The viewing settings below affect photo previews; exports use their selected output colour space."); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase }
            C.TextField {
                id: ocioPath; objectName: "ocioConfigPath"
                width: parent.width; height: Theme.hControl
                color: Theme.textPrimary; placeholderTextColor: Theme.textMuted
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
                background: Rectangle { color: Theme.inputBg; radius: Theme.rControl; border.color: ocioPath.activeFocus ? Theme.accent : Theme.border }
                placeholderText: qsTr("Oma Colour 1 (bundled), or a config path / ocio:// URI")
                hoverEnabled: true
                Tooltip { visible: ocioPath.hovered; text: qsTr("Colour configuration"); description: qsTr("Use the bundled colour setup or enter a configuration path. Choose a display and view, then press Apply view.") }
                onEditingFinished: displayColourDialog.stage(text, "", "")
            }
            Row {
                spacing: Theme.s3
                ToolButton { text: qsTr("Choose config…"); tip: qsTr("Browse for a colour configuration file to use for photo previews."); onClicked: ocioConfigDialog.open() }
                ToolButton { text: qsTr("Use suite default"); tip: qsTr("Select the bundled colour setup and reset preview exposure to zero. Press Apply view to use it."); onClicked: { displayColourDialog.stage("", "", ""); ocioExposure.text = "0" } }
            }
            ComboField {
                id: ocioDisplay; width: parent.width; caption: qsTr("Display")
                tip: qsTr("Choose the display definition supplied by this colour configuration.")
                model: displayColourDialog.configInfo.displays || []
                onActivated: displayColourDialog.stage(ocioPath.text, currentText, "")
            }
            ComboField {
                id: ocioView; objectName: "ocioView"
                width: parent.width; caption: qsTr("View")
                tip: qsTr("Choose the viewing transform for the photograph. Standard preserves its developed appearance.")
                model: displayColourDialog.configInfo.views || []
            }
            Row {
                spacing: Theme.s3
                Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Preview exposure (EV)"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase }
                C.TextField {
                    id: ocioExposure; width: 85; height: Theme.hControl
                    color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
                    background: Rectangle { color: Theme.inputBg; radius: Theme.rControl; border.color: ocioExposure.activeFocus ? Theme.accent : Theme.border }
                    validator: DoubleValidator { bottom: -20; top: 20; locale: "C" }
                    hoverEnabled: true
                    Tooltip { visible: ocioExposure.hovered; text: qsTr("Preview exposure"); description: qsTr("Brighten or darken the displayed preview in stops. This viewing adjustment leaves the edit and exports unchanged.") }
                }
                ToolButton {
                    objectName: "ocioApply"
                    text: qsTr("Apply view")
                    tip: qsTr("Apply the selected configuration, display, view and preview exposure.")
                    enabled: !displayColourDialog.inspectError && ocioView.currentIndex >= 0 && ocioExposure.acceptableInput
                    onClicked: backend.colourPipeline.configure(ocioPath.text, ocioDisplay.currentText, ocioView.currentText, Number(ocioExposure.text))
                }
            }
            Text { width: parent.width; wrapMode: Text.WordWrap; visible: text !== ""; text: displayColourDialog.inspectError || backend.colourPipeline.error; color: Theme.warning; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase }
            Text { width: parent.width; wrapMode: Text.WordWrap; text: qsTr("Standard preserves the developed photograph. An external film or ACES view may add a second tone mapping operation to an existing edit. This window presents SDR sRGB."); color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel }
            Rectangle { width: parent.width; height: 1; color: Theme.border }
            Text { width: parent.width; text: backend.displayColour.screenName; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading }
            ComboField {
                width: parent.width
                model: [qsTr("System colour management"), qsTr("Manual ICC — unmanaged display"), qsTr("sRGB — no monitor conversion")]
                tipTitle: qsTr("Monitor calibration")
                tip: qsTr("Use system colour management for a managed desktop. Choose a manual ICC only when the desktop does not already convert colours for this monitor.")
                currentIndex: backend.displayColour.mode
                onActivated: i => backend.displayColour.mode = i
            }
            ToolButton {
                visible: backend.displayColour.mode === 1
                text: qsTr("Choose monitor profile…")
                tip: qsTr("Choose the RGB ICC calibration profile for this monitor. It affects displayed photos only.")
                onClicked: monitorProfileDialog.open()
            }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: backend.displayColour.status
                color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase
            }
            Text {
                visible: backend.displayColour.mode === 1
                width: parent.width; wrapMode: Text.WordWrap
                text: qsTr("Use a manual monitor profile only when your desktop does not already convert colours for the display. These settings follow this monitor and affect photo previews only.")
                color: Theme.warning; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
            }
          }
        }
    }
    FilePicker {
        id: catalogFileDialog
        objectName: "catalogFileDialog"
        title: qsTr("Open a catalog")
        nameFilters: [qsTr("OmaRAW catalog (*.db)")]
        onAccepted: win.switchCatalog(selectedFile.toString())
    }
    NewCatalogDialog {
        id: newCatalogDialog
        onCreated: path => win.switchCatalog(path)
    }
    // Switching reopens the window on the other catalog, so work that is
    // still running would be interrupted. The queue is kept with its own
    // catalog and resumes there; imports do not.
    C.Popup {
        id: switchBusyDialog
        objectName: "switchBusyDialog"
        modal: true
        anchors.centerIn: parent
        width: 400; padding: Theme.s4
        background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }
        property string pending: ""
        Column {
            width: parent.width
            spacing: Theme.s3
            Text { text: qsTr("Finish here first?"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading; color: Theme.textPrimary }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: engine.offlineBusy
                      ? qsTr("An offline-copy or Smart Preview job is still running. Opening another catalog closes this one; completed files and edits are kept, but remaining preview work is not resumed automatically.")
                      : backend.importing && engine.exporting
                      ? qsTr("An import and an export are still running. Opening another catalog closes this one: the import stops where it is, and the export queue waits here until you come back.")
                      : backend.importing
                      ? qsTr("An import is still running. Opening another catalog closes this one and stops the import where it is; photos already brought in are kept.")
                      : qsTr("An export is still running. Opening another catalog closes this one; the queue waits here and continues when you come back.")
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
            Row {
                anchors.right: parent.right
                spacing: Theme.s2
                ToolButton { text: qsTr("Stay"); showLabel: true; onClicked: switchBusyDialog.close() }
                ToolButton {
                    objectName: "switchAnyway"
                    text: qsTr("Open Anyway"); showLabel: true
                    onClicked: { const p = switchBusyDialog.pending; switchBusyDialog.close(); backend.switchCatalog(p) }
                }
            }
        }
    }
    FilePicker {
        id: backupManifestDialog
        objectName: "backupManifestDialog"
        title: qsTr("Choose manifest.json inside an OmaRAW backup")
        nameFilters: [qsTr("OmaRAW backup (manifest.json)")]
        onAccepted: restoreFolderDialog.open()
    }
    FolderPicker {
        id: restoreFolderDialog
        objectName: "restoreFolderDialog"
        title: qsTr("Choose where to create the recovered catalog")
        onAccepted: {
            const path = backend.restoreCatalogBackup(backupManifestDialog.selectedFile.toString(), selectedFolder.toString())
            if (path) { recoveredDialog.catalogPath = path; recoveredDialog.open() }
        }
    }
    C.Dialog {
        id: recoveredDialog
        parent: C.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 500
        title: qsTr("Catalog recovered")
        property string catalogPath: ""
        standardButtons: C.Dialog.Open | C.Dialog.Close
        palette.window: Theme.panelBg
        palette.windowText: Theme.textPrimary
        palette.button: Theme.controlBg
        palette.buttonText: Theme.textPrimary
        palette.dark: Theme.borderStrong
        onAccepted: backend.openCatalogWindow(catalogPath)
        contentItem: Text {
            text: qsTr("The catalog and Develop history have been restored into a new folder. Your current catalog is unchanged. Open the recovered catalog in another window?\n\n%1").arg(recoveredDialog.catalogPath)
            textFormat: Text.PlainText   // names and filenames are not markup
            wrapMode: Text.Wrap; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase
        }
    }
    // A snapshot straight from the keyboard: auto-named, no dialog.
    function quickSnapshot() {
        if (win.workspace === "Develop" && engine.imageId >= 0) backend.saveSnapshot(backend.currentId, "")
    }
    property var currentInfo: backend.currentId > 0 ? backend.info(backend.currentId) : ({})
    readonly property bool currentIsVariant: (currentInfo.variant || 0) > 0
    Connections {
        target: backend
        function onSelectionChanged() { win.currentInfo = backend.currentId > 0 ? backend.info(backend.currentId) : {} }
        function onVariantsChanged() { win.currentInfo = backend.currentId > 0 ? backend.info(backend.currentId) : {} }
        function onDevelopRequested() { win.workspace = "Develop" }
    }
    property alias cardMenu: cardMenu
    property alias commandPalette: commandPalette
    function toggleOriginal() { developWs.toggleOriginal() }
    // Count matching commands after rebuilding the palette.
    function paletteCommandCount() { commandPalette.gather(); return commandPalette.commands.length }

    // Command search over every menu, Ctrl+K.
    property bool paletteOpen: false
    onPaletteOpenChanged: if (paletteOpen) commandPalette.open(); else commandPalette.close()
    CommandPalette {
        id: commandPalette
        menuBar: titleBar.menuBar
        extras: [
            {label: qsTr("Light Interface"), path: qsTr("Preferences"), shortcut: "", tip: qsTr("Use light panels and dark text. Photographs keep a neutral surround."), enabled: true, run: () => backend.lightInterface = !backend.lightInterface},
            {label: qsTr("High Contrast"), path: qsTr("Preferences"), shortcut: "", tip: qsTr("Increase contrast in interface text and borders."), enabled: true, run: () => backend.highContrast = !backend.highContrast},
            {label: qsTr("Reduced Motion"), path: qsTr("Preferences"), shortcut: "", tip: qsTr("Reduce interface animations."), enabled: true, run: () => backend.reducedMotion = !backend.reducedMotion},
            {label: qsTr("Label Names…"), path: qsTr("Preferences"), shortcut: "", tip: qsTr("Name your colour labels."), enabled: true, run: () => win.editLabelNames()},
            {label: qsTr("Camera Profiles Folder…"), path: qsTr("Preferences"), shortcut: "", tip: qsTr("Choose the folder of DCP camera profiles."), enabled: true, run: () => win.chooseCameraProfilesFolder()},
            {label: qsTr("Colour Management…"), path: qsTr("Preferences"), shortcut: "", tip: qsTr("Configure the viewing transform and monitor profile."), enabled: true, run: () => win.displayColourSettings()},
            {label: qsTr("Daily Backup on Open"), path: qsTr("Preferences"), shortcut: "", tip: qsTr("Toggle daily catalog backups."), enabled: true, run: () => backend.autoBackup = !backend.autoBackup},
            {label: qsTr("Write XMP Sidecars Automatically"), path: qsTr("Preferences"), shortcut: "", tip: qsTr("Save metadata and edits beside originals."), enabled: true, run: () => backend.writeSidecars = !backend.writeSidecars},
            {label: qsTr("Write Into DNG, JPEG and TIFF Files"), path: qsTr("Preferences"), shortcut: "", tip: qsTr("Write metadata and edits inside these original files."), enabled: true, run: () => backend.embedXmp = !backend.embedXmp}
        ]
        parent: C.Overlay.overlay
        onClosed: win.paletteOpen = false
    }

    // ── dialogs ─────────────────────────────────────────────────────────
    // Develop settings across photos: copy the chosen groups, paste them on
    // the open photo or the whole selection, or sync them from the open
    // photo to the rest of the selection.
    SyncDialog { id: syncDialog; objectName: "settingsTransferDialog"; onClosed: win.syncOpen = false }
    property alias syncDialog: syncDialog
    property bool syncOpen: false
    onSyncOpenChanged: if (syncOpen) syncDialog.openFor("sync", win.otherSelected(), win.currentInfo); else syncDialog.close()
    function otherSelected() {
        return backend.exportItems(false).filter(it => it.path !== win.currentInfo.path || it.variant !== win.currentInfo.variant)
    }
    function copySettings() { if (!libraryBrowsing && engine.ready && backend.currentId > 0) syncDialog.openFor("copy", [], win.currentInfo) }
    function syncSettings() { if (!libraryBrowsing && engine.ready && backend.currentId > 0 && backend.selectedCount > 1) syncDialog.openFor("sync", win.otherSelected(), win.currentInfo) }
    function pasteSettings() {
        if (libraryBrowsing || !engine.hasSettingsClipboard) return
        if (backend.selectedCount > 1 || (win.workspace !== "Develop" && backend.selectedCount > 0)) engine.applyValuesTo(backend.exportItems(false), engine.settingsClipboard())
        else engine.pasteSettings()
    }
    FilePicker {
        id: presetExportDialog
        objectName: "presetExportDialog"
        title: qsTr("Save your presets as a file")
        description: qsTr("Choose a folder and file name for your OmaRAW presets.")
        fileName: qsTr("OmaRAW presets.json")
        fileMode: PathPicker.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("OmaRAW presets (*.json)")]
        onAccepted: backend.exportPresets(selectedFile.toString())
    }
    FilePicker {
        id: presetImportDialog
        objectName: "presetImportDialog"
        title: qsTr("Import presets")
        fileMode: PathPicker.OpenFiles
        nameFilters: [qsTr("Preset files (*.json *.xmp *.lrtemplate *.JSON *.XMP *.LRTEMPLATE)"), qsTr("OmaRAW presets (*.json)"), qsTr("XMP presets (*.xmp *.lrtemplate)"), qsTr("All files (*)")]
        onAccepted: {
            backend.importPresetFiles(selectedFiles.map(file => file.toString()))
            Qt.callLater(function() { presetImportReportDialog.open() })
        }
    }
    FolderPicker {
        id: cameraProfilesDialog
        objectName: "cameraProfilesDialog"
        title: qsTr("Choose camera profiles folder")
        description: qsTr("Choose the folder containing your .dcp profiles. Subfolders are searched too.")
        currentFolder: backend.fileUrl(backend.cameraProfilesFolder())
        onAccepted: backend.setCameraProfilesFolder(selectedFolder.toString())
    }
    FilePicker {
        id: locateDialog
        objectName: "locateDialog"
        title: qsTr("Where is this file now?")
        onAccepted: { const e = backend.relinkPhoto(backend.currentId, selectedFile.toString()); if (e !== "") backend.setStatus(qsTr("Could not relink: %1").arg(e)) }
    }
    FolderPicker {
        id: relinkAllDialog
        objectName: "relinkAllDialog"
        title: qsTr("Look for the offline photos under a folder")
        onAccepted: backend.relinkOfflineIn(selectedFolder.toString())
    }
    function locateFile() { if (backend.currentId > 0) locateDialog.open() }
    function relinkOffline() { relinkAllDialog.open() }
    function exportPresets() { presetExportDialog.open() }
    function importPresets() { presetImportDialog.open() }
    function showPresetImportReport() { presetImportReportDialog.open() }
    function chooseCameraProfilesFolder() { cameraProfilesDialog.open() }
    FolderPicker {
        id: moveDialog
        objectName: "moveDialog"
        title: qsTr("Move the selected photos to a folder")
        onAccepted: backend.moveSelectionTo(selectedFolder.toString())
    }
    C.Popup {
        id: labelNamesDialog
        modal: true
        anchors.centerIn: parent
        width: 380; padding: Theme.s4
        background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }
        Column {
            width: parent.width
            spacing: Theme.s2
            Text { text: qsTr("Label names"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading; color: Theme.textPrimary }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: qsTr("Give a colour a meaning — Client, Print, Web. The colour is what sidecars and other programs see; the name is yours.")
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
            Repeater {
                model: Theme.labelNames
                Item {
                    required property string modelData
                    width: parent.width; height: Theme.hControl + Theme.s1
                    Rectangle { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; width: 12; height: 12; radius: 2; color: Theme.labelColor(parent.modelData) }
                    Text { anchors.left: parent.left; anchors.leftMargin: 20; anchors.verticalCenter: parent.verticalCenter; width: 70; text: parent.modelData.charAt(0).toUpperCase() + parent.modelData.slice(1); font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted }
                    SearchField {
                        anchors.left: parent.left; anchors.leftMargin: 96; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                        placeholder: qsTr("Name")
                        text: backend.labelNames[parent.modelData] || ""
                        live: false
                        onAccepted: t => backend.setLabelName(parent.modelData, t)
                    }
                }
            }
            Row {
                anchors.right: parent.right
                ToolButton { text: qsTr("Done"); showLabel: true; onClicked: labelNamesDialog.close() }
            }
        }
    }
    C.Popup {
        id: trashDialog
        modal: true
        anchors.centerIn: parent
        width: 380; padding: Theme.s4
        background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }
        Column {
            width: parent.width
            spacing: Theme.s3
            Text { textFormat: Text.PlainText; text: backend.selectedCount === 1 ? qsTr("Move %1 to the trash?").arg(backend.currentFilename) : qsTr("Move %1 photos to the trash?").arg(backend.selectedCount); font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading; color: Theme.textPrimary }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: qsTr("The files and their sidecars go to the system trash and leave the catalog, with their edits. Remove from Catalog (Del) keeps the files where they are.")
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
            Row {
                anchors.right: parent.right
                spacing: Theme.s2
                ToolButton { text: qsTr("Cancel"); showLabel: true; onClicked: trashDialog.close() }
                ToolButton { text: qsTr("Move to Trash"); showLabel: true; onClicked: { backend.moveSelectionToTrash(); trashDialog.close() } }
            }
        }
    }
    C.Popup {
        id: renameFileDialog
        modal: true
        anchors.centerIn: parent
        width: 380; padding: Theme.s4
        background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }
        onOpened: { fileName.text = win.currentInfo.filename || ""; fileName.forceActiveFocus() }
        function apply() { const n = fileName.text.trim(); if (n !== "" && backend.renamePhoto(backend.currentId, n) === "") renameFileDialog.close() }
        Column {
            width: parent.width
            spacing: Theme.s3
            Text { textFormat: Text.PlainText; text: qsTr("Rename %1 on disk").arg(win.currentInfo.filename || ""); font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading; color: Theme.textPrimary }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: qsTr("The file, its sidecar, its variants and its edits follow the new name. Leave the extension off to keep it.")
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
            SearchField {
                id: fileName
                width: parent.width
                placeholder: qsTr("File name")
                live: false
                onAccepted: t => renameFileDialog.apply()
            }
            Row {
                anchors.right: parent.right
                spacing: Theme.s2
                ToolButton { text: qsTr("Cancel"); showLabel: true; onClicked: renameFileDialog.close() }
                ToolButton { text: qsTr("Rename"); showLabel: true; enabled: fileName.text.trim() !== ""; onClicked: renameFileDialog.apply() }
            }
        }
    }
    readonly property bool libraryBrowsing: workspace === "Library" && libraryWs.browsing
    readonly property string libraryViewMode: libraryBrowsing ? (libraryWs.importWorkspace.photoBrowser.previewing ? "loupe" : "grid") : browserMode
    function browsePhotos(path) {
        workspace = "Library"; sourceDockVisible = true
        libraryWs.browse(path || "")
    }
    function selectAllPhotos(pick) { libraryWs.selectAllPhotos(pick) }
    function showPhotos(mode) { libraryWs.showPhotos(mode) }
    // Offscreen review hook: show a folder in the Library's Browse tab.
    property string browseFolder: ""
    onBrowseFolderChanged: if (browseFolder !== "") browsePhotos(browseFolder)
    C.Popup {
        id: albumDialog
        property int parentId: 0
        modal: true
        anchors.centerIn: parent
        width: 360; padding: Theme.s4
        background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }
        onOpened: { albumName.text = ""; albumName.forceActiveFocus() }
        Column {
            width: parent.width
            spacing: Theme.s3
            Text { text: albumDialog.parentId ? qsTr("New album inside, from %1 selected").arg(backend.selectedCount) : qsTr("New album from %1 selected").arg(backend.selectedCount); font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading; color: Theme.textPrimary }
            SearchField {
                id: albumName
                width: parent.width
                placeholder: qsTr("Album name")
                live: false
                onAccepted: t => { if (t.trim() !== "") { backend.addSelectionToAlbum(backend.createAlbum(t.trim(), albumDialog.parentId)); albumDialog.close() } }
            }
            Row {
                anchors.right: parent.right
                spacing: Theme.s2
                ToolButton { text: qsTr("Cancel"); showLabel: true; onClicked: albumDialog.close() }
                ToolButton {
                    text: qsTr("Create"); showLabel: true
                    enabled: albumName.text.trim() !== ""
                    onClicked: { backend.addSelectionToAlbum(backend.createAlbum(albumName.text.trim(), albumDialog.parentId)); albumDialog.close() }
                }
            }
        }
    }
    C.Popup {
        id: variantDialog
        modal: true
        anchors.centerIn: parent
        width: 360; padding: Theme.s4
        background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }
        onOpened: { variantName.text = win.currentInfo.variantName || ""; variantName.forceActiveFocus() }
        Column {
            width: parent.width
            spacing: Theme.s3
            Text { textFormat: Text.PlainText; text: qsTr("Name this variant of %1").arg(win.currentInfo.filename || ""); font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading; color: Theme.textPrimary }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: qsTr("The name shows on the card and becomes part of the exported file name.")
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
            SearchField {
                id: variantName
                width: parent.width
                placeholder: qsTr("Variant name")
                live: false
                onAccepted: t => { backend.renameVariant(backend.currentId, t.trim()); variantDialog.close() }
            }
            Row {
                anchors.right: parent.right
                spacing: Theme.s2
                ToolButton { text: qsTr("Cancel"); showLabel: true; onClicked: variantDialog.close() }
                ToolButton { text: qsTr("Rename"); showLabel: true; onClicked: { backend.renameVariant(backend.currentId, variantName.text.trim()); variantDialog.close() } }
            }
        }
    }
    C.Popup {
        id: autoStackDialog
        modal: true
        anchors.centerIn: parent
        width: 360; padding: Theme.s4
        background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }
        property int seconds: 5
        Column {
            width: parent.width
            spacing: Theme.s3
            Text { text: qsTr("Auto-stack by capture time"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading; color: Theme.textPrimary }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: qsTr("Shots in %1 taken within this many seconds of the previous one become a stack. Stacked photos are left alone.").arg(backend.sourceTitle)
                textFormat: Text.PlainText   // names and filenames are not markup
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
            SliderField {
                width: parent.width
                label: qsTr("Seconds"); from: 1; to: 120; stepSize: 1; decimals: 0
                value: autoStackDialog.seconds
                onEdited: v => autoStackDialog.seconds = v
                onEditingFinished: v => autoStackDialog.seconds = v
            }
            Row {
                anchors.right: parent.right
                spacing: Theme.s2
                ToolButton { text: qsTr("Cancel"); showLabel: true; onClicked: autoStackDialog.close() }
                ToolButton { text: qsTr("Stack"); showLabel: true; onClicked: { backend.autoStackShown(autoStackDialog.seconds); autoStackDialog.close() } }
            }
        }
    }
    ContextMenu {
        id: cardMenu
        objectName: "photoContextMenu"
        implicitWidth: 270
        MenuAction { text: qsTr("Open in Loupe"); iconName: "image"; shortcut: "E"; onTriggered: win.browserMode = "loupe" }
        MenuAction { text: qsTr("Develop"); iconName: "sliders-horizontal"; shortcut: "Ctrl+2"; onTriggered: win.workspace = "Develop" }
        C.MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.border } }
        MenuAction { objectName: "photoCopySettings"; text: qsTr("Copy Settings…"); iconName: "copy"; shortcut: "Ctrl+Alt+C"; enabled: engine.ready && backend.currentId > 0; onTriggered: win.copySettings() }
        MenuAction { objectName: "photoPasteSettings"; text: qsTr("Paste Settings"); iconName: "copy"; shortcut: "Ctrl+Alt+V"; enabled: engine.ready && engine.hasSettingsClipboard && backend.selectedCount > 0; onTriggered: win.pasteSettings() }
        MenuAction { objectName: "photoSyncSettings"; text: qsTr("Sync Settings…"); iconName: "arrow-left-right"; shortcut: "Ctrl+Alt+S"; enabled: engine.ready && backend.currentId > 0 && backend.selectedCount > 1; onTriggered: win.syncSettings() }
        C.MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.border } }
        ContextMenu {
            title: qsTr("Stacks")
            MenuAction { text: qsTr("Stack Selection"); iconName: "layers"; shortcut: "Ctrl+G"; enabled: backend.selectedCount > 1; onTriggered: backend.stackSelection() }
            MenuAction { text: qsTr("Unstack"); iconName: "layers"; shortcut: "Ctrl+Shift+G"; enabled: (win.currentInfo.stackCount || 0) > 1; onTriggered: backend.unstackSelection() }
            MenuAction { text: backend.isStackExpanded(win.currentInfo.stackId || 0) ? qsTr("Collapse Stack") : qsTr("Expand Stack"); iconName: "chevrons-up-down"; shortcut: "S"; enabled: (win.currentInfo.stackCount || 0) > 1; onTriggered: backend.toggleStack(backend.currentId) }
            MenuAction { text: qsTr("Set as Stack Top"); iconName: "arrow-up"; shortcut: "Shift+S"; enabled: (win.currentInfo.stackPos || 0) > 0; onTriggered: backend.setStackTop(backend.currentId) }
            MenuAction { text: qsTr("Auto-Stack by Capture Time…"); iconName: "clock"; onTriggered: autoStackDialog.open() }
        }
        C.MenuSeparator { visible: backend.customOrderAvailable; height: visible ? implicitHeight : 0; contentItem: Rectangle { implicitHeight: 1; color: Theme.border } }
        MenuAction { visible: backend.customOrderAvailable; text: qsTr("Move Earlier in Album"); iconName: "chevron-left"; shortcut: "Ctrl+Left"; onTriggered: backend.moveInAlbum(-1) }
        MenuAction { visible: backend.customOrderAvailable; text: qsTr("Move Later in Album"); iconName: "chevron-right"; shortcut: "Ctrl+Right"; onTriggered: backend.moveInAlbum(1) }
        MenuAction { visible: backend.customOrderAvailable; text: qsTr("Set as Album Cover"); iconName: "image"; onTriggered: backend.setAlbumCover(backend.sourceId, backend.currentId) }
        C.MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.border } }
        ContextMenu {
            title: qsTr("Variants")
            MenuAction { text: qsTr("Create Variant"); iconName: "copy"; shortcut: "Ctrl+'"; enabled: engine.ready; onTriggered: backend.createVariant(backend.currentId) }
            MenuAction { text: qsTr("Rename Variant…"); iconName: "pencil"; enabled: win.currentIsVariant; onTriggered: win.renameVariant() }
            MenuAction { text: qsTr("Promote Variant to Master"); iconName: "arrow-up"; enabled: win.currentIsVariant && engine.ready; onTriggered: backend.promoteVariant(backend.currentId) }
            MenuAction { text: qsTr("Delete Variant"); iconName: "trash-2"; enabled: win.currentIsVariant; onTriggered: backend.deleteVariant(backend.currentId) }
        }
        C.MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.border } }
        MenuAction { text: qsTr("Pick"); iconName: "flag"; shortcut: "P"; onTriggered: backend.setFlag(1) }
        MenuAction { text: qsTr("Reject"); iconName: "x"; shortcut: "X"; onTriggered: backend.setFlag(-1) }
        MenuAction { text: qsTr("Unflag"); shortcut: "U"; onTriggered: backend.setFlag(0) }
        C.MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.border } }
        MenuAction { text: qsTr("Add to Quick Collection"); iconName: "plus"; shortcut: "B"; onTriggered: backend.addSelectionToQuickCollection() }
        AddToAlbumMenu { objectName: "photoAddToAlbumMenu" }
        MenuAction { text: qsTr("New Album from Selection…"); iconName: "book-image"; onTriggered: win.newAlbum() }
        C.MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.border } }
        MenuAction { text: qsTr("Rename File…"); iconName: "pencil"; onTriggered: win.renameFile() }
        MenuAction { text: qsTr("Move Selection to Folder…"); iconName: "folder"; onTriggered: win.moveSelection() }
        MenuAction { text: qsTr("Reveal in File Manager"); iconName: "folder-open"; onTriggered: backend.revealInFileManager(backend.currentId) }
        MenuAction { text: qsTr("Open location in browser"); Accessible.description: qsTr("Opens OpenStreetMap in your browser and shares this photo’s coordinates with that site."); iconName: "external-link"; enabled: win.currentInfo.hasGps || false; onTriggered: backend.openInMap(backend.currentId) }
        MenuAction { text: qsTr("Remove from Catalog"); iconName: "trash-2"; shortcut: "Del"; onTriggered: backend.removeSelectionFromCatalog() }
        MenuAction { text: qsTr("Move to Trash…"); iconName: "trash-2"; shortcut: "Shift+Del"; onTriggered: win.trashSelection() }
    }

    // ── layout ──────────────────────────────────────────────────────────
    Column {
        anchors.fill: parent
        spacing: 0
        AppTitleBar {
            id: titleBar
            width: parent.width
            target: win
            shell: win
            workspace: win.workspace
        }
        Item {
            width: parent.width
            height: parent.height - titleBar.height - statusBar.height
            LibraryWorkspace { id: libraryWs; objectName: "libraryWorkspace"; anchors.fill: parent; shell: win; visible: win.workspace === "Library" }
            DevelopWorkspace { id: developWs; anchors.fill: parent; shell: win; visible: win.workspace === "Develop" }
            CaptureWorkspace { anchors.fill: parent; shell: win; visible: win.workspace === "Capture" }
            OutputWorkspace { anchors.fill: parent; shell: win; visible: win.workspace === "Output" }
        }
        StatusBar {
            id: statusBar
            width: parent.width
            message: backend.statusMessage
            browseInfo: win.libraryBrowsing ? ({ count: libraryWs.importWorkspace.preview.count || 0, selected: libraryWs.importWorkspace.selectedCount, name: libraryWs.importWorkspace.photoBrowser.current ? libraryWs.importWorkspace.photoBrowser.current.name : "" }) : null
        }
    }

    // ── shortcuts ───────────────────────────────────────────────────────
    // One table drives the bindings, the cheat sheet and the editor. An
    // entry: id, default keys, label, group, action; `always` fires even
    // while a text field has the keyboard; `scope` ("Develop") marks keys
    // that only mean something there, so the same key can serve elsewhere.
    // Single keys only fire when no text field owns the keyboard.
    readonly property bool typing: activeFocusItem && (activeFocusItem.hasOwnProperty("cursorPosition"))
    readonly property var shortcutTable: [
        { id: "import", keys: "Ctrl+I", label: qsTr("Browse photos to import"), group: qsTr("General"), always: true, action: () => win.browsePhotos("") },
        { id: "export", keys: "Ctrl+Shift+E", label: qsTr("Export (opens Output)"), group: qsTr("General"), always: true, action: () => win.workspace = "Output" },
        { id: "wsLibrary", keys: "Ctrl+1", label: qsTr("Library"), group: qsTr("General"), always: true, action: () => win.workspace = "Library" },
        { id: "wsDevelop", keys: "Ctrl+2", label: qsTr("Develop"), group: qsTr("General"), always: true, action: () => win.workspace = "Develop" },
        { id: "wsCapture", keys: "Ctrl+3", label: qsTr("Capture"), group: qsTr("General"), always: true, action: () => win.workspace = "Capture" },
        { id: "wsOutput", keys: "Ctrl+4", label: qsTr("Output"), group: qsTr("General"), always: true, action: () => win.workspace = "Output" },
        { id: "palette", keys: "Ctrl+K", label: qsTr("Command search"), group: qsTr("General"), action: () => commandPalette.open() },
        { id: "help", keys: "F1", label: qsTr("Help for the current workspace"), group: qsTr("General"), always: true, action: () => win.help() },
        { id: "paletteAlt", keys: "Ctrl+Shift+P", label: qsTr("Command search"), group: qsTr("General"), hidden: true, action: () => commandPalette.open() },
        { id: "cheatSheet", keys: "?", label: qsTr("Keyboard shortcuts"), group: qsTr("General"), action: () => win.editShortcuts() },
        { id: "quit", keys: "Ctrl+Q", label: qsTr("Quit"), group: qsTr("General"), always: true, action: () => win.close() },
        { id: "grid", keys: "G", label: qsTr("Grid"), group: qsTr("Library"), action: () => win.showPhotos("grid") },
        { id: "list", keys: "L", label: qsTr("List"), group: qsTr("Library"), action: () => win.browserMode = "list" },
        { id: "loupe", keys: "E", label: qsTr("Loupe"), group: qsTr("Library"), action: () => win.showPhotos("loupe") },
        { id: "compare", keys: "C", label: qsTr("Compare"), group: qsTr("Library"), action: () => win.browserMode = "compare" },
        { id: "survey", keys: "N", label: qsTr("Survey"), group: qsTr("Library"), action: () => win.browserMode = "survey" },
        { id: "next", keys: "Right", label: qsTr("Next photo"), group: qsTr("Library"), action: () => libraryWs.stepPhoto(1) },
        { id: "previous", keys: "Left", label: qsTr("Previous photo"), group: qsTr("Library"), action: () => libraryWs.stepPhoto(-1) },
        { id: "selectAll", keys: "Ctrl+A", label: qsTr("Select all"), group: qsTr("Library"), action: () => win.selectAllPhotos(true) },
        { id: "selectNone", keys: "Ctrl+D", label: qsTr("Select none"), group: qsTr("Library"), action: () => win.selectAllPhotos(false) },
        { id: "cardsBigger", keys: "Ctrl++", label: qsTr("Larger thumbnails"), group: qsTr("Library"), always: true, action: () => win.cardSize = Math.min(Theme.szCardMax, win.cardSize + 40) },
        { id: "cardsBiggerAlt", keys: "Ctrl+=", label: qsTr("Larger thumbnails"), group: qsTr("Library"), hidden: true, always: true, action: () => win.cardSize = Math.min(Theme.szCardMax, win.cardSize + 40) },
        { id: "cardsSmaller", keys: "Ctrl+-", label: qsTr("Smaller thumbnails"), group: qsTr("Library"), always: true, action: () => win.cardSize = Math.max(Theme.szCardMin, win.cardSize - 40) },
        { id: "stack", keys: "Ctrl+G", label: qsTr("Stack selection"), group: qsTr("Library"), action: () => backend.stackSelection() },
        { id: "unstack", keys: "Ctrl+Shift+G", label: qsTr("Unstack"), group: qsTr("Library"), action: () => backend.unstackSelection() },
        { id: "toggleStack", keys: "S", label: qsTr("Expand or collapse stack"), group: qsTr("Library"), action: () => backend.toggleStack(backend.currentId) },
        { id: "stackTop", keys: "Shift+S", label: qsTr("Set stack top"), group: qsTr("Library"), action: () => backend.setStackTop(backend.currentId) },
        { id: "quickCollection", keys: "B", label: qsTr("Add to Quick Collection"), group: qsTr("Library"), action: () => backend.addSelectionToQuickCollection() },
        { id: "albumEarlier", keys: "Ctrl+Left", label: qsTr("Move earlier in the album"), group: qsTr("Library"), when: () => backend.customOrderAvailable, action: () => backend.moveInAlbum(-1) },
        { id: "albumLater", keys: "Ctrl+Right", label: qsTr("Move later in the album"), group: qsTr("Library"), when: () => backend.customOrderAvailable, action: () => backend.moveInAlbum(1) },
        { id: "remove", keys: "Delete", label: qsTr("Remove from catalog (files untouched)"), group: qsTr("Library"), action: () => backend.removeSelectionFromCatalog() },
        { id: "trash", keys: "Shift+Delete", label: qsTr("Move to the trash"), group: qsTr("Library"), action: () => win.trashSelection() },
        { id: "pick", keys: "P", label: qsTr("Pick"), group: qsTr("Culling"), action: () => backend.setFlag(1) },
        { id: "reject", keys: "X", label: qsTr("Reject"), group: qsTr("Culling"), action: () => backend.setFlag(-1) },
        { id: "unflag", keys: "U", label: qsTr("Unflag"), group: qsTr("Culling"), action: () => backend.setFlag(0) },
        { id: "rating0", keys: "0", label: qsTr("Unrated"), group: qsTr("Culling"), action: () => backend.setRating(0) },
        { id: "rating1", keys: "1", label: qsTr("1 star"), group: qsTr("Culling"), action: () => backend.setRating(1) },
        { id: "rating2", keys: "2", label: qsTr("2 stars"), group: qsTr("Culling"), action: () => backend.setRating(2) },
        { id: "rating3", keys: "3", label: qsTr("3 stars"), group: qsTr("Culling"), action: () => backend.setRating(3) },
        { id: "rating4", keys: "4", label: qsTr("4 stars"), group: qsTr("Culling"), action: () => backend.setRating(4) },
        { id: "rating5", keys: "5", label: qsTr("5 stars"), group: qsTr("Culling"), action: () => backend.setRating(5) },
        { id: "labelRed", keys: "6", label: qsTr("Red label"), group: qsTr("Culling"), action: () => backend.toggleLabel("red") },
        { id: "labelYellow", keys: "7", label: qsTr("Yellow label"), group: qsTr("Culling"), action: () => backend.toggleLabel("yellow") },
        { id: "labelGreen", keys: "8", label: qsTr("Green label"), group: qsTr("Culling"), action: () => backend.toggleLabel("green") },
        { id: "labelBlue", keys: "9", label: qsTr("Blue label"), group: qsTr("Culling"), action: () => backend.toggleLabel("blue") },
        { id: "variant", keys: "Ctrl+'", label: qsTr("Create a variant"), group: qsTr("Develop"), action: () => backend.createVariant(backend.currentId) },
        { id: "snapshot", keys: "Ctrl+N", label: qsTr("Snapshot of the current settings"), group: qsTr("Develop"), action: () => win.quickSnapshot() },
        { id: "undo", keys: "Ctrl+Z", label: qsTr("Undo a develop step"), group: qsTr("Develop"), scope: "Develop", when: () => engine.imageId >= 0 && engine.historyEnd > 0, action: () => engine.undo() },
        { id: "redo", keys: "Ctrl+Shift+Z", label: qsTr("Redo a develop step"), group: qsTr("Develop"), scope: "Develop", when: () => engine.imageId >= 0 && engine.historyEnd < engine.history.length, action: () => engine.redo() },
        { id: "crop", keys: "R", label: qsTr("Crop and straighten"), group: qsTr("Develop"), scope: "Develop", action: () => developWs.toggleCrop() },
        { id: "lightsOut", keys: "Shift+L", label: qsTr("Lights out"), group: qsTr("Develop"), scope: "Develop", action: () => win.lightsOut = !win.lightsOut },
        { id: "copyMask", keys: "Ctrl+Shift+C", label: qsTr("Copy the active mask"), group: qsTr("Develop"), scope: "Develop", when: () => engine.activeLocal >= 0, action: () => engine.copyMask() },
        { id: "pasteMask", keys: "Ctrl+Shift+V", label: qsTr("Paste the mask"), group: qsTr("Develop"), scope: "Develop", when: () => engine.hasMaskClipboard && engine.imageId >= 0, action: () => engine.pasteMask() },
        { id: "copySettings", keys: "Ctrl+Alt+C", label: qsTr("Copy develop settings"), group: qsTr("Develop"), action: () => win.copySettings() },
        { id: "pasteSettings", keys: "Ctrl+Alt+V", label: qsTr("Paste develop settings"), group: qsTr("Develop"), action: () => win.pasteSettings() },
        { id: "syncSettings", keys: "Ctrl+Alt+S", label: qsTr("Sync settings to the selection"), group: qsTr("Develop"), action: () => win.syncSettings() }
    ]
    // The hold keys are not in the table: they are held, not pressed (see below).
    readonly property var holdKeyTable: [
        { keys: "\\", label: qsTr("Before / after: tap to toggle, hold to peek at the original"), group: qsTr("Develop") },
        { keys: "J", label: qsTr("Clipping indicators: tap to toggle, hold to peek"), group: qsTr("Develop") },
        { keys: "F", label: qsTr("False colour: tap to toggle, hold to peek"), group: qsTr("Develop") },
        { keys: "M", label: qsTr("Mask overlay: tap to toggle, hold to peek"), group: qsTr("Develop") }
    ]
    function keysFor(id, def) { const o = backend.shortcutOverrides[id]; return o !== undefined ? o : def }
    Component.onCompleted: { const d = {}; for (const e of shortcutTable) d[e.id] = e.keys; backend.registerShortcuts(d) }
    function shortcutEnabled(e) {
        if (!(e.always || !win.typing)) return false
        if (win.libraryBrowsing && e.group !== qsTr("General") && ["grid", "loupe", "next", "previous", "selectAll", "selectNone"].indexOf(e.id) < 0) return false
        if (win.libraryBrowsing && e.id === "export") return false
        if (e.scope === "Develop" && win.workspace !== "Develop") return false
        return e.when ? e.when() : true
    }
    Repeater {
        model: win.shortcutTable
        Item {
            id: holder
            required property var modelData
            readonly property string binding: win.keysFor(modelData.id, modelData.keys)
            Shortcut {
                // Some Wayland layouts retain Shift on the question-mark
                // key event. Keep both forms on the same configurable action.
                sequences: holder.modelData.id === "cheatSheet" && holder.binding === "?"
                    ? ["?", "Shift+?"] : [holder.binding]
                context: Qt.ApplicationShortcut
                enabled: holder.binding !== "" && win.shortcutEnabled(holder.modelData)
                onActivated: holder.modelData.action()
            }
        }
    }
    // A text field keeps its own Escape (clear, then let go of the focus).
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; enabled: !win.typing && !titleBar.menuOpen && !commandPalette.visible && !shortcutsDialog.visible && !helpDialog.visible; onActivated: { if (win.lightsOut) win.lightsOut = false; else if (win.workspace === "Develop" && engine.scopeExpanded) engine.scopeExpanded = false; else if (win.workspace === "Develop" && engine.cropMode) engine.cropMode = false; else if (win.workspace === "Develop" && developWs.leaveTool()) {} else if (win.libraryBrowsing) libraryWs.showPhotos("grid"); else if (win.browserMode === "loupe") win.browserMode = "grid" } }

    // Hold to peek: a tap toggles the view, a press held past 300 ms shows
    // it only while the key is down. Keys arrive from the application
    // event filter so the release is seen too.
    property string heldKey: ""
    property bool holdEngaged: false
    Timer {
        id: holdTimer
        interval: 300
        onTriggered: if (win.heldKey !== "") { win.holdEngaged = true; win.peek(win.heldKey, true) }
    }
    function peek(name, on) {
        if (name === "backslash") developWs.setOriginal(on)
        else if (name === "j") engine.clippingShown = on
        else if (name === "m") developWs.peekMaskCoverage(on)
        else if (name === "f") { if (engine.falseColour !== on) engine.toggleFalseColour() }
    }
    function toggleHoldKey(name) {
        if (name === "backslash") developWs.toggleOriginal()
        else if (name === "j") engine.clippingShown = !engine.clippingShown
        else if (name === "m") developWs.toggleMaskCoverage()
        else if (name === "f") engine.toggleFalseColour()
    }
    Connections {
        target: backend
        function onHoldKey(name, down) {
            if (win.typing || win.workspace !== "Develop" || shortcutsDialog.visible) return
            if (down) { if (win.heldKey !== "") return; win.heldKey = name; win.holdEngaged = false; holdTimer.restart(); return }
            if (win.heldKey !== name) return
            holdTimer.stop()
            if (win.holdEngaged) win.peek(name, false); else win.toggleHoldKey(name)
            win.heldKey = ""; win.holdEngaged = false
        }
    }
    ShortcutsDialog { id: shortcutsDialog; shell: win }
    PresetImportDialog { id: presetImportReportDialog }
    HelpDialog { id: helpDialog; shell: win }
    function editShortcuts() { shortcutsDialog.open() }
    property string helpShown: ""
    onHelpShownChanged: if (helpShown !== "") help(helpShown)
    property bool shortcutsEditor: false
    onShortcutsEditorChanged: if (shortcutsEditor) shortcutsDialog.open()
}
