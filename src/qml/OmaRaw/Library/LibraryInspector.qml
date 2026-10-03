pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

// Right dock of the Library: histogram, keywords, metadata, camera, lens,
// copyright and GPS. Reads backend.info(); photo editing belongs in Develop.
Rectangle {
    id: root
    property var shell: null
    property var info: ({})
    AutoTagSettings { id: tagSettings }
    AutoTagPicker { id: tagPicker }
    property var histogram: []
    // What the selection shares, for batch edits ("Mixed" when it differs).
    property var summary: ({ count: 0 })
    color: Theme.panelBg

    function reload() {
        if (backend.currentId > 0) backend.reviewSidecar(backend.currentId)
        info = backend.info(backend.currentId)
        summary = backend.selectionSummary()
        histogram = backend.histogram(backend.currentId)
    }
    Component.onCompleted: reload()
    Connections {
        target: backend
        function onSelectionChanged() { root.reload() }
        function onAutoTagsChanged() { root.info = backend.info(backend.currentId) }
    }
    Rectangle { anchors.left: parent.left; width: Theme.hairline; height: parent.height; color: Theme.border }

    C.ScrollView {
        anchors.fill: parent
        anchors.leftMargin: Theme.hairline
        clip: true
        C.ScrollBar.vertical: ScrollBar {}
        C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
        contentWidth: availableWidth

        Column {
            width: parent.width
            spacing: 0

            InspectorGroup {
                title: qsTr("Histogram"); helpSection: "library"
                HistogramView {
                    x: Theme.s3; width: parent.width - Theme.s3 * 2
                    bins: root.histogram
                }
                Text {
                    x: Theme.s3
                    text: qsTr("From the embedded preview, not the RAW data.")
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                }
            }

            InspectorGroup {
                title: qsTr("Keywords"); helpSection: "organising"
                SearchField {
                    id: kwField
                    x: Theme.s3; width: parent.width - Theme.s3 * 2
                    placeholder: qsTr("Add keywords… (a/b for levels)")
                    tip: qsTr("Type a keyword and press Return; commas add several, a slash nests one under another, as in Places/Venice.")
                    live: false
                    enabled: backend.currentId > 0
                    // Suggestions for the word being typed (after the last comma), from names and synonyms.
                    readonly property string typing: text.split(",").pop().trim()
                    readonly property var suggestions: typing.length >= 2 && active ? backend.suggestKeywords(typing).filter(k => k.toLowerCase() !== typing.toLowerCase()) : []
                    function take(k) { const parts = text.split(","); parts.pop(); parts.push(k); backend.addKeyword(k); text = "" }
                    onAccepted: t => {
                        for (const k of t.split(",")) if (k.trim() !== "") backend.addKeyword(k.trim())
                        text = ""
                    }
                }
                Flow {
                    x: Theme.s3; width: parent.width - Theme.s3 * 2
                    spacing: Theme.s1
                    visible: kwField.suggestions.length > 0
                    Repeater {
                        model: kwField.suggestions
                        Rectangle {
                            required property string modelData
                            height: 20; width: sugText.implicitWidth + Theme.s2 * 2
                            radius: Theme.rControl
                            color: sugHover.hovered ? Theme.hovered(Theme.controlBg) : Theme.controlBg
                            border.width: Theme.hairline; border.color: Theme.accent
                            Text { id: sugText; anchors.centerIn: parent; text: parent.modelData; textFormat: Text.PlainText; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textPrimary }
                            HoverHandler { id: sugHover }
                            TapHandler { onTapped: kwField.take(parent.modelData) }
                        }
                    }
                }
                Flow {
                    x: Theme.s3; width: parent.width - Theme.s3 * 2
                    spacing: Theme.s1
                    Repeater {
                        model: root.info.keywords || []
                        Rectangle {
                            required property string modelData
                            height: 20; width: kwText.implicitWidth + 26
                            radius: Theme.rControl
                            color: Theme.controlBg
                            border.width: Theme.hairline; border.color: Theme.border
                            Text {
                                id: kwText
                                anchors.left: parent.left; anchors.leftMargin: Theme.s2
                                anchors.verticalCenter: parent.verticalCenter
                                text: parent.modelData
                                textFormat: Text.PlainText   // names and filenames are not markup
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textPrimary
                            }
                            Icon {
                                anchors.right: parent.right; anchors.rightMargin: Theme.s1
                                anchors.verticalCenter: parent.verticalCenter
                                name: "x"; size: 11; color: Theme.textMuted
                                TapHandler { onTapped: backend.removeKeyword(parent.parent.modelData) }
                            }
                        }
                    }
                }
                // Recently typed keywords, one click to add; those the photo already has are left out.
                Flow {
                    x: Theme.s3; width: parent.width - Theme.s3 * 2
                    spacing: Theme.s1
                    visible: backend.currentId > 0 && recentRepeater.count > 0
                    Text { text: qsTr("Recent:"); height: 20; verticalAlignment: Text.AlignVCenter; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted }
                    Repeater {
                        id: recentRepeater
                        model: backend.recentKeywords.filter(k => !(root.info.keywords || []).some(h => h.toLowerCase() === k.toLowerCase()))
                        Rectangle {
                            required property string modelData
                            height: 20; width: recentText.implicitWidth + Theme.s2 * 2
                            radius: Theme.rControl
                            color: recentHover.hovered ? Theme.hovered(Theme.controlBg) : "transparent"
                            border.width: Theme.hairline; border.color: Theme.border
                            Text {
                                id: recentText
                                anchors.centerIn: parent
                                text: parent.modelData
                                textFormat: Text.PlainText   // names and filenames are not markup
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
                            }
                            HoverHandler { id: recentHover }
                            TapHandler { onTapped: backend.addKeyword(parent.modelData) }
                        }
                    }
                }
                Text {
                    visible: (root.info.keywords || []).length === 0
                    x: Theme.s3
                    text: backend.currentId ? qsTr("No keywords. Type one and press Enter.") : qsTr("Select a photo.")
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                }
            }

            InspectorGroup {
                objectName: "automaticTags"
                title: qsTr("Automatic tags"); helpSection: "library"
                Column {
                    x: Theme.s3; width: parent.width - Theme.s3 * 2; spacing: Theme.s1
                    Text {
                        width: parent.width; wrapMode: Text.WordWrap; textFormat: Text.PlainText
                        text: !backend.autoTagEnabled ? qsTr("Scanning off · existing tags are kept") : backend.autoTagPaused && backend.autoTagPending > 0 ? qsTr("Paused · %1 waiting").arg(backend.autoTagPending)
                            + (backend.autoTagStatus ? "\n" + backend.autoTagStatus : "")
                            : backend.autoTagBusy ? backend.autoTagStatus : (root.info.autoTagScan || qsTr("Select photos to scan."))
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                    }
                    Toggle {
                        objectName: "autoTagMaster"
                        width: parent.width; label: qsTr("Automatic tagging")
                        checked: backend.autoTagEnabled; checkable: false
                        tip: qsTr("Tag new imports locally. Off stops scanning and keeps existing tags.")
                        onClicked: backend.autoTagEnabled = !backend.autoTagEnabled
                    }
                    Flow {
                        width: parent.width; spacing: Theme.s1
                        Repeater {
                            model: (root.info.autoTags || []).filter(t => t.active)
                            ToolButton {
                                required property var modelData
                                objectName: "autoTag_" + modelData.tag
                                text: modelData.label; iconName: "x"; showLabel: true
                                tip: qsTr("Remove %1 from this photo. This correction is remembered.").arg(modelData.label)
                                onClicked: backend.setCurrentAutoTag(modelData.tag, false)
                            }
                        }
                    }
                    Flow {
                        width: parent.width; spacing: Theme.s1
                        ToolButton { objectName: "correctAutoTags"; text: qsTr("Add / correct…"); showLabel: true; enabled: backend.currentId > 0; onClicked: tagPicker.open() }
                        ToolButton { objectName: "scanAutoTags"; text: qsTr("Scan selected"); showLabel: true; enabled: backend.autoTagEnabled && backend.selectedCount > 0; onClicked: backend.scanSelectionAutoTags() }
                        ToolButton { objectName: "pauseAutoTags"; visible: backend.autoTagEnabled && backend.autoTagPending > 0; text: backend.autoTagPaused ? qsTr("Resume") : qsTr("Pause"); showLabel: true; onClicked: backend.autoTagPaused = !backend.autoTagPaused }
                        ToolButton { objectName: "configureAutoTags"; text: qsTr("Settings…"); showLabel: true; onClicked: tagSettings.open() }
                    }
                    ToolButton {
                        text: qsTr("Use detections"); showLabel: true
                        visible: (root.info.autoTags || []).some(t => t.decision !== 0)
                        tip: qsTr("Clear your tag corrections for this photo and use the last scan again.")
                        onClicked: backend.resetCurrentAutoTagCorrections()
                    }
                    Text {
                        width: parent.width; wrapMode: Text.WordWrap
                        text: qsTr("Offline subject detection. Tags stay in this catalog, separate from your keywords.")
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                    }
                }
            }

            InspectorGroup {
                title: qsTr("Metadata"); helpSection: "organising"
                InspectorRow { label: qsTr("File Name"); value: root.info.filename || "" }
                InspectorRow { label: qsTr("Folder"); value: root.info.folder || "" }
                InspectorRow { label: qsTr("File Size"); value: root.info.size || ""; mono: true }
                InspectorRow { label: qsTr("File Type"); value: root.info.format || "" }
                InspectorRow { label: qsTr("Date Captured"); value: root.info.captured || ""; mono: true }
                InspectorRow { label: qsTr("Dimensions"); value: root.info.dimensions ? root.info.dimensions + "  " + root.info.megapixels : ""; mono: true }
                InspectorRow { label: qsTr("Bit Depth"); value: root.info.bitDepth || ""; mono: true }
                InspectorRow { label: qsTr("ISO"); value: root.info.iso ? String(root.info.iso).replace("ISO ", "") : ""; mono: true }
                InspectorRow { label: qsTr("Exposure"); value: root.info.exposure || ""; mono: true }
                InspectorRow {
                    label: qsTr("Variant")
                    value: root.info.variant ? qsTr("%1 of %2").arg(root.info.variantLabel).arg(root.info.filename)
                         : root.info.variantCount ? qsTr("Master · %1 variant%2").arg(root.info.variantCount).arg(root.info.variantCount === 1 ? "" : "s") : ""
                }
                InspectorRow {
                    label: qsTr("Stack")
                    value: root.info.stackCount > 1 ? (root.info.stackPos === 0 ? qsTr("Top of %1").arg(root.info.stackCount) : qsTr("%1 of %2").arg(root.info.stackPos + 1).arg(root.info.stackCount)) : ""
                }
                InspectorRow { label: qsTr("Sidecar"); value: root.info.sidecar || "" }
                // A sidecar edited by another program: what it says, field by field, and the two ways out.
                Column {
                    visible: root.info.sidecarChanged === true
                    x: Theme.s3; width: parent.width - Theme.s3 * 2
                    spacing: Theme.s1
                    readonly property var review: root.info.sidecarReview || ({})
                    readonly property var differs: review.differs || []
                    Repeater {
                        model: parent.differs
                        Item {
                            required property string modelData
                            width: parent.width; height: Theme.hRow
                            readonly property string theirs: modelData === "rating" ? qsTr("%1 stars").arg(parent.review.rating)
                                                            : modelData === "label" ? (parent.review.label || qsTr("no label"))
                                                            : modelData === "title" ? (parent.review.title || qsTr("no title"))
                                                            : modelData === "caption" ? (parent.review.caption || qsTr("no caption"))
                                                            : (parent.review.keywords || []).join(", ") || qsTr("no keywords")
                            Text {
                                anchors.left: parent.left; anchors.right: takeOne.left; anchors.rightMargin: Theme.s2
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("%1 in sidecar: %2").arg(modelData === "rating" ? qsTr("Rating") : modelData === "label" ? qsTr("Label") : modelData === "title" ? qsTr("Title") : modelData === "caption" ? qsTr("Caption") : qsTr("Keywords")).arg(theirs)
                                elide: Text.ElideRight
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
                            }
                            IconButton { id: takeOne; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; iconName: "corner-down-right"; text: qsTr("Take the sidecar's %1").arg(parent.modelData); onClicked: backend.applySidecar(backend.currentId, parent.modelData) }
                        }
                    }
                    Row {
                        spacing: Theme.s2
                        ToolButton { text: qsTr("Use sidecar"); showLabel: true; tip: qsTr("Takes every field from the XMP file another program wrote."); onClicked: backend.applySidecar(backend.currentId) }
                        ToolButton { text: qsTr("Keep catalog"); showLabel: true; tip: qsTr("Keeps the catalog's values and writes them back over the sidecar."); onClicked: backend.keepCatalogOverSidecar(backend.currentId) }
                    }
                }
                InspectorRow { label: qsTr("Status"); value: root.info.offline ? (root.info.offlineCopy ? qsTr("Original offline · working copy available") : root.info.smartPreview ? qsTr("Original offline · editing Smart Preview") : qsTr("Offline — file not found")) : ""; }
                InspectorRow { label: qsTr("Smart Preview"); value: root.info.smartPreview ? qsTr("%1 × %2 · %3 MB").arg(root.info.smartPreviewWidth).arg(root.info.smartPreviewHeight).arg((Number(root.info.smartPreviewBytes || 0) / 1048576).toFixed(1)) : root.info.smartPreviewStored ? qsTr("Preview missing or damaged") : ""; hideEmpty: true }
                InspectorRow { label: qsTr("Offline Copy"); value: root.info.offlineCopy ? qsTr("Full quality · %1 MB").arg((Number(root.info.offlineCopyBytes || 0) / 1048576).toFixed(1)) : root.info.offlineCopyStored ? qsTr("Copy missing or damaged") : ""; hideEmpty: true }
                Row {
                    visible: root.info.offline === true
                    x: Theme.s3; spacing: Theme.s2
                    ToolButton { iconName: "link"; text: qsTr("Locate…"); showLabel: true; tip: qsTr("Point at the file where it is now."); onClicked: root.shell.locateFile() }
                    ToolButton { iconName: "folder-open"; text: qsTr("Search a folder…"); showLabel: true; tip: qsTr("Looks through a folder for every offline photo by name and reconnects the ones it finds."); onClicked: root.shell.relinkOffline() }
                }
            }

            InspectorGroup {
                title: qsTr("Camera"); helpSection: "library"
                InspectorRow { label: qsTr("Camera"); value: root.info.camera || "" }
                InspectorRow { label: qsTr("Make"); value: root.info.make || "" }
                InspectorRow { label: qsTr("Model"); value: root.info.model || "" }
            }
            InspectorGroup {
                title: qsTr("Lens"); helpSection: "library"
                InspectorRow { label: qsTr("Lens"); value: root.info.lens || "" }
                InspectorRow { label: qsTr("Focal Length"); value: root.info.focal || ""; mono: true }
                InspectorRow { label: qsTr("Aperture"); value: root.info.aperture || ""; mono: true }
                InspectorRow { label: qsTr("Shutter"); value: root.info.shutter || ""; mono: true }
            }
            InspectorGroup {
                title: root.summary.count > 1 ? qsTr("Title & Caption · %1 selected").arg(root.summary.count) : qsTr("Title & Caption")
                helpSection: "organising"
                Item {
                    width: parent.width; height: Theme.hRow + Theme.s1
                    Text {
                        anchors.left: parent.left; anchors.leftMargin: Theme.s3; width: 104
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Title")
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                    }
                    SearchField {
                        objectName: "titleField"
                        anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 104
                        anchors.right: parent.right; anchors.rightMargin: Theme.s3
                        anchors.verticalCenter: parent.verticalCenter
                        placeholder: root.summary.titleMixed ? qsTr("Mixed") : qsTr("Title")
                        tip: qsTr("A title for the photo, written to its sidecar and into every export; with several selected it sets them all.")
                        text: root.summary.count > 1 ? (root.summary.title || "") : (root.info.title || "")
                        live: false
                        enabled: backend.currentId > 0
                        onAccepted: t => backend.setTitle(t)
                    }
                }
                Item {
                    width: parent.width; height: Theme.hRow + Theme.s1
                    Text {
                        anchors.left: parent.left; anchors.leftMargin: Theme.s3; width: 104
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Caption")
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                    }
                    SearchField {
                        anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 104
                        anchors.right: parent.right; anchors.rightMargin: Theme.s3
                        anchors.verticalCenter: parent.verticalCenter
                        placeholder: root.summary.captionMixed ? qsTr("Mixed") : qsTr("Caption")
                        tip: qsTr("A description of the photo, written to its sidecar and into every export.")
                        text: root.summary.count > 1 ? (root.summary.caption || "") : (root.info.caption || "")
                        live: false
                        enabled: backend.currentId > 0
                        onAccepted: t => backend.setCaption(t)
                    }
                }
                Text {
                    x: Theme.s3; width: parent.width - Theme.s3 * 2; wrapMode: Text.WordWrap
                    text: qsTr("Written to sidecars and into exported files as the XMP title and description, with the copyright and creator.")
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                }
            }
            InspectorGroup {
                title: root.summary.count > 1 ? qsTr("Copyright · %1 selected").arg(root.summary.count) : qsTr("Copyright")
                helpSection: "organising"
                Text {
                    x: Theme.s3; width: parent.width - Theme.s3 * 2; wrapMode: Text.WordWrap
                    visible: root.summary.count > 1
                    text: qsTr("Typing here applies to every selected photo. Mixed means they differ.")
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                }
                Item {
                    width: parent.width; height: Theme.hRow + Theme.s1
                    Text {
                        anchors.left: parent.left; anchors.leftMargin: Theme.s3; width: 104
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Copyright")
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                    }
                    SearchField {
                        objectName: "copyrightField"
                        anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 104
                        anchors.right: parent.right; anchors.rightMargin: Theme.s3
                        anchors.verticalCenter: parent.verticalCenter
                        placeholder: root.summary.copyrightMixed ? qsTr("Mixed") : qsTr("© Year Name")
                        tip: qsTr("The copyright notice carried by every export.")
                        text: root.summary.count > 1 ? (root.summary.copyright || "") : (root.info.copyright || "")
                        live: false
                        onAccepted: t => backend.setCopyright(t)
                    }
                }
                Item {
                    width: parent.width; height: Theme.hRow + Theme.s1
                    Text {
                        anchors.left: parent.left; anchors.leftMargin: Theme.s3; width: 104
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Creator")
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                    }
                    SearchField {
                        anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 104
                        anchors.right: parent.right; anchors.rightMargin: Theme.s3
                        anchors.verticalCenter: parent.verticalCenter
                        placeholder: root.summary.creatorMixed ? qsTr("Mixed") : qsTr("Photographer")
                        tip: qsTr("Who took the photo, carried by every export.")
                        text: root.summary.count > 1 ? (root.summary.creator || "") : (root.info.creator || "")
                        live: false
                        onAccepted: t => backend.setCreator(t)
                    }
                }
            }
            InspectorGroup {
                title: qsTr("GPS"); helpSection: "library"
                InspectorRow { label: qsTr("Latitude"); value: root.info.latitude || ""; mono: true }
                InspectorRow { label: qsTr("Longitude"); value: root.info.longitude || ""; mono: true }
                Row {
                    visible: root.info.hasGps || false
                    x: Theme.s3; spacing: Theme.s2
                    ToolButton { objectName: "openLocationInBrowser"; iconName: "external-link"; text: qsTr("Open location in browser"); showLabel: true; tip: qsTr("Opens OpenStreetMap in your browser and shares this photo’s coordinates with that site."); onClicked: backend.openInMap(backend.currentId) }
                }
                Text {
                    visible: !(root.info.hasGps || false)
                    x: Theme.s3
                    text: qsTr("No location data.")
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                }
            }
            Item { width: 1; height: Theme.s5 }
        }
    }
}
