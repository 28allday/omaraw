pragma ComponentBehavior: Bound
import QtCore
import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui
import OmaRaw.Library

// Ordered workflow sections and independently folding tools beneath the histogram.
// Editor instances survive navigation; layout changes never change a photo.
Rectangle {
    id: root
    objectName: "adjustmentPanel"
    color: Theme.panelBg
    // The Develop workspace, so the Local panel can reach the viewer's
    // paint mode. Null in isolation (the shell test).
    property var develop: null
    function saveModulePreset(operation, label) { if (develop) develop.saveModulePreset(operation, label) }
    readonly property var groups: ["Prepare", "Lens", "Basic", "Color", "Detail", "Effects"]
    readonly property var sectionLabels: [qsTr("Prepare"), qsTr("Lens & Geometry"), qsTr("Light & Tone"),
        qsTr("Colour & Look"), qsTr("Detail"), qsTr("Effects")]
    readonly property var sectionChoices: sectionLabels.map((label, i) => (i + 1) + "  " + label)
    readonly property var tools: ["Local", "Retouch"]
    readonly property var scopeLabels: scope.labels
    property string group: "Prepare"
    property string lastSection: "Prepare"
    readonly property bool toolActive: tools.includes(group)
    function migratedTab(key) {
        const previous = {Profile:"Prepare", Noise:"Prepare", WhiteBalance:"Basic", Curve:"Basic",
            Texture:"Detail", Sharpening:"Detail", Grading:"Color", Film:"Color", Finishing:"Effects"}
        return previous[key] || (groups.includes(key) || tools.includes(key) ? key : "Prepare")
    }
    onGroupChanged: {
        if (group !== migratedTab(group)) { group = migratedTab(group); return }
        if (!tools.includes(group)) { if (restored) lastSection = group; if (scroller.contentItem) scroller.contentItem.contentY = 0 }
    }
    function leaveTool() { if (!toolActive) return false; group = migratedTab(lastSection); return true }
    property bool solo: true
    property var openTools: ({"colorin": true, "flip": true, "channelmixerrgb": true, "omarawgrade": true, "presence": true, "omarawhalation": true})
    property var advancedTools: ({})
    property var visibility: ({})
    readonly property var toolOrder: ["colorin", "negadoctor", "aidenoise", "denoiseprofile", "capture", "airemove", "flip", "lens", "cacorrect", "defringe", "ashift", "crop", "channelmixerrgb", "highlights", "light", "sigmoid", "filmicrgb", "toneequal", "rgblevels", "curves", "primaries", "omarawmatch", "omarawgrade", "colorbalancergb", "colorzones", "colorequal", "lab", "monochrome", "omarawprofile", "omarawprint", "lut3d", "presence", "contrastntexture", "atrous", "sharpen", "omarawhalation", "bloom", "vignette", "grain"]
    readonly property var combinedTools: ({light:["exposure", "shadhi"], presence:["bilat", "diffuse", "hazeremoval"]})
    function toolKey(key) {
        if (key === "rgbcurve" || key === "tonecurve") return "curves"
        for (const combined of Object.keys(combinedTools)) if (combinedTools[combined].includes(key)) return combined
        return key
    }
    function migrateTools(saved, visibilityMap) {
        const next = Object.assign({}, saved)
        // Capture used to share Sharpening's fold and visibility preference.
        if (next.capture === undefined && saved.sharpen !== undefined) next.capture = saved.sharpen
        for (const combined of Object.keys(combinedTools)) {
            const previous = combinedTools[combined]
            if (previous.some(key => key !== combined && saved[key] !== undefined)) {
                // An open member keeps the combined card open. Hide the new
                // card only if every member was deliberately hidden.
                if (next[combined] === undefined || previous.includes(combined))
                    next[combined] = visibilityMap ? previous.some(key => saved[key] !== false) : previous.some(key => saved[key] === true)
                for (const key of previous) if (key !== combined) delete next[key]
            }
        }
        return next
    }
    property var cards: []
    property var legacyAdvanced: ({})
    property bool restored: false
    Settings {
        id: layoutSettings
        category: "develop"
        property alias openSection: root.lastSection
        property bool soloTools: true
        property string toolLayout: ""
        // Read the old choice once, translating section-wide Advanced into tool choices.
        property string fullerSections: "{}"
    }
    Component.onCompleted: {
        try {
            if (layoutSettings.toolLayout !== "") {
                const saved = JSON.parse(layoutSettings.toolLayout)
                openTools = migrateTools(saved.open || openTools, false); advancedTools = saved.advanced || {}; visibility = migrateTools(saved.visibility || {}, true)
            } else {
                legacyAdvanced = JSON.parse(layoutSettings.fullerSections)
                const next = {}
                for (const card of cards) if (legacyAdvancedFor(card)) next[card.key] = true
                advancedTools = next
                if (lastSection === "Curve") openTools = Object.assign({}, openTools, {light:false, curves:true})
                if (lastSection === "Film") openTools = Object.assign({}, openTools, {omarawgrade:false, omarawprint:true})
            }
        } catch (e) { console.warn("Could not restore Develop tool layout:", e) }
        solo = layoutSettings.soloTools
        if (group === "Prepare") group = migratedTab(lastSection)
        normaliseOpenTools()
        restored = true
        if (!toolActive) lastSection = group
        for (const card of cards) retainTool(card)
    }
    function persistLayout() {
        if (!restored) return
        layoutSettings.toolLayout = JSON.stringify({open:openTools, advanced:advancedTools, visibility:visibility})
        layoutSettings.soloTools = solo
    }
    onOpenToolsChanged: persistLayout()
    onAdvancedToolsChanged: persistLayout()
    onVisibilityChanged: persistLayout()
    function normaliseOpenTools() {
        if (!solo) return
        const next = Object.assign({}, openTools), seen = {}
        for (const card of cards.slice().sort((a, b) => toolOrder.indexOf(a.key) - toolOrder.indexOf(b.key))) {
            if (next[card.key]) { if (seen[card.tab]) next[card.key] = false; else seen[card.tab] = true }
        }
        openTools = next
    }
    onSoloChanged: { normaliseOpenTools(); persistLayout() }
    function legacyAdvancedFor(card) { return Object.keys(legacyAdvanced).some(key => legacyAdvanced[key] && migratedTab(key) === card.tab) }
    function registerTool(card) {
        cards = cards.concat([card])
        // Completion order differs between direct cards and Repeater delegates.
        // Apply the old section preference to cards that arrive after restoration.
        if (advancedTools[card.key] === undefined && legacyAdvancedFor(card)) setAdvanced(card.key, true)
        retainTool(card)
    }
    function unregisterTool(card) { cards = cards.filter(c => c !== card) }
    function isToolOpen(key) { return openTools[toolKey(key)] === true }
    function toggleTool(key, tab) {
        key = toolKey(key)
        const next = Object.assign({}, openTools), opening = !next[key]
        if (opening && solo) for (const card of cards) if (card.tab === tab) next[card.key] = false
        next[key] = opening; openTools = next
    }
    function showTool(key) {
        key = toolKey(key)
        const card = cards.find(c => c.key === key)
        if (!card) return false
        group = card.tab; setToolVisible(key, true)
        if (!isToolOpen(key)) toggleTool(key, card.tab)
        revealTool(card); return true
    }
    function revealTool(card) { scroller.reveal(card) }
    function setAdvanced(key, on) { if (advancedTools[key] !== on) advancedTools = Object.assign({}, advancedTools, {[key]:on}) }
    function setToolVisible(key, on) {
        key = toolKey(key)
        // Applied edits must remain discoverable, as in the Customise menu.
        on = on || cards.some(card => card.key === key && card.available && card.contributes)
        if (visibility[key] !== on) visibility = Object.assign({}, visibility, {[key]:on})
    }
    function retainTool(card) {
        // Revealing an edited tool is a layout choice, not a live filter.
        // Reset, bypass, Undo and another tone rendering must not remove the
        // editor under the pointer. Remember it until deliberately hidden.
        if (restored && card.available && card.contributes && visibility[card.key] !== true)
            setToolVisible(card.key, true)
    }
    function restoreDefaultTools() {
        const next = {}
        for (const card of cards) if (card.available && card.contributes) next[card.key] = true
        visibility = next
    }
    function toolVisible(key, specialist, contributes) { return contributes || (visibility[key] !== undefined ? visibility[key] : !specialist) }
    function moduleChanged(op) {
        engine.paramsVersion
        if (op === "colorzones") return (engine.zones.bands || []).some(b => Math.abs(b.shift || 0) > .005 || Math.abs(b.sat || 0) > .005 || Math.abs(b.lum || 0) > .005)
        return paramsChanged(engine.paramsFor(op))
    }
    function paramsChanged(rows) {
        return rows.some(p => {
            if (p.kind === "file") return (p.text || "") !== ""
            if (p.idle !== undefined && p.enabled === false && p.configured === false) return false
            const start = p.reset !== undefined ? p.reset : p.def
            return start !== undefined && (typeof p.value === "number" ? Math.abs(p.value - start) > 1e-6 : p.value !== start)
        })
    }
    function helpPage(key) {
        const pages = {Prepare:"develop-prepare", Lens:"develop-lens", Basic:"develop-basic",
            Color:"develop-colour", Detail:"develop-detail", Effects:"develop-film"}
        return pages[key] || "develop-" + key.toLowerCase()
    }
    // Legacy section reset API stays for automation; the UI resets individual tools.
    function sectionRowsOf(key) {
        const gradingOps = ["omarawgrade"]
        const mine = p => key === "Grading" ? gradingOps.indexOf(p.op) >= 0 : Names.section(p) === key && gradingOps.indexOf(p.op) < 0
        // Of raw conversion only capture sharpening is on show; the method's
        // listed default (RCD) is wrong for X-Trans sensors, so it is left be.
        return engine.params.filter(p => mine(p) && p.enabled !== false && p.def !== undefined && p.kind !== "file"
                                         && !(p.op === "demosaic" && p.field !== "cs_enabled"))
    }
    function bent(c) { const xs = (c && c.xs) || [], ys = (c && c.ys) || []; return xs.some((x, i) => Math.abs(x - ys[i]) > 1e-5) }
    function sectionChanged(key) {
        if (paramsChanged(sectionRowsOf(key))) return true
        if (key === "Color") {
            const z = engine.zones || ({}), lab = engine.parametric || ({})
            return (z.enabled === true && (z.bands || []).some(b => Math.abs(b.shift || 0) > 0.005 || Math.abs(b.sat || 0) > 0.005 || Math.abs(b.lum || 0) > 0.005))
                || (lab.ab || []).some(c => root.bent(c))
        }
        if (key === "Detail") return engine.params.some(p => (p.op === "denoiseprofile" || p.op === "hotpixels") && p.enabled === true)
        if (key === "Curve") {
            const c = engine.curve || ({}), r = engine.parametric || ({})
            return (c.enabled === true && (c.channels || []).some(ch => root.bent(ch)))
                || (r.enabled === true && ["highlights", "lights", "darks", "shadows"].some(k => Math.abs(r[k] || 0) > 1e-6))
        }
        return false
    }
    // One undo step, however many modules the section touches.
    function resetSection(key) {
        engine.beginUndoGroup()
        resetSectionSteps(key)
        engine.endUndoGroup()
    }
    function resetSectionSteps(key) {
        const rows = sectionRowsOf(key)
        if (rows.length > 0) {
            const values = rows.map(p => ({op: p.op, field: p.field, value: p.op === "bloom" ? p.def : p.reset !== undefined ? p.reset : p.def}))
            if (rows.some(p => p.op === "bloom")) values.push({op:"bloom",enabled:false})
            engine.applyValues(values)
        }
        if (key === "Color") {
            engine.resetModule("colorzones")
            if ((engine.parametric.ab || []).some(c => root.bent(c))) engine.resetLabColour()
        } else if (key === "Detail") {
            if (engine.params.some(p => (p.op === "denoiseprofile" || p.op === "hotpixels") && p.enabled === true)) {
                engine.setModuleEnabled("denoiseprofile", false); engine.setModuleEnabled("hotpixels", false)
            }
        } else if (key === "Curve") {
            engine.resetModule("rgbcurve")
            engine.resetModule("tonecurve")
        }
    }
    component SpecialistTool: ToolCard {
        id: nativeCard
        required property string sourceGroup
        title: Names.module(key, key); tip: Names.moduleTip(key); specialist: true
        available: nativeEditor.operations.length > 0
        NativeToolsPanel { id: nativeEditor; width: parent.width; group: nativeCard.sourceGroup; requestedOperation: nativeCard.key; embedded: true }
    }
    ContextMenu {
        id: toolsMenu
        objectName: "customiseToolsMenu"
        implicitWidth: 270
        MenuAction { text: qsTr("One tool open at a time"); checkable: true; checked: root.solo; onTriggered: root.solo = !root.solo }
        MenuAction { text: qsTr("Collapse tools in this section"); onTriggered: { const next = Object.assign({}, root.openTools); for (const card of root.cards) if (card.tab === root.group) next[card.key] = false; root.openTools = next } }
        Repeater {
            model: root.cards.filter(card => card.tab === root.group && card.available).sort((a, b) => root.toolOrder.indexOf(a.key) - root.toolOrder.indexOf(b.key))
            MenuAction {
                required property var modelData
                text: modelData.title
                checkable: true; checked: root.toolVisible(modelData.key, modelData.specialist, modelData.contributes)
                enabled: !modelData.contributes
                Accessible.description: modelData.contributes ? qsTr("Visible because its edited settings affect this photo.") : qsTr("Show or hide this tool. Its settings are kept.")
                onTriggered: root.setToolVisible(modelData.key, !root.toolVisible(modelData.key, modelData.specialist, modelData.contributes))
            }
        }
        C.MenuSeparator { contentItem: Rectangle { implicitHeight: Theme.hairline; color: Theme.border } }
        MenuAction { text: qsTr("Restore default tools"); onTriggered: root.restoreDefaultTools() }
    }
    Column {
        anchors.fill: parent
        anchors.leftMargin: Theme.hairline
        spacing: 0
        Item {
            width: parent.width; height: Theme.hField + Theme.s2
            ComboField {
                objectName: "scopeSelector"
                anchors.left: parent.left; anchors.right: expandScope.left
                anchors.leftMargin: Theme.s3; anchors.rightMargin: Theme.s1
                anchors.bottom: parent.bottom
                caption: qsTr("Scope")
                model: root.scopeLabels
                currentIndex: engine.scopeMode
                tipTitle: qsTr("Exposure and colour scopes")
                tip: qsTr("Choose histogram, waveform, RGB parade, vectorscope or false colour. RGB parade shows red, green and blue side by side. Scopes read the sRGB preview before monitor conversion.")
                onActivated: index => engine.scopeMode = index
            }
            IconButton {
                id: expandScope
                objectName: "expandScopeButton"
                anchors.right: parent.right; anchors.rightMargin: Theme.s3
                anchors.bottom: parent.bottom
                height: Theme.hField; width: Theme.hField
                iconName: engine.scopeExpanded ? "minimize-2" : "maximize-2"
                text: engine.scopeExpanded ? qsTr("Compact scopes") : qsTr("Enlarge scopes")
                tip: qsTr("Show a larger, movable scope over the photo while keeping adjustments available.")
                checkable: true; checked: engine.scopeExpanded
                enabled: root.develop !== null && engine.imageId >= 0
                onClicked: engine.scopeExpanded = !engine.scopeExpanded
            }
        }
        ScopeView { id: scope; width: parent.width; height: implicitHeight }
        // Said only when there is something to say, in a line that is always
        // kept: appearing, going or wrapping as renders land, it moved every
        // control below it under the pointer in the middle of a drag.
        Item {
            id: clipping
            objectName: "clippingLine"
            width: parent.width; height: clippingNote.implicitHeight + Theme.s2
            HoverHandler { id: clippingHover }
            Tooltip { text: clippingNote.text; visible: clippingHover.hovered && clippingNote.truncated }
            readonly property string note: [engine.clippedHighlights >= 0.05 ? qsTr("%1% of highlights clipped").arg(engine.clippedHighlights.toFixed(1)) : "",
                                            engine.clippedShadows >= 0.05 ? qsTr("%1% of shadows clipped").arg(engine.clippedShadows.toFixed(1)) : ""].filter(t => t !== "").join(" · ")
            Text {
                id: clippingNote
                objectName: "clippingNote"
                x: Theme.s3; width: parent.width - Theme.s3 * 2
                anchors.verticalCenter: parent.verticalCenter
                text: engine.rawClippingShown && engine.rawClippingStatus !== "" ? engine.rawClippingStatus : clipping.note
                elide: Text.ElideRight
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
        }
        Item {
            width: parent.width; height: Theme.hControl + Theme.s3
            Row {
                anchors.left: parent.left; anchors.leftMargin: Theme.s3
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.s1
                ToolButton {
                    objectName: "autoButton"
                    iconName: "wand-sparkles"; text: qsTr("Auto"); showLabel: true
                    tip: qsTr("Estimates exposure before film looks and tone curves, with highlight protection. Import supplies a starting correction. White Balance has its own Auto.")
                    enabled: engine.imageId >= 0; onClicked: engine.autoExposure()
                }
            }
            ToolButton {
                objectName: "resetAllButton"
                anchors.right: parent.right; anchors.rightMargin: Theme.s3
                anchors.verticalCenter: parent.verticalCenter
                iconName: "rotate-ccw"; text: qsTr("Reset"); showLabel: true
                tip: qsTr("Restores the import starting look, including automatic corrections, and clears the development history and redo steps. This reset cannot be undone. Reset to Camera Original also removes the starting look.")
                enabled: engine.imageId >= 0 && engine.history.length > 0; onClicked: engine.resetHistory()
            }
        }
        // The tools that work on the picture itself.
        Item {
            width: parent.width; height: Theme.hControl + Theme.s2
            Row {
                anchors.left: parent.left; anchors.leftMargin: Theme.s3
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.s1
                ToolButton {
                    objectName: "tool_Crop"
                    iconName: "crop"; text: qsTr("Crop"); showLabel: true; shortcut: "R"
                    tip: qsTr("Crop and straighten: drag the frame's edges and corners, pick a ratio.")
                    enabled: engine.imageId >= 0; checked: engine.cropMode
                    onClicked: engine.cropMode = !engine.cropMode
                }
                ToolButton {
                    objectName: "tool_Local"
                    iconName: "circle-dashed"; text: Names.group("Local"); showLabel: true
                    tip: Names.groupTip("Local")
                    enabled: engine.imageId >= 0; checked: root.group === "Local"
                    onClicked: { if (root.group === "Local") root.leaveTool(); else { engine.cropMode = false; root.group = "Local" } }
                }
                ToolButton {
                    objectName: "tool_Retouch"
                    iconName: "bandage"; text: Names.group("Retouch"); showLabel: true
                    tip: Names.groupTip("Retouch")
                    enabled: engine.imageId >= 0; checked: root.group === "Retouch"
                    onClicked: { if (root.group === "Retouch") root.leaveTool(); else { engine.cropMode = false; root.group = "Retouch" } }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.hairline; color: Theme.border }
        }
        Item {
            visible: !root.toolActive
            width: parent.width; height: visible ? workflow.implicitHeight + Theme.s2 * 2 : 0
            ComboField {
                id: workflow
                objectName: "workflowSection"
                anchors.left: parent.left; anchors.right: parent.right; anchors.margins: Theme.s3
                anchors.verticalCenter: parent.verticalCenter
                model: root.sectionChoices
                currentIndex: root.groups.indexOf(root.group)
                tipTitle: qsTr("Editing workflow")
                tip: currentText
                popup.implicitHeight: Math.min(workflow.popup.contentItem.implicitHeight + Theme.s1 * 2, Math.max(160, root.height * 0.6))
                onActivated: index => { if (index >= 0 && index < root.groups.length) root.group = root.groups[index] }
                implicitHeight: Math.max(Theme.hControl, contentItem.implicitHeight + Theme.s2 * 2)
                contentItem: Text {
                    objectName: "workflowLabel"
                    text: workflow.displayText; wrapMode: Text.WordWrap
                    font: workflow.font; color: Theme.textPrimary
                    verticalAlignment: Text.AlignVCenter
                }
                // Full names remain readable in a narrow dock and in the menu.
                delegate: C.ItemDelegate {
                    id: workflowOption
                    required property int index
                    required property string modelData
                    objectName: "workflowOption_" + index
                    width: workflow.width
                    implicitHeight: Math.max(Theme.hRow, contentItem.implicitHeight + Theme.s2 * 2)
                    highlighted: workflow.highlightedIndex === index
                    contentItem: Text {
                        text: workflowOption.modelData; wrapMode: Text.WordWrap
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
                        color: Theme.textPrimary
                    }
                    background: Rectangle { color: parent.highlighted ? Theme.hoverBg : "transparent" }
                }
            }
        }
        Item {
            visible: !root.toolActive
            width: parent.width; height: visible ? Theme.hControl : 0
            ToolButton {
                objectName: "customiseTools"
                anchors.left: parent.left; anchors.leftMargin: Theme.s2
                text: qsTr("Customise tools"); showLabel: true; iconName: "sliders-horizontal"
                tip: qsTr("Choose the tools in this section and whether several can stay open.")
                onClicked: toolsMenu.popup(this, 0, height)
            }
            IconButton {
                anchors.right: parent.right; anchors.rightMargin: Theme.s2
                iconName: "circle-help"; text: qsTr("Help for these tools")
                onClicked: { const w = root.Window.window; if (w && w.help) w.help(root.helpPage(root.group)) }
            }
        }
        C.ScrollView {
            id: scroller
            objectName: "adjustmentScroll"
            // Brings a tool that has just been opened to the top of the list,
            // so all of it (four colour wheels, eight colour bands) is in view
            // without hunting for it under the controls above.
            // (After the list has taken the opened tool's height into account.)
            function reveal(item) { revealTimer.target = item; revealTimer.tries = 0; revealTimer.restart() }
            Timer {
                id: revealTimer
                property Item target: null
                property int tries: 0
                interval: Math.max(60, Theme.dSlow + 20)
                onTriggered: {
                    const flick = scroller.contentItem
                    if (!target) return
                    // Opened while its section was still arriving: look again shortly.
                    if (!target.visible) { if (++tries < 25) restart(); return }
                    const top = target.mapToItem(flick.contentItem, 0, 0).y
                    flick.contentY = Math.max(0, Math.min(top, flick.contentHeight - flick.height))
                }
            }
            width: parent.width
            height: parent.height - y
            clip: true
            C.ScrollBar.vertical: ScrollBar {}
            C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
            contentWidth: availableWidth
            Column {
                width: scroller.availableWidth
                spacing: 0
                    Item {
                        visible: root.toolActive
                        width: parent.width; height: visible ? Math.round(Theme.hControl * 1.5) : 0
                        IconButton {
                            id: toolBack
                            objectName: "toolBack"
                            anchors.left: parent.left; anchors.leftMargin: Theme.s2
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "chevron-left"; text: qsTr("Back to the adjustments"); shortcut: "Esc"
                            tip: qsTr("Leaves the tool. Its work stays on the picture.")
                            onClicked: root.leaveTool()
                        }
                        Text {
                            anchors.left: toolBack.right; anchors.leftMargin: Theme.s1
                            anchors.right: toolHelp.left; anchors.rightMargin: Theme.s2
                            anchors.verticalCenter: parent.verticalCenter
                            text: Names.group(root.group)
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl + 1; font.weight: Theme.wHeading
                            color: Theme.textPrimary
                        }
                        IconButton {
                            id: toolHelp
                            anchors.right: parent.right; anchors.rightMargin: Theme.s2
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "circle-help"; text: qsTr("Help for %1").arg(Names.group(root.group)); shortcut: "F1"
                            tip: qsTr("Opens the guide for this tool.")
                            onClicked: { const w = root.Window.window; if (w && w.help) w.help(root.helpPage(root.group)) }
                        }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.hairline; color: Theme.border }
                    }
                ToolCard {
                    controller: root; key: "colorin"; tab: "Prepare"; title: qsTr("Camera profile")
                    available: (engine.paramsVersion, engine.paramsFor("colorin")).length > 0
                    changed: engine.cameraProfileState.active === true || (engine.cameraProfileState.cameraLook || {}).applied === true || root.moduleChanged("colorin")
                    CameraProfilePanel {
                        width: parent.width
                        advanced: root.advancedTools.colorin === true
                        toggleAdvanced: () => root.setAdvanced("colorin", !root.advancedTools.colorin)
                    }
                }
                SpecialistTool { controller: root; key: "negadoctor"; operation: key; tab: "Prepare"; sourceGroup: "Color" }
                ToolCard {
                    controller: root; key: "aidenoise"; tab: "Prepare"; title: qsTr("AI denoise")
                    resetAction: () => aiDenoise.reset()
                    resetTip: qsTr("Clears the denoise preview and restores its controls. A saved denoised DNG keeps its pixels; use Photo versions to open the source.")
                    DenoisePanel { id: aiDenoise; width: parent.width }
                }
                ToolCard {
                    controller: root; key: "denoiseprofile"; operation: key; tab: "Prepare"; title: qsTr("Noise reduction"); available: noise.found
                    toggleEnabled: () => { if (noise.moduleOn) engine.setModuleEnabled(noise.op, false); else noise.send(noise.what, noise.amount, noise.keepDetail) }
                    NoiseReductionPanel { id: noise; embedded: true; width: parent.width }
                    ToolButton { objectName: "resetNoiseTool"; x: Theme.s3; text: qsTr("Reset noise reduction"); showLabel: true; iconName: "rotate-ccw"; enabled: noise.moduleOn || noise.hotOn; onClicked: noise.reset() }
                }
                ToolCard {
                    id: capture
                    controller: root; key: "capture"; tab: "Prepare"; title: qsTr("Capture sharpening")
                    readonly property var row: (engine.paramsVersion, engine.paramsFor("demosaic")).find(p => p.field === "cs_enabled")
                    readonly property bool raw: (engine.paramsVersion, engine.paramsFor("demosaic")).some(p => p.field === "demosaicing_method" && p.enabled === true)
                    available: raw && row !== undefined
                    moduleOn: row !== undefined && row.value > 0.5
                    changed: row !== undefined && row.value !== row.def
                    CheckField {
                        objectName: "captureSharpening"
                        x: Theme.s3; width: parent.width - Theme.s3 * 2
                        text: qsTr("Capture sharpening")
                        tip: qsTr("Undoes the slight softness every lens and sensor adds, before other edits. Judge at 100%.")
                        checked: capture.moduleOn
                        onClicked: { const was = capture.moduleOn; checked = Qt.binding(() => capture.moduleOn)
                            engine.applyValues([{op:"demosaic", field:"cs_enabled", value:was ? 0 : 1}, {op:"demosaic", field:"cs_radius", value:0}]) }
                    }
                }
                ToolCard {
                    controller: root; key: "airemove"; tab: "Prepare"; title: qsTr("AI object removal")
                    changed: root.moduleChanged("omarawrepair")
                    moduleOn: (engine.paramsVersion, engine.moduleEnabled("omarawrepair"))
                    AiPanel { kind: "remove"; develop: root.develop }
                }
                ModuleTool { controller: root; tab: "Lens"; operation: "flip" }
                ModuleTool { controller: root; tab: "Lens"; operation: "lens"; LensPanel { width: parent.width; visible: found } }
                ModuleTool { controller: root; tab: "Lens"; operation: "cacorrect" }
                ModuleTool { controller: root; tab: "Lens"; operation: "defringe" }
                ModuleTool { controller: root; tab: "Lens"; operation: "ashift"; fields: ["lensshift_v", "lensshift_h"] }
                ToolCard {
                    controller: root; key: "crop"; tab: "Lens"; title: qsTr("Crop & straighten")
                    ToolButton {
                        objectName: "openCropGeometry"; x: Theme.s3; text: qsTr("Crop & straighten"); showLabel: true; iconName: "crop"
                        tip: qsTr("Open crop, straighten, automatic and guided perspective, and precise crop edges.")
                        enabled: engine.imageId >= 0; onClicked: engine.cropMode = true
                    }
                }
                ModuleTool {
                    controller: root; tab: "Basic"; operation: "channelmixerrgb"
                    Item {
                        visible: true
                        width: parent.width; height: visible ? Theme.hControl + Theme.s2 : 0
                        Row {
                            anchors.left: parent.left; anchors.leftMargin: Theme.s3
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.s1
                            ToolButton { iconName: "camera"; text: qsTr("As shot"); showLabel: true; tip: qsTr("White balance as the camera set it."); enabled: engine.imageId >= 0; onClicked: engine.whiteBalanceAsShot() }
                            ToolButton { iconName: "wand-sparkles"; text: qsTr("Auto"); showLabel: true; tip: qsTr("Balances the picture so its average colour is neutral."); enabled: engine.imageId >= 0; onClicked: engine.autoWhiteBalance() }
                            ToolButton {
                                iconName: "pipette"; text: qsTr("Pick a neutral"); showLabel: true; tip: qsTr("Then click something grey or white in the picture to balance from it.")
                                enabled: engine.imageId >= 0 && root.develop !== null
                                checked: root.develop !== null && root.develop.wbPickMode
                                onClicked: if (root.develop) root.develop.wbPickMode = !root.develop.wbPickMode
                            }
                        }
                    }
                }
                ModuleTool { controller: root; tab: "Basic"; operation: "highlights"; specialist: true }
                GroupedTool { controller: root; key: "light"; tab: "Basic"; title: qsTr("Light"); operations: ["exposure", "colorbalancergb", "shadhi"]; fields: ({colorbalancergb:["contrast"]}) }
                ModuleTool { controller: root; tab: "Basic"; operation: "sigmoid"; specialist: true }
                ModuleTool { controller: root; tab: "Basic"; operation: "filmicrgb"; specialist: true }
                SpecialistTool { controller: root; key: "toneequal"; operation: key; tab: "Basic"; sourceGroup: "Basic" }
                SpecialistTool { controller: root; key: "rgblevels"; operation: key; tab: "Basic"; sourceGroup: "Curve" }
                ToolCard {
                    controller: root; key: "curves"; tab: "Basic"; title: qsTr("Curves"); showStatus: true; moduleOn: engine.curve.enabled === true || engine.parametric.enabled === true; available: engine.curve.found === true || engine.parametric.found === true
                    changed: (engine.curve.channels || []).some(c => root.bent(c)) || ["highlights", "lights", "darks", "shadows"].some(k => Math.abs(engine.parametric[k] || 0) > 1e-6)
                    CurvePanel { width: parent.width; saveEnabled: root.develop !== null; onSavePresetRequested: (operation, label) => root.saveModulePreset(operation, label) }
                }
                SpecialistTool { controller: root; key: "primaries"; operation: key; tab: "Color"; sourceGroup: "Color" }
                ToolCard {
                    controller: root; key: "omarawmatch"; operation: key; tab: "Color"; title: qsTr("Image Match")
                    available: (engine.paramsVersion, engine.paramsFor(key)).length > 0
                    tip: qsTr("Match one reference's tone, colour and optional grain to this photo or a selection. All corrections stay editable.")
                    resetAction: () => matchPanel.reset()
                    ImageMatchPanel { id: matchPanel; width: parent.width }
                }
                ToolCard {
                    controller: root; key: "omarawgrade"; operation: key; tab: "Color"; title: qsTr("Primary correction"); available: wheels.params.length > 0
                    PrimaryGradePanel { id: wheels; embedded: true; open: true; width: parent.width }
                }
                ToolCard {
                    controller: root; key: "colorbalancergb"; operation: key; tab: "Color"; title: qsTr("Vibrance & saturation"); available: (engine.paramsVersion, engine.paramsFor("colorbalancergb")).length > 0
                    tip: qsTr("Colour intensity and hue. Reset keeps Contrast in Light; the effect switch and presets include both.")
                    ColourGradingPanel { width: parent.width; embedded: true; more: root.advancedTools.colorbalancergb === true; saveEnabled: root.develop !== null; onSavePresetRequested: (operation, label) => root.saveModulePreset(operation, label) }
                    ToolButton { x: Theme.s3; text: qsTr("Advanced"); showLabel: true; iconName: root.advancedTools.colorbalancergb ? "chevron-up" : "chevron-down"; onClicked: root.setAdvanced("colorbalancergb", !root.advancedTools.colorbalancergb) }
                }
                ToolCard {
                    controller: root; key: "colorzones"; operation: key; tab: "Color"; title: qsTr("Colour mixer"); available: mixer.found
                    ZonesPanel { id: mixer; embedded: true; open: true; width: parent.width }
                }
                ToolCard {
                    controller: root; key: "colorequal"; operation: key; tab: "Color"; title: qsTr("Selective colour"); available: selective.params.length > 0
                    SelectiveColourPanel { id: selective; embedded: true; open: true; width: parent.width }
                }
                ToolCard {
                    controller: root; key: "lab"; tab: "Color"; title: qsTr("Lab colour"); showStatus: true; available: labCurves.found; changed: labCurves.changed; moduleOn: labCurves.moduleOn
                    LabCurvePanel { id: labCurves; embedded: true; open: true; width: parent.width }
                    ToolButton { x: Theme.s3; text: qsTr("Reset Lab colour"); showLabel: true; iconName: "rotate-ccw"; enabled: labCurves.changed; onClicked: engine.resetLabColour() }
                }
                ToolCard {
                    id: monochromeCard
                    controller: root; key: "monochrome"; operation: key; tab: "Color"
                    title: qsTr("Black and white"); tip: Names.moduleTip(key)
                    readonly property var params: (engine.paramsVersion, engine.paramsFor("monochrome"))
                    property bool filtersOpen: false
                    available: params.length > 0
                    Text {
                        x: Theme.s3; width: parent.width - Theme.s3 * 2
                        text: qsTr("Make each original colour darker or lighter in black and white.")
                        wrapMode: Text.Wrap; color: Theme.textMuted
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
                    }
                    ModuleRows {
                        width: parent.width; headings: false
                        rows: monochromeCard.params.filter(p => p.field.startsWith("mix_"))
                    }
                    ToolButton {
                        objectName: "monochromeFilterControls"
                        x: Theme.s3; text: qsTr("Filter controls"); showLabel: true
                        iconName: monochromeCard.filtersOpen ? "chevron-up" : "chevron-down"
                        onClicked: monochromeCard.filtersOpen = !monochromeCard.filtersOpen
                    }
                    SlideSection {
                        width: parent.width; expanded: monochromeCard.filtersOpen
                        ModuleRows {
                            width: parent.width; headings: false
                            rows: monochromeCard.params.filter(p => !p.field.startsWith("mix_"))
                        }
                    }
                }
                ToolCard {
                    controller: root; key: "omarawprofile"; operation: key; tab: "Color"; title: qsTr("Creative profile")
                    available: (engine.paramsVersion, engine.paramsFor(key)).length > 0
                    CreativeProfilePanel { width: parent.width }
                }
                PrintStockTool { controller: root; tab: "Color" }
                ModuleTool { controller: root; tab: "Color"; operation: "lut3d" }
                GroupedTool { controller: root; key: "presence"; tab: "Detail"; title: qsTr("Texture, clarity & dehaze"); operations: ["bilat", "diffuse", "hazeremoval"] }
                SpecialistTool { controller: root; key: "contrastntexture"; operation: key; tab: "Detail"; sourceGroup: "Detail" }
                SpecialistTool { controller: root; key: "atrous"; operation: key; tab: "Detail"; sourceGroup: "Detail" }
                GroupedTool { controller: root; key: "sharpen"; tab: "Detail"; title: qsTr("Sharpening"); operations: ["sharpen"] }
                ModuleTool { controller: root; tab: "Effects"; operation: "omarawhalation" }
                ModuleTool { controller: root; tab: "Effects"; operation: "bloom" }
                ModuleTool { controller: root; tab: "Effects"; operation: "vignette" }
                ModuleTool { controller: root; tab: "Effects"; operation: "grain" }
                LocalPanel {
                    visible: root.group === "Local"; width: parent.width; develop: root.develop
                    onRevealRequested: item => scroller.reveal(item)
                }
                ToolCard {
                    controller: root; key: "rasterfile"; tab: "Local"; title: qsTr("External mask")
                    changed: (engine.toolState.rasterTargets || []).length > 0
                    ExternalMaskPanel { embedded: true; width: parent.width }
                }
                RetouchPanel { visible: root.group === "Retouch"; width: parent.width; develop: root.develop; openAiRemoval: () => root.showTool("airemove") }
                EmptyState { width: parent.width; height: 140; visible: !engine.ready; iconName: "sliders-horizontal"; title: qsTr("Engine not ready"); description: engine.status }
                Item { width: parent.width; height: Theme.s4 }
            }
        }
    }
}
