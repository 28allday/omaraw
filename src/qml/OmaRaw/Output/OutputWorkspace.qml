pragma ComponentBehavior: Bound
import QtCore
import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui
import OmaRaw.Library
import OmaRaw.Develop

// Output — Phase 5. Left: presets and source. Centre: preview of the current
// render. Right: settings and the Export button. Bottom: the queue.
Item {
    id: root
    objectName: "outputWorkspace"
    readonly property var templateKeys: ["sourceMode", "maxEdge", "resizeMode", "copyOriginal", "copySidecar", "ppi",
        "outputProfile", "outputIcc", "outputIntent", "quality", "suffix", "namePattern", "folder", "format", "bpp",
        "metaExif", "metaLocation", "metaKeywords", "metaHistory", "sharpen", "sharpenAmount", "watermarkText", "watermarkImage",
        "watermarkAnchor", "watermarkSize", "watermarkOpacity", "sheetPaper", "sheetOrientation", "sheetColumns", "sheetRows",
        "sheetCaptions", "sheetTitle", "sheetIcc", "sheetIntent", "sheetBlackPoint"]
    function templateState() { const state = {}; for (const key of templateKeys) state[key] = root[key]; return state }
    function applyTemplate(state) { for (const key of templateKeys) if (state[key] !== undefined) root[key] = state[key]; presetIndex = -1; customPpi = false }
    property var shell: null
    property string sourceMode: "selection"   // selection | shown
    property int maxEdge: 2048
    // How the size is read: long edge (px), short edge, width, height, megapixels, percent, or none.
    property string resizeMode: "long"
    readonly property var resizeModes: ["long", "short", "width", "height", "megapixels", "percent", "none"]
    property bool copyOriginal: false
    property bool copySidecar: false
    property int ppi: 0
    readonly property var ppiValues: [0, 150, 240, 300, 360, 600]
    property bool customPpi: false
    property int outputProfile: 0
    property string outputIcc: ""
    property int outputIntent: 0
    // A format without a depth choice writes 8 bits.
    readonly property string outputProfileError: engine.outputProfileError(outputProfile, outputIcc, formatInfo.bpp ? bpp : 8)
    Settings {
        category: "outputColour"
        property alias profile: root.outputProfile
        property alias icc: root.outputIcc
        property alias intent: root.outputIntent
    }
    Settings {
        category: "outputExport"
        property alias ppi: root.ppi
        property alias format: root.format
        property alias bpp: root.bpp
        property alias maxEdge: root.maxEdge
        property alias resizeMode: root.resizeMode
        property alias quality: root.quality
        property alias suffix: root.suffix
        property alias sharpen: root.sharpen
        property alias sharpenAmount: root.sharpenAmount
        property alias presetIndex: root.presetIndex
    }
    FilePicker {
        id: outputIccDialog
        objectName: "outputIccDialog"
        title: qsTr("Choose an RGB output profile")
        nameFilters: [qsTr("ICC profiles (*.icc *.icm *.ICC *.ICM)")]
        onAccepted: root.outputIcc = selectedFile.toString()
    }
    readonly property var resizeArg: ({ mode: resizeMode, value: maxEdge, copyOriginal: copyOriginal, copySidecar: copySidecar, ppi: ppi,
                                      outputProfile: outputProfile, outputIcc: outputIcc, outputIntent: outputIntent })
    readonly property var destSpace: { folder; backend.catalogPath; return folder !== "" ? backend.folderSpace(folder) : ({}) }
    readonly property int offlineCount: sourceItems.filter(i => i.offline).length
    // A rough size per file for the free-space check: pixels × bytes per pixel for the format.
    readonly property real estimatedBytes: {
        const perPixel = format === "jpeg" ? 0.35 : format === "webp" ? 0.25 : format === "avif" || format === "jpegxl" ? 0.2 : (bpp === 16 ? 6 : 3)
        let total = 0
        for (const it of sourceItems) {
            const w = it.width || 6000, h = it.height || 4000
            let px = w * h
            if (resizeMode === "long" || resizeMode === "short" || resizeMode === "width" || resizeMode === "height") { const edge = Math.max(w, h); if (maxEdge > 0 && maxEdge < edge) px = px * (maxEdge / edge) * (maxEdge / edge) }
            else if (resizeMode === "megapixels" && maxEdge > 0) px = Math.min(px, maxEdge * 1e6)
            else if (resizeMode === "percent" && maxEdge > 0 && maxEdge < 100) px = px * (maxEdge / 100) * (maxEdge / 100)
            total += px * perPixel
        }
        return total
    }
    readonly property bool spaceShort: destSpace.bytesFree !== undefined && destSpace.bytesFree >= 0 && estimatedBytes > destSpace.bytesFree
    property int quality: 85
    property string suffix: "-web"
    // Empty: the original name plus the preset's suffix. Otherwise a pattern
    // of tokens ({name} {seq} {date} {time} {camera} {rating} {folder} {tag});
    // {suffix} stands for the preset's suffix.
    property string namePattern: ""
    readonly property string nameArg: namePattern.trim() !== "" ? namePattern.replace(/\{suffix\}/g, suffix) : suffix
    readonly property string namePreview: sourceItems.length > 0 ? engine.exportNamePreview(nameArg, sourceItems[0]) : qsTr("<original>") + suffix
    property string folder: ""
    property string format: "jpeg"
    property bool metaExif: true
    property bool metaLocation: false
    property bool metaKeywords: true
    property bool metaHistory: false
    property int bpp: 8
    property var formats: engine.exportFormats()
    readonly property var formatInfo: formats.find(f => f.id === format) || ({ id: "jpeg", label: "JPEG", ext: "jpg", quality: true, bpp: false })
    readonly property var presets: [
        { id: "web", name: qsTr("Web JPEG"), detail: qsTr("2048 px long edge · quality 85 · sRGB"), maxEdge: 2048, resizeMode: "long", quality: 85, suffix: "-web", format: "jpeg", bpp: 8, ppi: 0, outputProfile: 0, outputIntent: 0, sharpen: 0, sharpenAmount: 1 },
        { id: "full", name: qsTr("Full-size JPEG"), detail: qsTr("No resize · quality 92 · sRGB"), maxEdge: 0, resizeMode: "none", quality: 92, suffix: "", format: "jpeg", bpp: 8, ppi: 0, outputProfile: 0, outputIntent: 0, sharpen: 0, sharpenAmount: 1 },
        { id: "print4000", name: qsTr("Print JPEG · 4000 px"), detail: qsTr("4000 px long edge · quality 95 · sRGB · 300 ppi"), maxEdge: 4000, resizeMode: "long", quality: 95, suffix: "-print", format: "jpeg", bpp: 8, ppi: 300, outputProfile: 0, outputIntent: 0, sharpen: 0, sharpenAmount: 1 },
        { id: "proof", name: qsTr("Proof JPEG"), detail: qsTr("1200 px long edge · quality 80 · sRGB"), maxEdge: 1200, resizeMode: "long", quality: 80, suffix: "-proof", format: "jpeg", bpp: 8, ppi: 0, outputProfile: 0, outputIntent: 0, sharpen: 0, sharpenAmount: 1 },
        { id: "printFull", name: qsTr("Print JPEG · full size"), detail: qsTr("No resize · quality 95 · sRGB · 300 ppi"), maxEdge: 0, resizeMode: "none", quality: 95, suffix: "-print", format: "jpeg", bpp: 8, ppi: 300, outputProfile: 0, outputIntent: 0, sharpen: 0, sharpenAmount: 1 },
        { id: "master", name: qsTr("Editing master · TIFF"), detail: qsTr("No resize · 16 bit · linear ProPhoto RGB · 300 ppi"), maxEdge: 0, resizeMode: "none", quality: 100, suffix: "-master", format: "tiff", bpp: 16, ppi: 300, outputProfile: 2, outputIntent: 0, sharpen: 0, sharpenAmount: 1 }
    ]
    property int presetIndex: 0
    readonly property var presetKeys: ["maxEdge", "resizeMode", "quality", "suffix", "format", "bpp", "ppi", "outputProfile", "outputIntent", "sharpen", "sharpenAmount"]
    readonly property bool presetMatches: presetIndex >= 0 && presetIndex < presets.length
        && presetKeys.every(key => root[key] === presets[presetIndex][key])
    // Finishing: output sharpening and a watermark, applied to the written file.
    property int sharpen: 0          // 0 off, 1 screen, 2 print
    property int sharpenAmount: 1    // 0 low, 1 standard, 2 high
    property string watermarkText: ""
    property string watermarkImage: ""
    property int watermarkAnchor: 8
    property real watermarkSize: 3
    property real watermarkOpacity: 0.6
    readonly property var finishing: ({ sharpen: sharpen, amount: sharpenAmount, text: watermarkText, imagePath: watermarkImage,
                                        anchor: watermarkAnchor, size: watermarkSize, opacity: watermarkOpacity, margin: 2 })
    // Print & contact sheet (PDF)
    property string sheetPaper: "A4"
    property string sheetOrientation: "auto"
    property int sheetColumns: 4
    property int sheetRows: 3
    property bool sheetCaptions: true
    property string sheetTitle: ""
    // Application-managed colour for the print layout: an RGB paper profile,
    // an intent and black point compensation; "" keeps PDF pages in sRGB.
    property string sheetIcc: ""
    property int sheetIntent: 1
    property bool sheetBlackPoint: true
    property bool sheetBusy: false
    property int printSerial: 0
    property string lastSheet: ""
    readonly property var sheetOptions: ({ paper: sheetPaper, orientation: sheetOrientation, columns: sheetColumns, rows: sheetRows,
                                           captions: sheetCaptions, title: sheetTitle, margin: 10, dpi: 300,
                                           icc: sheetIcc, intent: sheetIntent, blackPoint: sheetBlackPoint })
    function sheetPath(kind) {
        const stamp = new Date().toISOString().replace(/[:T.]/g, "-").replace(/Z$/, "")
        return root.folder + "/" + kind + "-" + stamp + ".pdf"
    }
    function makeContactSheet() {
        const path = sheetPath("contact-sheet")
        if (backend.makeContactSheet(root.sourceMode === "shown", path, root.sheetOptions) === "") root.lastSheet = path
    }
    // Print: full renders first (through the queue), then the pages.
    function makePrintSheet() {
        if (root.sheetBusy || engine.exporting || !engine.ready || root.sourceCount === 0) return
        root.sheetBusy = true
        printWatch.job = Date.now().toString() + "-" + (++root.printSerial)
        printWatch.expected = root.sourceCount
        printWatch.options = Object.assign({}, root.sheetOptions)
        printWatch.path = root.sheetPath("print")
        printWatch.enabled = true
        printWatch.submitting = true
        // 16-bit linear ProPhoto, converted into the paper profile from there:
        // an 8-bit sRGB intermediate clipped colours the paper can print and
        // the soft proof shows.
        engine.exportBatchItems(root.sourceItems, root.folder + "/.omaraw-print", backend.sheetLongEdge(printWatch.options), 95, "-print", "tiff", 16,
                                engine.metaFlagsFor(true, false, true, false), {}, { outputProfile: 2, outputIntent: 0, printJob: printWatch.job })
        printWatch.submitting = false
        // Validation can reject the batch before any queue rows are added.
        if (printRows().length === 0) { printWatch.enabled = false; root.sheetBusy = false; return }
        finishPrintSheet()
    }
    function printRows() {
        return engine.exportQueue.filter(r => r.options && r.options.printJob === printWatch.job)
    }
    function finishPrintSheet() {
        if (!printWatch.enabled || printWatch.submitting) return
        const rows = printRows()
        if (rows.some(r => r.status === "queued" || r.status === "rendering")) return
        printWatch.enabled = false
        root.sheetBusy = false
        const files = rows.filter(r => r.status === "done").map(r => r.out)
        if (files.length !== printWatch.expected) {
            backend.setStatus(qsTr("Print layout stopped: %1 of %2 photos rendered.").arg(files.length).arg(printWatch.expected))
            return
        }
        if (backend.makePrintSheet(files, printWatch.path, printWatch.options) === "") root.lastSheet = printWatch.path
    }
    Connections {
        id: printWatch
        target: engine
        enabled: false
        property string job: ""
        property int expected: 0
        property var options: ({})
        property string path: ""
        property bool submitting: false
        // Let the engine finish updating its state/status before laying out
        // pages, so a print failure is not replaced by "Export finished".
        function onExportQueueChanged() { Qt.callLater(root.finishPrintSheet) }
    }
    readonly property bool finishingActive: sharpen > 0 || watermarkText.trim() !== "" || watermarkImage !== ""
    readonly property bool finishingWritable: ["jpeg", "tiff", "png", "webp"].indexOf(format) >= 0
    // [{path, variant, name}] — a variant exports as its own file.
    property var sourceItems: []
    readonly property int sourceCount: sourceItems.length
    function refreshSource() { sourceItems = backend.exportItems(sourceMode === "shown") }
    onSourceModeChanged: refreshSource()

    function applyPreset(i) {
        if (i < 0 || i >= presets.length) return
        const p = presets[i]
        for (const key of presetKeys) root[key] = p[key]
        customPpi = false
        presetIndex = i
    }
    Component.onCompleted: {
        refreshSource()
        if (folder === "") folder = backend.localFile(StandardPaths.writableLocation(StandardPaths.PicturesLocation).toString()) + "/OmaRAW Export"
    }
    function loadCurrent() { if (backend.currentId > 0 && visible) { const info = backend.info(backend.currentId); engine.load(info.path, info.variant) } }
    onVisibleChanged: loadCurrent()
    Connections {
        target: engine
        // The list arrives once the engine has probed its modules.
        function onExportQueueChanged() { if (root.formats.length === 0) root.formats = engine.exportFormats() }
        function onAvailableChanged() { root.formats = engine.exportFormats() }
    }
    Connections {
        target: backend
        function onSelectionChanged() { root.loadCurrent(); root.refreshSource() }
        function onFilterChanged() { root.refreshSource() }
        function onSourceChanged() { root.refreshSource() }
    }

    FolderPicker {
        id: folderDialog
        objectName: "outputFolderDialog"
        title: qsTr("Export destination")
        description: qsTr("Choose where your exported photos will be saved.")
        onAccepted: root.folder = backend.localFile(selectedFolder.toString())
    }
    FilePicker {
        id: paperIccDialog
        objectName: "paperProfileDialog"
        title: qsTr("Paper or printer profile (RGB)")
        nameFilters: [qsTr("ICC profiles (*.icc *.icm *.ICC *.ICM)")]
        onAccepted: root.sheetIcc = backend.localFile(selectedFile.toString())
    }
    FilePicker {
        id: markDialog
        objectName: "watermarkFileDialog"
        title: qsTr("Watermark image")
        nameFilters: [qsTr("Images (*.png *.svg *.jpg *.jpeg *.webp)")]
        onAccepted: root.watermarkImage = backend.localFile(selectedFile.toString())
    }

    // Label above the control, hint underneath. A side caption in this
    // inspector leaves the field too narrow to read.
    component Field: Column {
        id: field
        property string label: ""
        property string hint: ""
        property color hintColor: Theme.textMuted
        default property alias controls: body.data
        width: parent ? parent.width : implicitWidth
        spacing: Theme.s2
        topPadding: Theme.s2
        bottomPadding: Theme.s1
        leftPadding: Theme.s3
        rightPadding: Theme.s3
        readonly property int innerWidth: Math.max(0, width - leftPadding - rightPadding)
        Text {
            width: field.innerWidth
            visible: field.label !== ""
            text: field.label
            textFormat: Text.PlainText
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsLabel
            font.weight: Theme.wHeading
            color: Theme.textSecondary
        }
        Column {
            id: body
            width: field.innerWidth
            spacing: Theme.s2
        }
        Text {
            width: field.innerWidth
            visible: field.hint !== ""
            text: field.hint
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsLabel
            color: field.hintColor
        }
    }

    Column {
        anchors.fill: parent
        spacing: 0
        Item {
            width: parent.width
            height: parent.height - queue.height
            Rectangle {
                id: left
                anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: root.shell.sourceDockWidth
                color: Theme.panelBg
                Rectangle { anchors.right: parent.right; width: Theme.hairline; height: parent.height; color: Theme.border }
                C.ScrollView {
                  id: leftScroll
                  anchors.fill: parent; anchors.rightMargin: Theme.hairline
                  contentWidth: availableWidth
                  clip: true
                  Column {
                    width: leftScroll.availableWidth
                    spacing: 0
                    SectionHeader { title: qsTr("Source"); helpSection: "output" }
                    SourceRow { iconName: "square-check-big"; name: qsTr("Selected photos"); count: backend.selectedCount || (backend.currentId ? 1 : 0); current: root.sourceMode === "selection"; rowHeight: Theme.hRow + Theme.s2; onClicked: root.sourceMode = "selection" }
                    SourceRow { iconName: "images"; name: qsTr("All photos shown"); count: assets.count; current: root.sourceMode === "shown"; rowHeight: Theme.hRow + Theme.s2; onClicked: root.sourceMode = "shown" }
                    Item { width: 1; height: Theme.s3 }
                    SectionHeader { title: qsTr("Export presets"); helpSection: "output" }
                    Repeater {
                        model: root.presets
                        SourceRow {
                            required property int index
                            required property var modelData
                            objectName: "exportPreset_" + modelData.id
                            iconName: "file"; name: modelData.name
                            rowHeight: Theme.hRow + Theme.s2
                            current: root.presetIndex === index && root.presetMatches
                            onClicked: root.applyPreset(index)
                        }
                    }
                    Text {
                        width: parent.width
                        leftPadding: Theme.s3
                        rightPadding: Theme.s3
                        topPadding: Theme.s2
                        wrapMode: Text.WordWrap
                        text: root.presetMatches ? root.presets[root.presetIndex].detail : qsTr("Custom export settings")
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                    }
                    Item { width: 1; height: Theme.s3 }
                    Item {
                        width: parent.width
                        height: templates.implicitHeight
                        SavedSettings {
                            id: templates
                            x: Theme.s3
                            width: parent.width - Theme.s3 * 2
                            caption: qsTr("Export & print templates")
                            entries: backend.outputTemplates
                            onChosen: values => root.applyTemplate(values)
                            onSaveRequested: name => backend.saveWorkflowPreset("output", name, root.templateState())
                            onRemoveRequested: name => backend.deleteWorkflowPreset("output", name)
                        }
                    }
                    Item { width: 1; height: Theme.s3 }
                    SectionHeader { title: qsTr("PDF layouts"); helpSection: "print" }
                    Text {
                        width: parent.width
                        leftPadding: Theme.s3
                        rightPadding: Theme.s3
                        bottomPadding: Theme.s3
                        wrapMode: Text.WordWrap
                        text: root.lastSheet !== "" ? qsTr("Last PDF: %1").arg(root.lastSheet.split("/").pop()) : qsTr("PDF layouts of the source above; set up on the right.")
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                    }
                  }
                }
            }
            DevelopViewer {
                id: preview
                anchors.left: left.right; anchors.right: right.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                // Watermark preview: the same placement rule as the file gets,
                // scaled to the render on screen.
                Item {
                    id: markOverlay
                    x: preview.imageRect.x; y: preview.imageRect.y
                    width: preview.imageRect.width; height: preview.imageRect.height
                    visible: preview.showingRender && (root.watermarkText.trim() !== "" || root.watermarkImage !== "")
                    clip: true
                    readonly property real edge: Math.max(width, height)
                    readonly property real margin: edge * 0.02
                    readonly property int col: root.watermarkAnchor % 3
                    readonly property int row: Math.floor(root.watermarkAnchor / 3)
                    function place(item, w, h) {
                        item.x = markOverlay.col === 0 ? markOverlay.margin : markOverlay.col === 1 ? (markOverlay.width - w) / 2 : markOverlay.width - markOverlay.margin - w
                        item.y = markOverlay.row === 0 ? markOverlay.margin : markOverlay.row === 1 ? (markOverlay.height - h) / 2 : markOverlay.height - markOverlay.margin - h
                    }
                    Image {
                        id: markImage
                        visible: root.watermarkImage !== ""
                        source: backend.fileUrl(root.watermarkImage)
                        width: markOverlay.edge * root.watermarkSize / 100 * 4
                        height: implicitHeight > 0 ? width * implicitHeight / implicitWidth : 0
                        opacity: root.watermarkOpacity
                        fillMode: Image.PreserveAspectFit; smooth: true; mipmap: true
                        onWidthChanged: markOverlay.place(markImage, width, height)
                        onHeightChanged: markOverlay.place(markImage, width, height)
                    }
                    Text {
                        id: markText
                        visible: root.watermarkText.trim() !== ""
                        text: root.watermarkText.trim()
                        font.pixelSize: Math.max(6, markOverlay.edge * root.watermarkSize / 100)
                        font.weight: Font.DemiBold
                        color: "white"; opacity: root.watermarkOpacity
                        style: Text.Outline; styleColor: Qt.rgba(0, 0, 0, 0.55)
                        onWidthChanged: markOverlay.place(markText, width, height)
                        onHeightChanged: markOverlay.place(markText, width, height)
                    }
                    Connections {
                        target: markOverlay
                        function onWidthChanged() { markOverlay.place(markText, markText.width, markText.height); markOverlay.place(markImage, markImage.width, markImage.height) }
                        function onHeightChanged() { markOverlay.place(markText, markText.width, markText.height); markOverlay.place(markImage, markImage.width, markImage.height) }
                    }
                    Connections {
                        target: root
                        function onWatermarkAnchorChanged() { markOverlay.place(markText, markText.width, markText.height); markOverlay.place(markImage, markImage.width, markImage.height) }
                    }
                }
            }
            Rectangle {
                id: right
                anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: root.shell.inspectorWidth
                color: Theme.panelBg
                Rectangle { anchors.left: parent.left; width: Theme.hairline; height: parent.height; color: Theme.border }
                C.ScrollView {
                    id: settingsScroll
                    objectName: "outputSettingsScroll"
                    anchors.fill: parent; anchors.leftMargin: Theme.hairline
                    contentWidth: availableWidth
                    C.ScrollBar.vertical: ScrollBar {}
                    C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
                    clip: true
                  Column {
                    width: settingsScroll.availableWidth
                    spacing: Theme.s2
                    InspectorGroup {
                        title: qsTr("Export settings"); helpSection: "output"
                        Field {
                            label: qsTr("Format")
                            ComboField {
                                width: parent.width
                                model: root.formats.map(f => f.label)
                                tipTitle: qsTr("Format"); tip: qsTr("The file type written; JPEG for sharing, TIFF for archive, PSD for a layered editor.")
                                currentIndex: Math.max(0, root.formats.findIndex(f => f.id === root.format))
                                onActivated: i => root.format = root.formats[i].id
                            }
                            SegmentedControl {
                                visible: root.formatInfo.bpp
                                width: parent.width
                                fill: true
                                labels: ["8 bit", "16 bit"]
                                tips: [qsTr("Standard depth; half the size."), qsTr("Keeps the engine's full precision for further editing.")]
                                currentIndex: root.bpp === 16 ? 1 : 0
                                onActivated: i => root.bpp = i === 1 ? 16 : 8
                            }
                        }
                        Field {
                            label: qsTr("Print PPI")
                            hint: root.ppi === 0 ? qsTr("No print-resolution override. Pixel size and JPEG quality are set separately.") : qsTr("%1 pixels per printed inch. This tag does not resize the image.").arg(root.ppi)
                            Row {
                                width: parent.width
                                spacing: Theme.s2
                                ComboField {
                                    id: ppiChoice
                                    objectName: "outputPpiChoice"
                                    width: parent.width - (ppiNumber.visible ? ppiNumber.width + parent.spacing : 0)
                                    model: [qsTr("No override"), "150 ppi", "240 ppi", "300 ppi", "360 ppi", "600 ppi", qsTr("Custom…")]
                                    currentIndex: root.customPpi || root.ppiValues.indexOf(root.ppi) < 0 ? root.ppiValues.length : root.ppiValues.indexOf(root.ppi)
                                    tipTitle: qsTr("Print PPI")
                                    tip: qsTr("Sets the print-size tag without changing pixel dimensions or JPEG quality. No override keeps the resolution supplied by the source or encoder.")
                                    onActivated: i => {
                                        root.customPpi = i === root.ppiValues.length
                                        root.ppi = root.customPpi ? (root.ppi > 0 ? root.ppi : 300) : root.ppiValues[i]
                                    }
                                }
                                ValueField {
                                    id: ppiNumber
                                    objectName: "outputPpiNumber"
                                    visible: ppiChoice.currentIndex === root.ppiValues.length
                                    from: 1; to: 2400; decimals: 0; snap: 1; step: 1
                                    label: qsTr("Custom print PPI")
                                    onEdited: v => root.ppi = v
                                }
                                Binding { target: ppiNumber; property: "value"; value: root.ppi }
                            }
                        }
                        Field {
                            label: qsTr("Colour profile")
                            hint: root.outputProfileError
                            hintColor: Theme.warning
                            ComboField {
                                width: parent.width
                                model: [qsTr("sRGB"), qsTr("RGB (1998)"), qsTr("Linear ProPhoto RGB (16-bit files)"), qsTr("Custom RGB ICC…")]
                                tipTitle: qsTr("Colour profile"); tip: qsTr("The colour space the file is written in and tagged with; sRGB for the web and most screens.")
                                currentIndex: root.outputProfile
                                onActivated: i => { root.outputProfile = i; if (i === 3 && root.outputIcc === "") outputIccDialog.open() }
                            }
                            Row {
                                visible: root.outputProfile === 3
                                width: parent.width
                                spacing: Theme.s2
                                Text {
                                    width: parent.width - profileButton.width - parent.spacing
                                    height: Theme.hControl
                                    text: root.outputIcc ? engine.proofProfileNameOf(root.outputIcc) : qsTr("Choose a profile…")
                                    elide: Text.ElideMiddle
                                    verticalAlignment: Text.AlignVCenter
                                    font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
                                }
                                IconButton {
                                    id: profileButton
                                    iconName: "folder-open"
                                    text: qsTr("Choose output ICC profile")
                                    tip: qsTr("Any RGB profile file, such as a lab's printing profile.")
                                    onClicked: outputIccDialog.open()
                                }
                            }
                            ComboField {
                                width: parent.width
                                visible: root.outputProfile === 3
                                model: [qsTr("Perceptual"), qsTr("Relative colorimetric"), qsTr("Saturation"), qsTr("Absolute colorimetric")]
                                tipTitle: qsTr("Rendering intent"); tip: qsTr("How colours outside the profile are brought in; perceptual for photos.")
                                currentIndex: root.outputIntent
                                onActivated: i => root.outputIntent = i
                            }
                        }
                        Field {
                            label: qsTr("Resize by")
                            hint: root.resizeMode === "none" ? qsTr("Full size, as developed") : ""
                            ComboField {
                                width: parent.width
                                model: [qsTr("Long edge"), qsTr("Short edge"), qsTr("Width"), qsTr("Height"), qsTr("Megapixels"), qsTr("Percent"), qsTr("No resize")]
                                tipTitle: qsTr("Resize"); tip: qsTr("Which measure the size below applies to; photos are never enlarged.")
                                currentIndex: Math.max(0, root.resizeModes.indexOf(root.resizeMode))
                                onActivated: i => {
                                    const was = root.resizeMode; root.resizeMode = root.resizeModes[i]
                                    if (root.resizeMode === "megapixels" && was !== "megapixels") root.maxEdge = 12
                                    else if (root.resizeMode === "percent" && was !== "percent") root.maxEdge = 50
                                    else if ((was === "megapixels" || was === "percent") && root.resizeMode !== "megapixels" && root.resizeMode !== "percent") root.maxEdge = 2048
                                }
                            }
                        }
                        Field {
                            visible: root.resizeMode !== "none"
                            hint: root.maxEdge === 0 ? qsTr("0 = full size")
                                : root.resizeMode === "long" ? qsTr("%1 px on the long edge").arg(root.maxEdge)
                                : root.resizeMode === "short" ? qsTr("%1 px on the short edge").arg(root.maxEdge)
                                : root.resizeMode === "width" ? qsTr("%1 px wide").arg(root.maxEdge)
                                : root.resizeMode === "height" ? qsTr("%1 px tall").arg(root.maxEdge)
                                : root.resizeMode === "megapixels" ? qsTr("At most %1 megapixels, never upsized").arg(root.maxEdge)
                                : qsTr("%1 % of the developed size").arg(root.maxEdge)
                            SliderField {
                                width: parent.width
                                stacked: true
                                label: root.resizeMode === "megapixels" ? qsTr("Megapixels") : root.resizeMode === "percent" ? qsTr("Percent") : qsTr("Pixels")
                                tip: qsTr("The size limit; 0 means no limit.")
                                from: 0; to: root.resizeMode === "megapixels" ? 200 : root.resizeMode === "percent" ? 100 : 8000
                                stepSize: root.resizeMode === "megapixels" ? 1 : root.resizeMode === "percent" ? 5 : 100; decimals: 0
                                value: root.maxEdge
                                onEdited: v => root.maxEdge = v
                                onEditingFinished: v => root.maxEdge = v
                            }
                        }
                        Field {
                            visible: root.formatInfo.quality
                            SliderField {
                                width: parent.width
                                stacked: true
                                label: qsTr("Quality"); from: 1; to: 100; stepSize: 1; decimals: 0
                                tip: qsTr("Compression quality: higher is a larger, cleaner file; 85 to 92 suits most uses.")
                                value: root.quality
                                onEdited: v => root.quality = v
                                onEditingFinished: v => root.quality = v
                            }
                        }
                    }
                    InspectorGroup {
                        title: qsTr("Destination"); helpSection: "output"
                        Field {
                            label: qsTr("Folder")
                            Text {
                                id: folderPath
                                width: parent.width
                                height: Theme.hControl
                                text: root.folder
                                textFormat: Text.PlainText
                                elide: Text.ElideMiddle
                                verticalAlignment: Text.AlignVCenter
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
                                color: root.folder === "" ? Theme.textMuted : Theme.textPrimary
                                HoverHandler { id: folderHover }
                                Tooltip { text: qsTr("Folder"); description: folderPath.text; visible: folderHover.hovered && folderPath.truncated }
                            }
                            ToolButton { iconName: "folder-open"; text: qsTr("Choose folder…"); showLabel: true; tip: qsTr("Where the files are written."); onClicked: folderDialog.openFor(root.folder) }
                        }
                        Field {
                            label: qsTr("Filename pattern")
                            hint: qsTr("Tokens: {name} {seq} {date} {time} {camera} {rating} {folder} {tag} {suffix}. Empty keeps the original name plus the preset's suffix.")
                            SearchField {
                                width: parent.width
                                glyph: "type"
                                placeholder: qsTr("{date}-{name}{suffix}")
                                tip: qsTr("How files are named: {name} the original name, {date} the capture date, {seq} a counter, {suffix} the preset suffix. The line under it shows the first result.")
                                live: false
                                text: root.namePattern
                                onAccepted: t => root.namePattern = t
                            }
                            Text {
                                id: nameLine
                                width: parent.width
                                text: root.namePreview + "." + root.formatInfo.ext
                                textFormat: Text.PlainText
                                elide: Text.ElideMiddle
                                font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel
                                color: Theme.textPrimary
                                HoverHandler { id: nameHover }
                                Tooltip { text: qsTr("Filename"); description: nameLine.text; visible: nameHover.hovered && nameLine.truncated }
                            }
                        }
                        Column {
                            width: parent.width
                            spacing: Theme.s2
                            topPadding: Theme.s2
                            bottomPadding: Theme.s2
                            leftPadding: Theme.s3
                            rightPadding: Theme.s3
                            Toggle {
                                width: parent.width - parent.leftPadding - parent.rightPadding
                                height: Theme.hRow + Theme.s2
                                label: qsTr("Copy the original file alongside")
                                tip: qsTr("Puts the untouched RAW or original next to the export.")
                                checkable: false
                                checked: root.copyOriginal
                                onClicked: root.copyOriginal = !root.copyOriginal
                            }
                            Toggle {
                                width: parent.width - parent.leftPadding - parent.rightPadding
                                height: Theme.hRow + Theme.s2
                                label: qsTr("Copy its XMP sidecar too")
                                tip: qsTr("The original's metadata file goes with it.")
                                checkable: false
                                checked: root.copySidecar
                                enabled: root.copyOriginal
                                onClicked: root.copySidecar = !root.copySidecar
                            }
                            Text {
                                width: parent.width - parent.leftPadding - parent.rightPadding
                                wrapMode: Text.WordWrap
                                text: qsTr("Existing files are never overwritten; a counter is added.")
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                            }
                        }
                    }
                    InspectorGroup {
                        title: qsTr("Finishing"); helpSection: "output"
                        Field {
                            label: qsTr("Sharpen for")
                            SegmentedControl {
                                width: parent.width
                                fill: true
                                labels: [qsTr("Off"), qsTr("Screen"), qsTr("Print")]
                                tips: [qsTr("No output sharpening."), qsTr("A fine radius suited to viewing on a display."), qsTr("A wider radius that survives the softening of ink on paper.")]
                                currentIndex: root.sharpen
                                onActivated: i => root.sharpen = i
                            }
                        }
                        Field {
                            visible: root.sharpen > 0
                            label: qsTr("Amount")
                            SegmentedControl {
                                width: parent.width
                                fill: true
                                labels: [qsTr("Low"), qsTr("Standard"), qsTr("High")]
                                tip: qsTr("How strong the output sharpening is.")
                                currentIndex: root.sharpenAmount
                                onActivated: i => root.sharpenAmount = i
                            }
                        }
                        Field {
                            label: qsTr("Watermark")
                            SearchField {
                                width: parent.width
                                glyph: "type"
                                placeholder: qsTr("Text, e.g. © Your Name")
                                tip: qsTr("Text drawn onto every export at the position, size and opacity below; blank for none.")
                                text: root.watermarkText
                                onTextChanged: root.watermarkText = text
                                onCleared: root.watermarkText = ""
                            }
                        }
                        Field {
                            label: qsTr("Image")
                            Row {
                                width: parent.width
                                spacing: Theme.s2
                                ToolButton {
                                    width: Math.min(implicitWidth, parent.width - (markClear.visible ? markClear.width + parent.spacing : 0))
                                    iconName: "image"
                                    text: root.watermarkImage === "" ? qsTr("Choose…") : root.watermarkImage.split("/").pop()
                                    showLabel: true
                                    tip: qsTr("A PNG logo, with transparency, drawn onto every export.")
                                    onClicked: markDialog.open()
                                }
                                IconButton {
                                    id: markClear
                                    visible: root.watermarkImage !== ""
                                    width: visible ? Theme.szIconHit : 0
                                    iconName: "x"
                                    text: qsTr("Remove the watermark image")
                                    tip: qsTr("Exports go out without the logo.")
                                    onClicked: root.watermarkImage = ""
                                }
                            }
                        }
                        Field {
                            label: qsTr("Position")
                            ComboField {
                                width: parent.width
                                model: [qsTr("Top left"), qsTr("Top"), qsTr("Top right"), qsTr("Left"), qsTr("Centre"), qsTr("Right"), qsTr("Bottom left"), qsTr("Bottom"), qsTr("Bottom right")]
                                tipTitle: qsTr("Position"); tip: qsTr("Where on the picture the watermark sits.")
                                currentIndex: root.watermarkAnchor
                                onActivated: i => root.watermarkAnchor = i
                            }
                        }
                        Field {
                            SliderField {
                                width: parent.width
                                stacked: true
                                label: qsTr("Size %"); from: 1; to: 12; stepSize: 0.5; decimals: 1
                                tip: qsTr("Height of the watermark as a share of the picture's long edge.")
                                value: root.watermarkSize
                                onEdited: v => root.watermarkSize = v
                                onEditingFinished: v => root.watermarkSize = v
                            }
                        }
                        Field {
                            hint: root.finishingActive && !root.finishingWritable
                                ? qsTr("%1 files cannot be finished: sharpening and the watermark are skipped for this format.").arg(root.formatInfo.label || root.format)
                                : qsTr("Applied to the exported file after the engine renders it; the metadata stays. The preview shows the watermark where it will land.")
                            hintColor: root.finishingActive && !root.finishingWritable ? Theme.warning : Theme.textMuted
                            SliderField {
                                width: parent.width
                                stacked: true
                                label: qsTr("Opacity"); from: 0.05; to: 1; stepSize: 0.05; decimals: 2
                                tip: qsTr("How solid the watermark is drawn.")
                                value: root.watermarkOpacity
                                onEdited: v => root.watermarkOpacity = v
                                onEditingFinished: v => root.watermarkOpacity = v
                            }
                        }
                    }
                    InspectorGroup {
                        title: qsTr("Metadata"); helpSection: "output"
                        Column {
                            width: parent.width
                            spacing: Theme.s2
                            topPadding: Theme.s2
                            bottomPadding: Theme.s1
                            leftPadding: Theme.s3
                            rightPadding: Theme.s3
                            Toggle { width: parent.width - parent.leftPadding - parent.rightPadding; height: Theme.hRow + Theme.s2; label: qsTr("Camera EXIF"); tip: qsTr("Camera, lens and exposure details travel with the file."); checked: root.metaExif; onClicked: root.metaExif = !root.metaExif }
                            Toggle { width: parent.width - parent.leftPadding - parent.rightPadding; height: Theme.hRow + Theme.s2; label: qsTr("Location (GPS)"); tip: qsTr("Where the photo was taken travels with the file; off keeps that private."); checked: root.metaLocation; onClicked: root.metaLocation = !root.metaLocation }
                            Toggle { width: parent.width - parent.leftPadding - parent.rightPadding; height: Theme.hRow + Theme.s2; label: qsTr("Keywords"); tip: qsTr("The catalog's keywords are written into the file."); checked: root.metaKeywords; onClicked: root.metaKeywords = !root.metaKeywords }
                            Toggle { width: parent.width - parent.leftPadding - parent.rightPadding; height: Theme.hRow + Theme.s2; label: qsTr("Develop history (XMP)"); tip: qsTr("The full edit recipe goes into the file's XMP, so OmaRAW can read it back."); checked: root.metaHistory; onClicked: root.metaHistory = !root.metaHistory }
                            Text {
                                width: parent.width - parent.leftPadding - parent.rightPadding
                                wrapMode: Text.WordWrap
                                text: qsTr("Location is off by default so a shared file does not carry where it was taken. The Software tag reads OmaRAW.")
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                            }
                        }
                    }
                    InspectorGroup {
                        title: qsTr("Print & contact sheet"); helpSection: "print"
                        Field {
                            label: qsTr("Paper")
                            ComboField {
                                width: parent.width
                                model: backend.sheetPapers()
                                tipTitle: qsTr("Paper"); tip: qsTr("The page size of the PDF.")
                                currentIndex: Math.max(0, backend.sheetPapers().indexOf(root.sheetPaper))
                                onActivated: i => root.sheetPaper = backend.sheetPapers()[i]
                            }
                        }
                        Field {
                            label: qsTr("Orientation")
                            SegmentedControl {
                                width: parent.width
                                fill: true
                                labels: [qsTr("Auto"), qsTr("Portrait"), qsTr("Landscape")]
                                tips: [qsTr("Each page turns to suit its photo."), qsTr("Every page upright."), qsTr("Every page on its side.")]
                                currentIndex: ["auto", "portrait", "landscape"].indexOf(root.sheetOrientation)
                                onActivated: i => root.sheetOrientation = ["auto", "portrait", "landscape"][i]
                            }
                        }
                        Field {
                            label: qsTr("Paper profile")
                            hint: root.sheetIcc !== "" ? qsTr("OmaRAW converts the PDF pages into this profile. When printing the PDF, avoid applying the same colour conversion again.") : ""
                            hintColor: Theme.warning
                            Row {
                                width: parent.width
                                spacing: Theme.s2
                                Text {
                                    width: Math.max(0, parent.width - iccButtons.width - parent.spacing)
                                    height: Theme.hControl
                                    text: root.sheetIcc !== "" ? engine.proofProfileNameOf(root.sheetIcc) : qsTr("Driver-managed (sRGB)")
                                    elide: Text.ElideMiddle
                                    verticalAlignment: Text.AlignVCenter
                                    font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
                                    color: root.sheetIcc !== "" ? Theme.textPrimary : Theme.textMuted
                                }
                                Row {
                                    id: iccButtons
                                    spacing: Theme.s1
                                    IconButton { iconName: "folder-open"; text: qsTr("Choose a paper profile…"); tip: qsTr("The ICC profile for this printer and paper; the pages are converted into it."); onClicked: paperIccDialog.open() }
                                    IconButton { visible: root.sheetIcc !== ""; width: visible ? Theme.szIconHit : 0; iconName: "x"; text: qsTr("Use sRGB"); tip: qsTr("Keeps PDF pages in sRGB for the application or lab receiving the file."); onClicked: root.sheetIcc = "" }
                                }
                            }
                            ComboField {
                                visible: root.sheetIcc !== ""
                                width: parent.width
                                model: [qsTr("Perceptual"), qsTr("Relative colorimetric"), qsTr("Saturation"), qsTr("Absolute colorimetric")]
                                tipTitle: qsTr("Rendering intent"); tip: qsTr("How colours the paper cannot show are brought in; perceptual for photos.")
                                currentIndex: root.sheetIntent
                                onActivated: i => root.sheetIntent = i
                            }
                            Toggle {
                                visible: root.sheetIcc !== ""
                                width: parent.width
                                height: Theme.hRow + Theme.s2
                                label: qsTr("Black point compensation")
                                tip: qsTr("Maps the picture's black to the paper's darkest black so shadows are not crushed.")
                                checkable: false
                                checked: root.sheetBlackPoint
                                onClicked: root.sheetBlackPoint = !root.sheetBlackPoint
                            }
                        }
                        Field {
                            SliderField {
                                width: parent.width; stacked: true
                                label: qsTr("Columns"); from: 1; to: 8; stepSize: 1; decimals: 0
                                tip: qsTr("Thumbnails across each contact sheet page.")
                                value: root.sheetColumns
                                onEdited: v => root.sheetColumns = v
                                onEditingFinished: v => root.sheetColumns = v
                            }
                        }
                        Field {
                            SliderField {
                                width: parent.width; stacked: true
                                label: qsTr("Rows"); from: 1; to: 8; stepSize: 1; decimals: 0
                                tip: qsTr("Thumbnails down each contact sheet page.")
                                value: root.sheetRows
                                onEdited: v => root.sheetRows = v
                                onEditingFinished: v => root.sheetRows = v
                            }
                        }
                        Column {
                            width: parent.width
                            spacing: Theme.s2
                            topPadding: Theme.s2
                            bottomPadding: Theme.s1
                            leftPadding: Theme.s3
                            rightPadding: Theme.s3
                            Toggle {
                                width: parent.width - parent.leftPadding - parent.rightPadding
                                height: Theme.hRow + Theme.s2
                                label: qsTr("Captions (file names)")
                                tip: qsTr("Prints each file's name under its thumbnail.")
                                checked: root.sheetCaptions
                                onClicked: root.sheetCaptions = !root.sheetCaptions
                            }
                        }
                        Field {
                            label: qsTr("Sheet title")
                            SearchField {
                                width: parent.width
                                glyph: "type"
                                placeholder: qsTr("Optional header")
                                tip: qsTr("A title printed at the top of every page.")
                                text: root.sheetTitle
                                onTextChanged: root.sheetTitle = text
                                onCleared: root.sheetTitle = ""
                            }
                        }
                        Column {
                            id: sheetActions
                            width: parent.width
                            spacing: Theme.s2
                            topPadding: Theme.s2
                            bottomPadding: Theme.s2
                            leftPadding: Theme.s3
                            rightPadding: Theme.s3
                            readonly property bool can: root.sourceCount > 0 && root.folder !== ""
                            ToolButton {
                                iconName: "layout-grid"; text: qsTr("Contact sheet PDF"); showLabel: true
                                tip: qsTr("A PDF of thumbnails in the grid set above, written to the export folder.")
                                enabled: sheetActions.can
                                onClicked: root.makeContactSheet()
                            }
                            ToolButton {
                                iconName: "printer"; text: root.sheetBusy ? qsTr("Rendering…") : qsTr("Print PDF, one per page"); showLabel: true
                                tip: qsTr("A PDF with each photo rendered through the engine on its own page, written to the export folder.")
                                enabled: sheetActions.can && engine.ready && !engine.exporting && !root.sheetBusy
                                onClicked: root.makePrintSheet()
                            }
                            Text {
                                width: parent.width - parent.leftPadding - parent.rightPadding
                                wrapMode: Text.WordWrap
                                text: qsTr("PDFs land in the destination folder, %1 × %2 photos per contact-sheet page from the Library thumbnails; the print layout renders every photo at 300 dpi for the paper first.").arg(root.sheetColumns).arg(root.sheetRows)
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                            }
                        }
                    }
                    InspectorGroup {
                        title: qsTr("Validation"); helpSection: "output"
                        InspectorRow { label: qsTr("Engine"); value: engine.ready ? qsTr("ready") : qsTr("not ready — %1").arg(engine.status) }
                        InspectorRow { label: qsTr("Photos"); value: String(root.sourceCount); mono: true }
                        InspectorRow { label: qsTr("Destination"); value: root.folder === "" ? qsTr("choose a folder") : root.destSpace.exists === false ? qsTr("missing") : root.destSpace.writable === false ? qsTr("not writable") : qsTr("ok") }
                        InspectorRow { label: qsTr("Free space"); value: root.destSpace.bytesFree > 0 ? qsTr("%1 GB free, about %2 MB needed").arg((root.destSpace.bytesFree / 1e9).toFixed(1)).arg(Math.max(1, Math.round(root.estimatedBytes / 1e6))) : ""; hideEmpty: true }
                        InspectorRow { label: qsTr("Offline"); value: root.offlineCount > 0 ? qsTr("%1 source file%2 missing — they will fail").arg(root.offlineCount).arg(root.offlineCount === 1 ? "" : "s") : ""; hideEmpty: true }
                        Text {
                            visible: root.spaceShort
                            width: parent.width
                            leftPadding: Theme.s3
                            rightPadding: Theme.s3
                            wrapMode: Text.WordWrap
                            text: qsTr("The destination may not have room for this export.")
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.warning
                        }
                    }
                    Item { width: 1; height: Theme.s3 }
                    Item {
                        width: parent.width
                        height: exportButton.height
                        Rectangle {
                        id: exportButton
                        x: Theme.s3; width: parent.width - Theme.s3 * 2; height: Theme.hControl + Theme.s3
                        radius: Theme.rControl
                        readonly property bool can: engine.ready && root.sourceCount > 0 && root.folder !== "" && root.destSpace.writable !== false && !engine.exporting && root.outputProfileError === ""
                        color: can ? (exportTap.pressed ? Theme.pressedOn(Theme.accent) : exportHover.hovered ? Theme.hovered(Theme.accent) : Theme.accent) : Theme.controlBg
                        opacity: can ? 1 : Theme.disabledOpacity
                        Text {
                            anchors.centerIn: parent
                            text: engine.exporting ? qsTr("Exporting…") : qsTr("Export %1 photo%2").arg(root.sourceCount).arg(root.sourceCount === 1 ? "" : "s")
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl; font.weight: Theme.wHeading
                            color: exportButton.can ? Theme.accentText : Theme.textMuted
                        }
                        HoverHandler { id: exportHover }
                        Tooltip {
                            text: qsTr("Export")
                            description: exportButton.can ? qsTr("Queues every photo in the source with the settings above; files are written in the background.")
                                       : root.folder === "" ? qsTr("Choose a destination folder first.") : root.sourceCount === 0 ? qsTr("Nothing to export: select photos or choose a source.") : ""
                            visible: exportHover.hovered && description !== ""
                        }
                        TapHandler {
                            id: exportTap
                            enabled: exportButton.can
                            onTapped: engine.exportBatchItems(root.sourceItems, root.folder, root.maxEdge, root.quality, root.nameArg, root.format, root.bpp, engine.metaFlagsFor(root.metaExif, root.metaLocation, root.metaKeywords, root.metaHistory), root.finishingActive ? root.finishing : {}, root.resizeArg)
                        }
                        }
                    }
                  }
                }
            }
        }
        OutputQueue {
            id: queue
            width: parent.width
            height: queue.collapsed ? Theme.hDockHeader : root.shell.filmstripHeight + Theme.s5
        }
    }
}
