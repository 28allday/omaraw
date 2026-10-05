import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

// The 44 px application bar: identity, menus inline, the workspace bar
// centred and catalog actions at the right. No minimise/maximise/close
// buttons — the window manager owns those. On a frameless window this
// strip is the drag handle and double-click maximise target.
Rectangle {
    id: root
    property Window target: null
    property var shell: null
    property alias workspace: workspaces.current
    // The menu bar, for the command palette to walk.
    property alias menuBar: menuBar
    // A colour label's name in force (Preferences ▸ Label names…), re-read when they change.
    function labelText(colour) { const names = backend.labelNames; return names[colour] ? names[colour] : backend.labelName(colour) }

    objectName: "appTitleBar"
    readonly property bool twoRows: identity.width + menuBar.implicitWidth + workspaces.width + actions.width + Theme.s4 * 4 > width
    implicitHeight: Theme.hTitleBar * (twoRows ? 2 : 1)
    color: Theme.panelBg

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width; height: Theme.hairline
        color: Theme.border
    }
    TapHandler {
        onDoubleTapped: if (root.target)
            root.target.visibility = root.target.visibility === Window.Maximized
                                     ? Window.Windowed : Window.Maximized
    }
    DragHandler {
        target: null
        onActiveChanged: if (active && root.target) root.target.startSystemMove()
    }

    Row {
        id: identity
        anchors.left: parent.left
        anchors.leftMargin: Theme.s3
        y: (Theme.hTitleBar - height) / 2
        spacing: Theme.s2
        HoverHandler { id: identityHover }
        Tooltip {
            text: Theme.appName
            description: qsTr("Drag the title bar to move the window. Double-click it to maximise or restore.")
            visible: identityHover.hovered
        }
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: "aperture"; size: 20; color: Theme.accent
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: Theme.appName
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsTitle
            font.weight: Theme.wHeading
            color: Theme.textPrimary
        }
    }

    C.MenuBar {
        id: menuBar
        objectName: "appMenuBar"
        anchors.left: identity.right
        anchors.leftMargin: Theme.s4
        y: 0
        height: Theme.hTitleBar
        padding: 0
        background: Item {}
        delegate: C.MenuBarItem {
            id: mbi
            hoverEnabled: true
            implicitHeight: Theme.hControl + Theme.s1
            height: Theme.hTitleBar
            padding: Theme.s2
            contentItem: Text {
                text: mbi.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsControl
                color: mbi.highlighted ? Theme.textPrimary : Theme.textSecondary
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: Theme.rControl
                color: mbi.down ? Theme.pressedOn(Theme.controlBg)
                     : mbi.highlighted || mbi.hovered ? Theme.hoverBg : "transparent"
            }
            Tooltip {
                text: mbi.text
                description: mbi.menu ? mbi.menu.tip : ""
                visible: mbi.hovered && !mbi.down && !(mbi.menu && mbi.menu.visible)
            }
        }
        StyledMenu {
            objectName: "menuFile"
            title: qsTr("File")
            tip: qsTr("Open catalogs, import photos and export finished work.")
            SMenuItem { text: qsTr("New Catalog…"); tip: qsTr("Create a separate catalog with its own edits, keywords and albums, then switch to it."); onTriggered: root.shell.newCatalog() }
            SMenuItem { text: qsTr("Open Catalog…"); tip: qsTr("Choose a catalog file and reopen OmaRAW with that catalog."); onTriggered: root.shell.openCatalogFile() }
            StyledMenu {
                id: recentCatalogs
                title: qsTr("Open Recent")
                tip: qsTr("Switch to a recently opened catalog.")
                // The list is read when the menu opens: another window may
                // have opened a catalog since this one started.
                property var entries: []
                onAboutToShow: entries = backend.recentCatalogs()
                Repeater {
                    model: recentCatalogs.entries
                    SMenuItem {
                        required property var modelData
                        text: modelData.name + "  —  " + modelData.folder
                        tip: qsTr("Reopen OmaRAW with this catalog: %1").arg(modelData.path)
                        enabled: !modelData.current
                        onTriggered: root.shell.switchCatalog(modelData.path)
                    }
                }
                MenuSep {}
                SMenuItem { text: qsTr("Clear Recent"); tip: qsTr("Clear the recent-catalog list without deleting any catalogs."); onTriggered: backend.clearRecentCatalogs() }
            }
            MenuSep {}
            SMenuItem { text: qsTr("Browse Photos to Import"); tip: qsTr("Choose a folder to add, copy or move into your library, with optional verification and a second copy."); shortcut: "Ctrl+I"; onTriggered: root.shell.browsePhotos("") }
            SMenuItem { text: qsTr("Export…"); enabled: !root.shell.libraryBrowsing; tip: qsTr("Open Output to choose a file format, size and destination for the selected photos."); shortcut: "Ctrl+Shift+E"; onTriggered: root.shell.workspace = "Output" }
            MenuSep {}
            StyledMenu {
                title: qsTr("Catalog Maintenance")
                tip: qsTr("Back up, verify or recover a catalog.")
                SMenuItem { text: qsTr("Overview…"); tip: qsTr("See catalog details, check results, backups and preview cache usage."); onTriggered: root.shell.showCatalogMaintenance("") }
                SMenuItem { text: qsTr("Back Up Catalog Now…"); tip: qsTr("Save a verified backup of the catalog and Develop data when editing, imports and exports are idle."); onTriggered: root.shell.showCatalogMaintenance("backup") }
                SMenuItem { text: qsTr("Reveal Backups"); tip: qsTr("Open the folder containing this catalog’s backup bundles."); onTriggered: backend.revealBackups() }
                SMenuItem { objectName: "checkCatalogMenuAction"; text: qsTr("Check Catalog Integrity…"); tip: qsTr("Check catalog references and Develop database structure, with a report you can copy."); onTriggered: root.shell.showCatalogMaintenance("check") }
                SMenuItem { text: qsTr("Restore Backup…"); tip: qsTr("Restore a verified backup into a new folder and offer to open the recovered catalog."); onTriggered: root.shell.restoreBackup() }
            }
            MenuSep {}
            SMenuItem { text: qsTr("Quit"); tip: qsTr("Close OmaRAW. Catalog changes and edit history are saved as you work."); shortcut: "Ctrl+Q"; onTriggered: root.target.close() }
        }
        StyledMenu {
            objectName: "menuEdit"
            title: qsTr("Edit")
            tip: qsTr("Undo editing steps, select photos and change preferences.")
            SMenuItem { text: qsTr("Undo Develop Step"); tip: qsTr("Return the current photo to the previous step in its Develop history."); shortcut: "Ctrl+Z"; enabled: root.shell.workspace === "Develop" && engine.imageId >= 0 && engine.historyEnd > 0; onTriggered: engine.undo() }
            SMenuItem { text: qsTr("Redo Develop Step"); tip: qsTr("Reapply the next step in the current photo’s Develop history."); shortcut: "Ctrl+Shift+Z"; enabled: root.shell.workspace === "Develop" && engine.imageId >= 0 && engine.historyEnd < engine.history.length; onTriggered: engine.redo() }
            MenuSep {}
            SMenuItem { text: qsTr("Select All"); tip: qsTr("Select every photo in the current view."); shortcut: "Ctrl+A"; onTriggered: root.shell.selectAllPhotos(true) }
            SMenuItem { text: qsTr("Select None"); tip: qsTr("Clear the photo selection."); shortcut: "Ctrl+D"; onTriggered: root.shell.selectAllPhotos(false) }
            MenuSep {}
            SMenuItem { text: qsTr("Keyboard Shortcuts…"); tip: qsTr("Search and change keyboard shortcuts, check conflicts or restore the defaults."); onTriggered: root.shell.editShortcuts() }
            SMenuItem { text: qsTr("Preferences…"); tip: qsTr("Change library, recovery, performance, accessibility and layout settings."); onTriggered: root.shell.preferences() }
        }
        StyledMenu {
            objectName: "menuLibrary"
            enabled: !root.shell.libraryBrowsing
            title: qsTr("Library")
            tip: qsTr("Organise collections, stacks, metadata and offline photos.")
            StyledMenu {
                objectName: "libraryAlbumsMenu"
                title: qsTr("Albums")
                tip: qsTr("Collect photographs and arrange an album.")
                AddToAlbumMenu { objectName: "libraryAddToAlbumMenu" }
                SMenuItem { text: qsTr("New Album from Selection…"); enabled: backend.selectedCount > 0; onTriggered: root.shell.newAlbum() }
                SMenuItem { text: qsTr("Move Earlier in Album"); shortcut: "Ctrl+Left"; enabled: backend.customOrderAvailable; onTriggered: backend.moveInAlbum(-1) }
                SMenuItem { text: qsTr("Move Later in Album"); shortcut: "Ctrl+Right"; enabled: backend.customOrderAvailable; onTriggered: backend.moveInAlbum(1) }
                SMenuItem { text: qsTr("Set as Album Cover"); enabled: backend.customOrderAvailable && backend.currentId > 0; onTriggered: backend.setAlbumCover(backend.sourceId, backend.currentId) }
            }
            SMenuItem { text: qsTr("Add to Quick Collection"); tip: qsTr("Add the selection to a temporary collection for gathering photos from different folders."); shortcut: "B"; enabled: backend.currentId > 0; onTriggered: backend.addSelectionToQuickCollection() }
            StyledMenu {
                title: qsTr("Stacks")
                tip: qsTr("Group related photographs without moving their files.")
                SMenuItem { text: qsTr("Stack Selection"); shortcut: "Ctrl+G"; enabled: backend.selectedCount > 1; onTriggered: backend.stackSelection() }
                SMenuItem { text: qsTr("Unstack"); shortcut: "Ctrl+Shift+G"; enabled: (root.shell.currentInfo.stackCount || 0) > 1; onTriggered: backend.unstackSelection() }
                SMenuItem { text: backend.isStackExpanded(root.shell.currentInfo.stackId || 0) ? qsTr("Collapse Stack") : qsTr("Expand Stack"); shortcut: "S"; enabled: (root.shell.currentInfo.stackCount || 0) > 1; onTriggered: backend.toggleStack(backend.currentId) }
                SMenuItem { text: qsTr("Set as Stack Top"); shortcut: "Shift+S"; enabled: (root.shell.currentInfo.stackPos || 0) > 0; onTriggered: backend.setStackTop(backend.currentId) }
                MenuSep {}
                SMenuItem { text: qsTr("Auto-Stack by Capture Time…"); onTriggered: root.shell.autoStackPhotos() }
            }
            MenuSep {}
            StyledMenu {
                title: qsTr("Duplicates")
                tip: qsTr("Compare file checksums to find duplicate photos for review.")
                SMenuItem { text: qsTr("Checksum Selection"); tip: qsTr("Read the selected files and save their SHA-256 checksums for duplicate checking."); enabled: backend.selectedCount > 0; onTriggered: backend.hashSelection() }
                SMenuItem { text: qsTr("Select Duplicates"); tip: qsTr("Select shown photos with matching stored checksums. Run Checksum Selection first if needed."); onTriggered: backend.selectDuplicates() }
            }
            StyledMenu {
                title: qsTr("Sidecars")
                tip: qsTr("Save photo metadata and edits beside the originals, where compatible editors can find them, and review outside changes.")
                SMenuItem { text: qsTr("Write Sidecars for Selection Now"); tip: qsTr("Write the selection's metadata and edit to its XMP sidecars: OmaRAW's exact edit, and an approximate XMP version beside it."); enabled: backend.currentId > 0; onTriggered: backend.writeSidecarsForSelection() }
                SMenuItem { text: qsTr("Read Sidecars for Selection"); tip: qsTr("Take each selected photo's metadata and edit from its sidecar, over the catalog's — as after editing it in another program."); enabled: backend.currentId > 0; onTriggered: backend.readSidecarsForSelection() }
                SMenuItem { text: qsTr("Check Sidecars for Outside Changes"); tip: qsTr("Select photos whose sidecars disagree with the catalog, so you can review the changed fields."); onTriggered: backend.checkSidecars() }
            }
            MenuSep {}
            StyledMenu {
                objectName: "smartPreviewsMenu"
                title: qsTr("Smart Previews")
                tip: qsTr("Keep smaller editable previews for use when the originals are disconnected.")
                SMenuItem { objectName: "buildSmartPreviews"; text: qsTr("Build Smart Previews for Selection"); tip: qsTr("Save editable previews beside the catalog. Originals or full offline copies are required to build them."); enabled: engine.ready && !engine.offlineBusy && backend.selectedCount > 0; onTriggered: backend.buildSelectionSmartPreviews(true) }
                SMenuItem { text: qsTr("Cancel Preview Job"); tip: qsTr("Stop building previews; completed previews are kept."); enabled: engine.offlineBusy; onTriggered: engine.cancelOfflineCopies() }
                MenuSep {}
                SMenuItem { objectName: "discardSmartPreviews"; text: qsTr("Discard Smart Previews of Selection"); tip: qsTr("Reclaim preview storage when the matching originals or full offline copies are available. Edits stay in the catalog."); enabled: engine.ready && !engine.offlineBusy && backend.selectedCount > 0; onTriggered: backend.buildSelectionSmartPreviews(false) }
            }
            StyledMenu {
                title: qsTr("Offline Originals")
                tip: qsTr("Find missing originals and keep verified copies for editing away from the original drive.")
                SMenuItem { text: qsTr("Keep Selection Available Offline"); tip: qsTr("Keep verified full-quality copies so the selected photos can be edited and exported without their original drive."); enabled: engine.ready && !engine.offlineBusy && backend.selectedCount > 0; onTriggered: backend.keepSelectionOffline(true) }
                SMenuItem { text: qsTr("Cancel Offline Copy Job"); tip: qsTr("Stop the offline-copy operation; completed copies remain available."); enabled: engine.offlineBusy; onTriggered: engine.cancelOfflineCopies() }
                MenuSep {}
                SMenuItem { text: qsTr("Locate Offline File…"); tip: qsTr("Point the catalog at the current photo after its original file has moved."); enabled: backend.currentId > 0; onTriggered: root.shell.locateFile() }
                SMenuItem { text: qsTr("Relink Offline Photos from Folder…"); tip: qsTr("Search a folder for missing originals and reconnect matching files to their edits."); onTriggered: root.shell.relinkOffline() }
                SMenuItem { text: qsTr("Rescan Folders"); tip: qsTr("Check which folders are available and refresh the offline status of their photos."); onTriggered: backend.rescanFolders() }
                MenuSep {}
                SMenuItem { text: qsTr("Remove Offline Copies of Selection"); tip: qsTr("Remove working copies only when the matching originals are available and verified."); enabled: engine.ready && !engine.offlineBusy && backend.selectedCount > 0; onTriggered: backend.keepSelectionOffline(false) }
            }
        }
        StyledMenu {
            objectName: "menuPhoto"
            enabled: !root.shell.libraryBrowsing
            title: qsTr("Photo")
            tip: qsTr("Rate, flag and label photos, manage variants and files.")
            StyledMenu {
                title: qsTr("Rating")
                tip: qsTr("Set a star rating on the selected photos.")
                Repeater {
                    model: 6
                    SMenuItem {
                        required property int index
                        text: index === 0 ? qsTr("None") : qsTr("%1 Star%2").arg(index).arg(index === 1 ? "" : "s")
                        tip: index === 0 ? qsTr("Clear the star rating from the selected photos.") : qsTr("Give the selected photos %1 stars.").arg(index)
                        shortcut: String(index)
                        onTriggered: backend.setRating(index)
                    }
                }
            }
            StyledMenu {
                title: qsTr("Flag")
                tip: qsTr("Mark the selected photos as picks, unflagged or rejected.")
                SMenuItem { text: qsTr("Pick"); tip: qsTr("Mark the selected photos as picks for keeping or delivery."); shortcut: "P"; onTriggered: backend.setFlag(1) }
                SMenuItem { text: qsTr("Unflag"); tip: qsTr("Clear pick or reject flags from the selected photos."); shortcut: "U"; onTriggered: backend.setFlag(0) }
                SMenuItem { text: qsTr("Reject"); tip: qsTr("Mark the selected photos as rejected; they collect under Rejected and leave ordinary views."); shortcut: "X"; onTriggered: backend.setFlag(-1) }
            }
            StyledMenu {
                title: qsTr("Colour Label")
                tip: qsTr("Toggle a colour label on the selected photos.")
                SMenuItem { text: root.labelText("red"); tip: qsTr("Toggle the %1 colour label on the selected photos.").arg(root.labelText("red")); shortcut: "6"; onTriggered: backend.toggleLabel("red") }
                SMenuItem { text: root.labelText("yellow"); tip: qsTr("Toggle the %1 colour label on the selected photos.").arg(root.labelText("yellow")); shortcut: "7"; onTriggered: backend.toggleLabel("yellow") }
                SMenuItem { text: root.labelText("green"); tip: qsTr("Toggle the %1 colour label on the selected photos.").arg(root.labelText("green")); shortcut: "8"; onTriggered: backend.toggleLabel("green") }
                SMenuItem { text: root.labelText("blue"); tip: qsTr("Toggle the %1 colour label on the selected photos.").arg(root.labelText("blue")); shortcut: "9"; onTriggered: backend.toggleLabel("blue") }
                SMenuItem { text: root.labelText("orange"); tip: qsTr("Toggle the %1 colour label on the selected photos.").arg(root.labelText("orange")); onTriggered: backend.toggleLabel("orange") }
                SMenuItem { text: root.labelText("purple"); tip: qsTr("Toggle the %1 colour label on the selected photos.").arg(root.labelText("purple")); onTriggered: backend.toggleLabel("purple") }
                MenuSep {}
                SMenuItem { text: qsTr("None"); tip: qsTr("Clear the colour label from the selected photos."); onTriggered: backend.setLabel("") }
            }
            SMenuItem { text: qsTr("Auto-Advance After Rating"); tip: qsTr("Move to the next photo after rating, flagging or labelling a single photo."); checkable: true; checked: backend.autoAdvance; onTriggered: backend.autoAdvance = !backend.autoAdvance }
            MenuSep {}
            StyledMenu {
                title: qsTr("Variants")
                tip: qsTr("Keep independent edits of one original file.")
                SMenuItem { text: qsTr("Create Variant"); tip: qsTr("Create an independent edit of the same original file, starting from its current settings."); shortcut: "Ctrl+'"; enabled: engine.ready && backend.currentId > 0; onTriggered: backend.createVariant(backend.currentId) }
                SMenuItem { text: qsTr("Rename Variant…"); enabled: root.shell.currentIsVariant; onTriggered: root.shell.renameVariant() }
                SMenuItem { text: qsTr("Promote Variant to Master"); enabled: root.shell.currentIsVariant && engine.ready; onTriggered: backend.promoteVariant(backend.currentId) }
                SMenuItem { text: qsTr("Delete Variant"); enabled: root.shell.currentIsVariant; onTriggered: backend.deleteVariant(backend.currentId) }
            }
            MenuSep {}
            SMenuItem { text: qsTr("Rename File…"); tip: qsTr("Rename the current file on disk; its sidecar, variants and edits follow."); enabled: backend.currentId > 0; onTriggered: root.shell.renameFile() }
            SMenuItem { text: qsTr("Move Selection to Folder…"); tip: qsTr("Move selected files and sidecars to another folder while keeping their catalog entries and edits."); enabled: backend.selectedCount > 0; onTriggered: root.shell.moveSelection() }
            SMenuItem { text: qsTr("Reveal in File Manager"); tip: qsTr("Open the folder containing the current photo."); enabled: backend.currentId > 0; onTriggered: backend.revealInFileManager(backend.currentId) }
            MenuSep {}
            SMenuItem { text: qsTr("Remove from Catalog"); tip: qsTr("Remove the selected catalog entries and their edits; photo files stay on disk."); shortcut: "Del"; enabled: backend.selectedCount > 0; onTriggered: backend.removeSelectionFromCatalog() }
            SMenuItem { text: qsTr("Move to Trash…"); tip: qsTr("Ask before moving selected photo files and their sidecars to the system trash."); shortcut: "Shift+Del"; enabled: backend.selectedCount > 0; onTriggered: root.shell.trashSelection() }
        }
        StyledMenu {
            objectName: "menuAdjustments"
            enabled: !root.shell.libraryBrowsing
            title: qsTr("Adjustments")
            tip: qsTr("Correct photos, transfer settings and manage saved looks.")
            SMenuItem { text: qsTr("Auto Exposure"); tip: qsTr("Adjust the photo’s overall exposure automatically; the change can be undone."); enabled: engine.imageId >= 0; onTriggered: engine.autoExposure() }
            SMenuItem { text: qsTr("Auto White Balance"); tip: qsTr("Estimate a neutral white balance from the whole frame."); enabled: engine.imageId >= 0; onTriggered: engine.autoWhiteBalance() }
            SMenuItem { text: qsTr("White Balance As Shot"); tip: qsTr("Restore the white balance recorded by the camera."); enabled: engine.imageId >= 0; onTriggered: engine.whiteBalanceAsShot() }
            MenuSep {}
            SMenuItem { text: qsTr("Copy Settings…"); tip: qsTr("Choose which adjustment groups to copy from the current photo."); shortcut: "Ctrl+Alt+C"; enabled: engine.ready && backend.currentId > 0; onTriggered: root.shell.copySettings() }
            SMenuItem { text: qsTr("Paste Settings"); tip: qsTr("Apply the copied adjustment groups to the current photo or Library selection."); shortcut: "Ctrl+Alt+V"; enabled: engine.hasSettingsClipboard && (engine.imageId >= 0 || backend.selectedCount > 0); onTriggered: root.shell.pasteSettings() }
            SMenuItem { text: qsTr("Sync Settings…"); tip: qsTr("Choose adjustment groups to copy to the other selected photos and review the targets."); shortcut: "Ctrl+Alt+S"; enabled: engine.ready && backend.currentId > 0 && backend.selectedCount > 1; onTriggered: root.shell.syncSettings() }
            MenuSep {}
            SMenuItem { text: qsTr("Crop & Straighten"); tip: qsTr("Show crop handles, aspect ratios and straightening controls on the photo."); shortcut: "R"; checkable: true; checked: engine.cropMode; enabled: engine.imageId >= 0; onTriggered: engine.cropMode = !engine.cropMode }
            StyledMenu {
                title: qsTr("Masks")
                tip: qsTr("Make and transfer local adjustments.")
                SMenuItem { text: qsTr("Edit Masks"); enabled: engine.imageId >= 0; onTriggered: root.shell.openDevelopTool("Local") }
                SMenuItem { text: qsTr("Copy Mask"); tip: qsTr("Copy the active local adjustment, including its shapes, ranges and settings."); shortcut: "Ctrl+Shift+C"; enabled: root.shell.workspace === "Develop" && engine.activeLocal >= 0; onTriggered: engine.copyMask() }
                SMenuItem { text: qsTr("Paste Mask"); tip: qsTr("Add the copied local adjustment to this photo. Check shape positions on a differently sized frame."); shortcut: "Ctrl+Shift+V"; enabled: root.shell.workspace === "Develop" && engine.hasMaskClipboard && engine.imageId >= 0; onTriggered: engine.pasteMask() }
            }
            SMenuItem { text: qsTr("Retouch"); tip: qsTr("Heal, clone and reshape areas of the photo."); enabled: engine.imageId >= 0; onTriggered: root.shell.openDevelopTool("Retouch") }
            MenuSep {}
            StyledMenu {
                title: qsTr("Presets")
                tip: qsTr("Apply automatic presets or import and export saved looks.")
                SMenuItem { text: qsTr("Apply Auto Presets to Selection"); tip: qsTr("Apply presets whose camera, lens or exposure rules match the selected photos."); enabled: backend.selectedCount > 0 && backend.presetRuleCount > 0 && engine.ready; onTriggered: { const n = backend.applyPresetRules(backend.selectedIds()); if (n === 0) backend.setStatus(qsTr("No selected photo matches an auto-apply rule")) } }
                MenuSep {}
                SMenuItem { text: qsTr("Import Presets…"); tip: qsTr("Import OmaRAW or XMP preset files. XMP conversions include a report; existing names are kept."); onTriggered: root.shell.importPresets() }
                SMenuItem { text: qsTr("Export Presets…"); tip: qsTr("Save your user presets, categories and tags to a file for backup or transfer."); onTriggered: root.shell.exportPresets() }
                SMenuItem { text: qsTr("Last Import Report…"); tip: qsTr("Review converted settings, untranslated adjustments and files that were skipped."); enabled: backend.presetImportReport.length > 0; onTriggered: root.shell.showPresetImportReport() }
            }
            SMenuItem { text: qsTr("Quick Snapshot"); tip: qsTr("Save the current edit as a snapshot for later comparison or restoration."); shortcut: "Ctrl+N"; enabled: root.shell.workspace === "Develop" && engine.imageId >= 0; onTriggered: root.shell.quickSnapshot() }
            MenuSep {}
            SMenuItem { text: qsTr("Reset Develop Settings"); tip: qsTr("Restore the import starting look and clear the development history, including redo steps. This reset cannot be undone."); enabled: engine.imageId >= 0 && engine.history.length > 0; onTriggered: engine.resetHistory() }
            SMenuItem { text: qsTr("Reset to Camera Original"); tip: qsTr("Restore the camera original and clear the development history, including redo steps. This reset cannot be undone."); enabled: engine.imageId >= 0 && engine.history.length > 0; onTriggered: engine.resetToOriginal() }
        }
        StyledMenu {
            objectName: "menuView"
            title: qsTr("View")
            tip: qsTr("Browse, compare and arrange the workspace.")
            SMenuItem { objectName: "viewGridAction"; text: qsTr("Grid"); tip: qsTr("Browse photos as a grid of thumbnails."); shortcut: "G"; checkable: true; checked: root.shell.libraryViewMode === "grid"; onTriggered: root.shell.showPhotos("grid") }
            SMenuItem { objectName: "viewLoupeAction"; text: qsTr("Loupe"); tip: qsTr("Show the current photo on its own for a closer look."); shortcut: "E"; checkable: true; checked: root.shell.libraryViewMode === "loupe"; onTriggered: root.shell.showPhotos("loupe") }
            SMenuItem { objectName: "viewCompareAction"; text: qsTr("Compare"); tip: qsTr("Show the current photo beside another selected photo, or the next one."); shortcut: "C"; checkable: true; checked: root.shell.libraryViewMode === "compare"; enabled: !root.shell.libraryBrowsing; onTriggered: root.shell.browserMode = "compare" }
            SMenuItem { objectName: "viewSurveyAction"; text: qsTr("Survey"); tip: qsTr("Show all selected photos together to choose the strongest frames."); shortcut: "N"; checkable: true; checked: root.shell.libraryViewMode === "survey"; enabled: !root.shell.libraryBrowsing; onTriggered: root.shell.browserMode = "survey" }
            StyledMenu {
                title: qsTr("Thumbnail Size")
                enabled: !root.shell.libraryBrowsing
                SMenuItem { text: qsTr("Larger Thumbnails"); tip: qsTr("Increase thumbnail size in the Library grid."); shortcut: "Ctrl++"; onTriggered: root.shell.cardSize = Math.min(Theme.szCardMax, root.shell.cardSize + 40) }
                SMenuItem { text: qsTr("Smaller Thumbnails"); tip: qsTr("Decrease thumbnail size to fit more photos in the Library grid."); shortcut: "Ctrl+-"; onTriggered: root.shell.cardSize = Math.max(Theme.szCardMin, root.shell.cardSize - 40) }
            }
            MenuSep {}
            SMenuItem { text: qsTr("Before / After"); tip: qsTr("Toggle the Develop comparison with the original processing."); shortcut: "\\"; enabled: root.shell.workspace === "Develop" && engine.imageId >= 0; onTriggered: root.shell.toggleOriginal() }
            StyledMenu {
                title: qsTr("Exposure Overlays")
                SMenuItem { text: qsTr("Clipping Indicators"); tip: qsTr("Show where highlights are blown or shadows are crushed in the displayed photo."); shortcut: "J"; checkable: true; checked: engine.clippingShown; enabled: engine.imageId >= 0; onTriggered: engine.clippingShown = !engine.clippingShown }
                SMenuItem { text: qsTr("False Colour"); tip: qsTr("Repaint the displayed photo by brightness to read exposure off it: green middle grey, pink light skin, yellow highlights."); shortcut: "F"; checkable: true; checked: engine.falseColour; enabled: engine.imageId >= 0; onTriggered: engine.toggleFalseColour() }
                SMenuItem { objectName: "rawClippingAction"; text: qsTr("Mark Clipped Sensor Areas"); tip: qsTr("Mark saturated sensor samples in magenta, before exposure or highlight recovery. RAW files only."); checkable: true; checked: engine.rawClippingShown; enabled: engine.imageId >= 0; onTriggered: engine.rawClippingShown = !engine.rawClippingShown }
            }
            SMenuItem { text: qsTr("Lights Out"); tip: qsTr("Hide surrounding panels in Develop to concentrate on the photograph."); shortcut: "Shift+L"; checkable: true; checked: root.shell.lightsOut; enabled: root.shell.workspace === "Develop"; onTriggered: root.shell.lightsOut = !root.shell.lightsOut }
            SMenuItem {
                objectName: "lightInterfaceAction"
                text: qsTr("Light Interface")
                tip: backend.colourCritical
                     ? qsTr("Colour Critical uses a neutral interface. Turn it off to use the light interface.")
                     : qsTr("Use light panels and dark text. Photographs keep a neutral surround.")
                checkable: true
                enabled: !backend.colourCritical
                checked: backend.lightInterface
                onTriggered: backend.lightInterface = !backend.lightInterface
            }
            SMenuItem { objectName: "colourCriticalAction"; text: qsTr("Colour Critical"); tip: qsTr("Use a neutral grey interface and a fixed dark grey photo surround."); checkable: true; checked: backend.colourCritical; onTriggered: backend.colourCritical = !backend.colourCritical }
            StyledMenu {
                title: qsTr("Viewer Background")
                enabled: !backend.colourCritical
                tip: backend.colourCritical ? qsTr("Colour Critical uses a fixed dark grey surround. Turn it off to restore your saved viewer background.") : qsTr("Choose a neutral surround behind the photograph.")
                SMenuItem { text: qsTr("Dark"); tip: qsTr("Use a dark neutral surround behind the photo."); checkable: true; checked: root.shell.viewerBackground === "dark"; onTriggered: root.shell.viewerBackground = "dark" }
                SMenuItem { text: qsTr("Black"); tip: qsTr("Use a black surround behind the photo."); checkable: true; checked: root.shell.viewerBackground === "black"; onTriggered: root.shell.viewerBackground = "black" }
                SMenuItem { text: qsTr("Mid Grey"); tip: qsTr("Use a middle-grey surround behind the photo."); checkable: true; checked: root.shell.viewerBackground === "grey"; onTriggered: root.shell.viewerBackground = "grey" }
                SMenuItem { text: qsTr("Light"); tip: qsTr("Use a light neutral surround behind the photo."); checkable: true; checked: root.shell.viewerBackground === "light"; onTriggered: root.shell.viewerBackground = "light" }
            }
            MenuSep {}
            StyledMenu {
                title: qsTr("Panels & Layout")
                tip: qsTr("Show, hide or reset the surrounding panels.")
                SMenuItem { text: qsTr("Sources"); tip: qsTr("Show or hide the source sidebar."); checkable: true; checked: root.shell.sourceDockVisible; onTriggered: root.shell.sourceDockVisible = !root.shell.sourceDockVisible }
                SMenuItem { text: qsTr("Inspector"); tip: qsTr("Show or hide the right-hand inspector."); checkable: true; checked: root.shell.inspectorVisible; onTriggered: root.shell.inspectorVisible = !root.shell.inspectorVisible }
                SMenuItem { text: qsTr("Filmstrip"); tip: qsTr("Show or hide the row of photos along the bottom."); checkable: true; checked: root.shell.filmstripVisible; onTriggered: root.shell.filmstripVisible = !root.shell.filmstripVisible }
                MenuSep {}
                SMenuItem { text: qsTr("Reset Layout"); tip: qsTr("Restore default panel sizes and visibility."); onTriggered: root.shell.resetLayout() }
            }
            StyledMenu {
                title: qsTr("Workspace")
                tip: qsTr("Switch between organising, editing, tethered capture and output.")
                SMenuItem { text: qsTr("Library"); tip: qsTr("Import, organise, rate and select photos."); shortcut: "Ctrl+1"; onTriggered: root.shell.workspace = "Library" }
                SMenuItem { text: qsTr("Develop"); tip: qsTr("Adjust the current photo without changing its original file."); shortcut: "Ctrl+2"; onTriggered: root.shell.workspace = "Develop" }
                SMenuItem { text: qsTr("Capture"); tip: qsTr("Open tethered shooting, camera settings and live view."); shortcut: "Ctrl+3"; onTriggered: root.shell.workspace = "Capture" }
                SMenuItem { text: qsTr("Output"); tip: qsTr("Prepare exports, contact sheets and prints."); shortcut: "Ctrl+4"; onTriggered: root.shell.workspace = "Output" }
            }
        }
        StyledMenu {
            objectName: "menuCamera"
            title: qsTr("Camera")
            tip: qsTr("Connect a camera, control live view and take a photo.")
            SMenuItem { text: qsTr("Connect Camera…"); tip: qsTr("Open Capture and scan for supported USB cameras."); enabled: capture.available; onTriggered: { root.shell.workspace = "Capture"; capture.refresh() } }
            SMenuItem { text: qsTr("Disconnect"); tip: qsTr("Release the connected camera so it can be unplugged or used elsewhere."); enabled: capture.connected; onTriggered: capture.disconnectCamera() }
            MenuSep {}
            SMenuItem { text: qsTr("Live View"); tip: qsTr("Start or stop the connected camera’s live preview."); checkable: true; checked: capture.liveView; enabled: capture.connected && capture.canPreview; onTriggered: capture.liveView = !capture.liveView }
            SMenuItem { text: qsTr("Capture"); tip: qsTr("Take and download a photo from the connected camera."); enabled: capture.connected && capture.canCapture; onTriggered: { root.shell.workspace = "Capture"; capture.capture() } }
            MenuSep {}
            SMenuItem { text: qsTr("Capture Workspace"); tip: qsTr("Open tethered shooting, camera settings and live view."); shortcut: "Ctrl+3"; onTriggered: root.shell.workspace = "Capture" }
        }
        StyledMenu {
            objectName: "menuHelp"
            title: qsTr("Help")
            tip: qsTr("Read the guide, search commands or find keyboard shortcuts.")
            SMenuItem { text: qsTr("OmaRAW Help"); tip: qsTr("Open the introduction and searchable section-by-section guide."); shortcut: "F1"; onTriggered: root.shell.help("start") }
            SMenuItem { text: qsTr("Help for This Workspace"); tip: qsTr("Open the guide at the current workspace."); onTriggered: root.shell.help() }
            SMenuItem { text: qsTr("The Written Guide"); tip: qsTr("Open the installed manual and help pages as files."); onTriggered: backend.revealDocumentation() }
            SMenuItem { text: qsTr("Command Search…"); tip: qsTr("Find a menu command by typing its name, then press Return to run it."); shortcut: "Ctrl+K"; onTriggered: root.shell.commandPalette.open() }
            SMenuItem { text: qsTr("Keyboard Shortcuts"); tip: qsTr("Show the shortcut reference for common actions."); shortcut: "?"; onTriggered: root.shell.shortcutsVisible = !root.shell.shortcutsVisible }
            MenuSep {}
            SMenuItem { text: qsTr("About OmaRAW"); tip: qsTr("Show the app version, credits and licence."); onTriggered: root.shell.about() }
        }
    }

    WorkspaceBar {
        id: workspaces
        objectName: "workspaceBar"
        x: root.twoRows ? (root.width - width) / 2
                       : Math.max(menuBar.x + menuBar.width + Theme.s3, (root.width - width) / 2)
        y: root.twoRows ? Theme.hTitleBar : 0
        height: Theme.hTitleBar
        onActivated: name => root.shell.workspace = name
    }

    Row {
        id: actions
        anchors.right: parent.right
        anchors.rightMargin: Theme.s2
        y: (Theme.hTitleBar - height) / 2
        spacing: Theme.s1
        IconButton {
            iconName: "undo-2"; text: qsTr("Undo develop step")
            shortcut: { backend.shortcutOverrides; return backend.shortcutHint("Ctrl+Z") }
            tip: qsTr("Return the current photo to the previous step in its Develop history.")
            enabled: root.shell.workspace === "Develop" && engine.imageId >= 0 && engine.historyEnd > 0
            onClicked: engine.undo()
            anchors.verticalCenter: parent.verticalCenter
        }
        IconButton {
            iconName: "redo-2"; text: qsTr("Redo develop step")
            shortcut: { backend.shortcutOverrides; return backend.shortcutHint("Ctrl+Shift+Z") }
            tip: qsTr("Reapply the next step in the current photo’s Develop history.")
            enabled: root.shell.workspace === "Develop" && engine.imageId >= 0 && engine.historyEnd < engine.history.length
            onClicked: engine.redo()
            anchors.verticalCenter: parent.verticalCenter
        }
        IconButton {
            iconName: "activity"
            text: qsTr("Background tasks")
            tip: backend.importing ? backend.importStatus
                 : engine.offlineBusy ? engine.offlineStatus
                 : engine.exporting ? qsTr("Exporting photos. Open Output to see the queue and its controls.")
                 : qsTr("No import, offline-copy or export job is running.")
            iconColor: backend.importing || engine.offlineBusy || engine.exporting ? Theme.accent : Theme.textSecondary
            anchors.verticalCenter: parent.verticalCenter
        }
        IconButton {
            iconName: "settings"; text: qsTr("Preferences")
            tip: qsTr("Change library, recovery, performance, accessibility and layout settings.")
            onClicked: root.shell.preferences()
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
