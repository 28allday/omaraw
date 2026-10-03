import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

// Top of the browser: import/export, search, sort, rating and label
// filters, view mode, thumbnail size. Every control drives backend state.
Rectangle {
    id: root
    property var shell: null
    implicitHeight: Theme.hToolbar * 2
    color: Theme.panelBg
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.hairline; color: Theme.border }

    // row 1
    Row {
        id: row1
        anchors.left: parent.left
        anchors.leftMargin: Theme.s3
        anchors.top: parent.top
        height: Theme.hToolbar
        spacing: Theme.s2
        ToolButton {
            anchors.verticalCenter: parent.verticalCenter
            iconName: "download"; text: qsTr("Import"); showLabel: true; shortcut: "Ctrl+I"
            tip: qsTr("Brings a folder or a card into the catalog.")
            onClicked: root.shell.browsePhotos("")
        }
        ToolButton {
            anchors.verticalCenter: parent.verticalCenter
            iconName: "upload"; text: qsTr("Export"); showLabel: true; shortcut: "Ctrl+Shift+E"
            tip: qsTr("Opens the Output page with the selection ready to write out.")
            onClicked: root.shell.workspace = "Output"
        }
        Tip { visible: false }
    }
    Row {
        anchors.right: parent.right
        anchors.rightMargin: Theme.s3
        anchors.top: parent.top
        height: Theme.hToolbar
        spacing: Theme.s2
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: backend.importing ? backend.importStatus : ""
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
        }
        ToolButton {
            anchors.verticalCenter: parent.verticalCenter
            visible: backend.importing
            iconName: "x"; text: qsTr("Cancel import"); tip: qsTr("Stops after the photo being read now; what is in already stays."); onClicked: backend.cancelImport()
        }
        ToolButton {
            anchors.verticalCenter: parent.verticalCenter
            visible: backend.fileJobBusy
            iconName: "x"; text: qsTr("Stop"); showLabel: true
            tip: qsTr("Stops the checksum or the search for offline photos; nothing from it is written to the catalog.")
            onClicked: backend.cancelFileJob()
        }
    }

    // row 2
    RowLayout {
        id: row2
        anchors.left: parent.left
        anchors.leftMargin: Theme.s3
        anchors.right: parent.right
        anchors.rightMargin: Theme.s3
        anchors.bottom: parent.bottom
        height: Theme.hToolbar
        spacing: Theme.s2
        ToolButton {
            iconName: "filter"; text: qsTr("Clear filters"); showLabel: false
            tip: qsTr("Shows everything in the source again.")
            enabled: backend.filterActive
            onClicked: { backend.filterText = ""; backend.filterRating = 0; backend.filterFlag = ""; backend.filterLabel = ""; backend.filterExtra = ({}) }
        }
        // More filters: type, edited state, place and camera.
        IconButton {
            id: moreButton
            iconName: "list-filter"; text: qsTr("More filters")
            tip: qsTr("File type, edited or not, GPS, camera, rating, colour label and flag.")
            checked: Object.keys(backend.filterExtra).length > 0 || morePopup.visible
            onClicked: morePopup.visible ? morePopup.close() : morePopup.open()
            C.Popup {
                id: morePopup
                y: moreButton.height + Theme.s1
                width: 280; padding: Theme.s3
                background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.border; radius: Theme.rMenu }
                function setExtra(key, value) { const e = Object.assign({}, backend.filterExtra); if (value) e[key] = value; else delete e[key]; backend.filterExtra = e }
                Column {
                    width: parent.width
                    spacing: Theme.s2
                    Text { text: qsTr("Type"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted }
                    SegmentedControl {
                        width: parent.width
                        labels: [qsTr("Any"), qsTr("RAW"), qsTr("Not RAW")]
                        tip: qsTr("Show only raw files, only processed ones, or both.")
                        readonly property var keys: ["", "raw", "other"]
                        currentIndex: Math.max(0, keys.indexOf(backend.filterExtra.type || ""))
                        onActivated: i => morePopup.setExtra("type", keys[i])
                    }
                    Text { text: qsTr("Edited"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted }
                    SegmentedControl {
                        width: parent.width
                        labels: [qsTr("Any"), qsTr("Edited"), qsTr("Untouched")]
                        tip: qsTr("Show only photos with develop edits, only those without, or both.")
                        readonly property var keys: ["", "yes", "no"]
                        currentIndex: Math.max(0, keys.indexOf(backend.filterExtra.edited || ""))
                        onActivated: i => morePopup.setExtra("edited", keys[i])
                    }
                    Text { text: qsTr("Place"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted }
                    SegmentedControl {
                        width: parent.width
                        labels: [qsTr("Any"), qsTr("With GPS"), qsTr("Without")]
                        tip: qsTr("Show only photos with a location, only those without, or both.")
                        readonly property var keys: ["", "yes", "no"]
                        currentIndex: Math.max(0, keys.indexOf(backend.filterExtra.gps || ""))
                        onActivated: i => morePopup.setExtra("gps", keys[i])
                    }
                    Text { text: qsTr("Camera"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted }
                    ComboField {
                        width: parent.width
                        readonly property var names: [qsTr("Any camera")].concat(backend.cameras)
                        model: names
                        tipTitle: qsTr("Camera"); tip: qsTr("Show only photos taken with one camera.")
                        currentIndex: Math.max(0, names.indexOf(backend.filterExtra.camera || ""))
                        onActivated: i => morePopup.setExtra("camera", i > 0 ? names[i] : "")
                    }
                    Text { text: qsTr("Minimum rating"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted }
                    RatingStars {
                        rating: backend.filterRating; interactive: true; size: 13
                        onColor: Theme.accent
                        onRated: r => backend.filterRating = r
                        Tip { visible: rh.hovered; text: backend.filterRating ? qsTr("%1 stars and up").arg(backend.filterRating) : qsTr("Filter by rating") }
                        HoverHandler { id: rh }
                    }
                    Text { text: qsTr("Colour label"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted }
                    LabelDots {
                        picker: true; size: 12
                        label: backend.filterLabel
                        onPicked: l => backend.filterLabel = backend.filterLabel === l ? "" : l
                    }
                    Text { text: qsTr("Flag"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted }
                    SegmentedControl {
                        id: flagSeg
                        icons: ["flag", "circle-dashed", "x"]
                        names: [qsTr("Picks only"), qsTr("Unflagged only"), qsTr("Rejected only")]
                        tip: qsTr("Click again to show every flag.")
                        readonly property var keys: ["pick", "unflagged", "reject"]
                        currentIndex: keys.indexOf(backend.filterFlag)
                        onActivated: idx => backend.filterFlag = backend.filterFlag === keys[idx] ? "" : keys[idx]
                    }
                }
            }
        }
        // Saved filters: named sets of search, rating, flag, label and sort.
        IconButton {
            id: savedButton
            iconName: "bookmark"; text: qsTr("Saved filters")
            tip: qsTr("Named sets of search, rating, flag, label and sort to bring back in one click.")
            checked: savedMenu.visible
            onClicked: savedMenu.visible ? savedMenu.close() : savedMenu.popup(savedButton, 0, savedButton.height)
            ContextMenu {
                id: savedMenu
                Instantiator {
                    model: backend.savedFilters
                    delegate: MenuAction {
                        required property var modelData
                        text: modelData.name
                        iconName: "bookmark"
                        onTriggered: backend.applySavedFilter(modelData.name)
                    }
                    onObjectAdded: (index, object) => savedMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => savedMenu.removeItem(object)
                }
                C.MenuSeparator { visible: backend.savedFilters.length > 0; contentItem: Rectangle { implicitHeight: 1; color: Theme.border } }
                MenuAction { text: qsTr("Save Current Filters…"); iconName: "plus"; enabled: backend.filterActive; onTriggered: saveFilterDialog.open() }
                MenuAction { text: qsTr("Forget a Saved Filter…"); iconName: "trash-2"; enabled: backend.savedFilters.length > 0; onTriggered: forgetFilterDialog.open() }
            }
        }
        SearchField {
            Layout.fillWidth: true; Layout.minimumWidth: 80
            placeholder: qsTr("Search %1").arg(backend.catalogName)
            tip: qsTr("Matches file names, titles, captions, keywords, camera and lens as you type.")
            text: backend.filterText
            onTextChanged: if (backend.filterText !== text) backend.filterText = text
            onCleared: backend.filterText = ""
        }
        Text {
            visible: root.width > 650
            text: qsTr("Sort")
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
        }
        ComboField {
            id: sortCombo
            Layout.preferredWidth: 130
            // Custom order is an album's own: it appears only while one is the source.
            readonly property var keys: backend.customOrderAvailable ? ["captured", "imported", "filename", "rating", "edited", "custom"] : ["captured", "imported", "filename", "rating", "edited"]
            model: backend.customOrderAvailable ? [qsTr("Capture Time"), qsTr("Import Time"), qsTr("Filename"), qsTr("Rating"), qsTr("Edit Time"), qsTr("Custom Order")]
                                                : [qsTr("Capture Time"), qsTr("Import Time"), qsTr("Filename"), qsTr("Rating"), qsTr("Edit Time")]
            tipTitle: qsTr("Sort by"); tip: qsTr("The order photos are shown in; Custom Order is an album's own, dragged into place.")
            currentIndex: Math.max(0, keys.indexOf(backend.sortKey))
            onActivated: idx => backend.sortKey = keys[idx]
        }
        IconButton {
            iconName: backend.sortDescending ? "arrow-down" : "arrow-up"
            text: backend.sortDescending ? qsTr("Descending") : qsTr("Ascending")
            tip: qsTr("Reverses the order.")
            onClicked: backend.sortDescending = !backend.sortDescending
        }

    }
    Row {
        id: rightTools
        anchors.right: parent.right
        anchors.rightMargin: Theme.s3
        anchors.top: parent.top
        height: Theme.hToolbar
        spacing: Theme.s2
        SegmentedControl {
            objectName: "libraryViewSelector"
            anchors.verticalCenter: parent.verticalCenter
            icons: ["layout-grid", "list", "image", "columns-2", "layout-dashboard"]
            names: [qsTr("Grid (G)"), qsTr("List (L)"), qsTr("Loupe (E)"), qsTr("Compare (C)"), qsTr("Survey (N)")]
            tips: [qsTr("Thumbnails in a grid."), qsTr("One row per photo with its details in columns."), qsTr("One photo large, for a close look."), qsTr("Two photos side by side, to choose between them."), qsTr("The selection laid out together, to cull a set.")]
            readonly property var modes: ["grid", "list", "loupe", "compare", "survey"]
            currentIndex: Math.max(0, modes.indexOf(root.shell.browserMode))
            onActivated: idx => root.shell.browserMode = modes[idx]
        }
        SliderField {
            visible: root.width > 620 && root.shell.browserMode === "grid"
            anchors.verticalCenter: parent.verticalCenter
            width: 150
            label: ""
            tipTitle: qsTr("Thumbnail size")
            tip: qsTr("How large the grid's thumbnails are.")
            labelWidth: 0
            from: Theme.szCardMin; to: Theme.szCardMax; stepSize: 8; decimals: 0
            value: root.shell.cardSize
            onEdited: v => root.shell.cardSize = v
        }
    }

    C.Popup {
        id: saveFilterDialog
        modal: true
        parent: C.Overlay.overlay
        anchors.centerIn: parent
        width: 360; padding: Theme.s4
        background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }
        onOpened: { filterNameField.text = ""; filterNameField.forceActiveFocus() }
        function save() { const n = filterNameField.text.trim(); if (n !== "") { backend.saveFilter(n); saveFilterDialog.close() } }
        Column {
            width: parent.width
            spacing: Theme.s3
            Text { text: qsTr("Save the current filters"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading; color: Theme.textPrimary }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: qsTr("Search, rating, flag, label and the sort order are kept under the name; a saved filter of the same name is replaced.")
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
            SearchField {
                id: filterNameField
                width: parent.width
                placeholder: qsTr("Filter name")
                tip: qsTr("The name the saved filter is listed by.")
                live: false
                onAccepted: t => saveFilterDialog.save()
            }
            Row {
                anchors.right: parent.right
                spacing: Theme.s2
                ToolButton { text: qsTr("Cancel"); showLabel: true; onClicked: saveFilterDialog.close() }
                ToolButton { text: qsTr("Save"); showLabel: true; enabled: filterNameField.text.trim() !== ""; onClicked: saveFilterDialog.save() }
            }
        }
    }
    C.Popup {
        id: forgetFilterDialog
        modal: true
        parent: C.Overlay.overlay
        anchors.centerIn: parent
        width: 360; padding: Theme.s4
        background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }
        Column {
            width: parent.width
            spacing: Theme.s3
            Text { text: qsTr("Forget a saved filter"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading; color: Theme.textPrimary }
            Repeater {
                model: backend.savedFilters
                Item {
                    required property var modelData
                    width: parent.width; height: Theme.hRow
                    Text {
                        anchors.left: parent.left; anchors.right: forgetButton.left; anchors.rightMargin: Theme.s2
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.name; elide: Text.ElideRight
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl; color: Theme.textPrimary
                    }
                    IconButton { id: forgetButton; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; iconName: "x"; text: qsTr("Forget %1").arg(parent.modelData.name); onClicked: backend.deleteSavedFilter(parent.modelData.name) }
                }
            }
            Row {
                anchors.right: parent.right
                ToolButton { text: qsTr("Done"); showLabel: true; onClicked: forgetFilterDialog.close() }
            }
        }
    }
}
