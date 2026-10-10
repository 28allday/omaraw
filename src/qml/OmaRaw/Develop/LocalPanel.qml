pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// Local adjustments: each one is an exposure instance under a mask made
// of one or more shapes — radial, gradient or brush stroke. Add a local,
// add shapes to it, and choose how each shape combines with the ones
// above it. The active local's shapes are all drawn on the viewer; the
// active shape is the one a drag moves.
Column {
    id: root
    objectName: "localMaskPanel"
    width: parent ? parent.width : 300
    spacing: Theme.s1
    readonly property var locals: engine.locals
    // Delegates persist across a read-back (see StableList); a slider being
    // dragged must not be rebuilt under the pointer. Locals key by priority,
    // shapes and ranges by position.
    StableList { id: localList; source: root.locals; key: l => l.priority }
    // The Develop workspace, for paint mode. Null in isolation.
    property var develop: null
    signal revealRequested(Item target)
    onVisibleChanged: if (!visible && develop) develop.pickChannel = -1
    readonly property var combineNames: [qsTr("Add"), qsTr("Intersect"), qsTr("Subtract"), qsTr("Exclude")]
    readonly property var combineIcons: ["plus", "blend", "minus", "diff"]
    // A local's own sliders, all zero when it does nothing. Only the ones that
    // moved are sent: a stack slider's module is made the first time it moves.
    readonly property var localKeys: ["exposure", "black", "contrast", "highlights", "shadows", "vibrance", "saturation", "hue", "lightness", "warmth", "tint", "clarity", "sharpness", "moire"]
    function resetLocal(local) {
        engine.beginUndoGroup()
        for (const k of localKeys) if (Math.abs(local[k] || 0) > 1e-6) engine.setLocalParam(local.priority, k, 0)
        engine.setLocalEnabled(local.priority, false)
        engine.endUndoGroup()
    }
    function prepareShape() {
        if (develop) { develop.maskVisualsShown = true; develop.penMode = false; develop.brushMode = false; develop.pickChannel = -1 }
    }
    readonly property bool coverageVisible: engine.maskShown && (!develop || develop.maskVisualsShown)

    component AdjustmentGroup: Column {
        id: group
        required property var owner
        required property string key
        required property string title
        required property var fields
        property bool moreColour: false
        width: parent.width
        AccordionHeader {
            objectName: "maskSection_" + group.key
            width: parent.width; text: group.title; open: group.owner.section === group.key
            onClicked: group.owner.section = open ? "" : group.key
        }
        Column {
            objectName: "maskControls_" + group.key
            width: parent.width; visible: group.owner.section === group.key
            LocalColourWheel {
                visible: group.key === "colour"
                x: Theme.s3; width: parent.width - Theme.s3 * 2
                local: group.owner.live
            }
            Repeater {
                model: group.fields
                SliderField {
                    id: adjustment
                    required property var modelData
                    readonly property real factor: modelData.factor || 1
                    visible: group.key !== "colour" || group.moreColour || ["saturation", "lightness"].includes(modelData.key)
                    objectName: "localAdjustment_" + modelData.key
                    x: Theme.s3; width: parent ? parent.width - Theme.s3 * 2 : 0
                    label: modelData.label; from: modelData.from; to: modelData.to
                    stepSize: modelData.step || 1; decimals: modelData.decimals || 0
                    suffix: modelData.suffix || ""; origin: 0; resetOnDoubleClick: true
                    tip: modelData.tip || ""
                    value: (group.owner.live[modelData.key] || 0) * factor
                    onEdited: v => throttle.push(v / factor)
                    onEditingFinished: v => throttle.flush(v / factor)
                    EditCoalescer {
                        id: throttle
                        onSend: v => {
                            if (group.key === "colour") engine.maskShown = false
                            engine.setLocalParam(group.owner.live.priority, adjustment.modelData.key, v)
                        }
                    }
                }
            }
            CheckField {
                objectName: "moreLocalColourControls"
                visible: group.key === "colour"
                x: Theme.s3; text: qsTr("More colour controls"); checked: group.moreColour
                onClicked: group.moreColour = checked
            }
        }
    }

    BlockHeading {
        text: qsTr("Local masks"); showReset: true; resetName: "resetTool_local"
        resetTip: qsTr("Removes the adjustments from every local mask. Keeps their shapes. Undo restores the adjustments.")
        resetAction: () => { engine.beginUndoGroup(); for (const local of root.locals) root.resetLocal(local); engine.endUndoGroup() }
    }

    Flow {
        x: Theme.s3; width: parent.width - Theme.s3 * 2; spacing: Theme.s1
        ToolButton {
            objectName: "newColourRangeMask"; iconName: "pipette"; text: qsTr("Colour Range"); showLabel: true
            enabled: engine.imageId >= 0 && !engine.busy && root.develop !== null
            checked: root.develop && root.develop.pickChannel === 3 && root.develop.colourRangeNew
            tip: qsTr("Click or drag over a colour, such as skin, to create a mask. No drawing is needed.")
            onClicked: root.develop.startColourRange(true)
        }
        ToolButton {
            id: luminosityButton
            objectName: "newLuminosityMask"; iconName: "sun"; text: qsTr("Luminosity"); showLabel: true
            enabled: engine.imageId >= 0 && !engine.busy && root.develop !== null
            checked: root.develop && root.develop.pickChannel === 4
            tip: qsTr("A mask by brightness: highlights, midtones, shadows or a tone you pick. No drawing is needed.")
            onClicked: luminosityMenu.visible ? luminosityMenu.close() : luminosityMenu.popup(luminosityButton, 0, luminosityButton.height)
            ContextMenu {
                id: luminosityMenu
                objectName: "luminosityMenu"
                MenuAction { objectName: "luminosityHighlights"; text: qsTr("Highlights"); onTriggered: { engine.addLuminosityMask("highlights"); engine.maskShown = true } }
                MenuAction { objectName: "luminosityMidtones"; text: qsTr("Midtones"); onTriggered: { engine.addLuminosityMask("midtones"); engine.maskShown = true } }
                MenuAction { objectName: "luminosityShadows"; text: qsTr("Shadows"); onTriggered: { engine.addLuminosityMask("shadows"); engine.maskShown = true } }
                MenuAction { objectName: "luminosityPick"; text: qsTr("Pick a Tone…"); iconName: "pipette"; onTriggered: root.develop.startLuminosityPick() }
            }
        }
        ToolButton { iconName: "circle-dot"; text: qsTr("Radial"); showLabel: true; tip: qsTr("A new local adjustment under an ellipse; drag it into place on the picture."); enabled: engine.imageId >= 0; onClicked: { root.prepareShape(); engine.addLocal(1) } }
        ToolButton { iconName: "diff"; text: qsTr("Gradient"); showLabel: true; tip: qsTr("A new local adjustment fading in across a line; drag it into place on the picture."); enabled: engine.imageId >= 0; onClicked: { root.prepareShape(); engine.addLocal(2) } }
        ToolButton { objectName: "newPenMask"; iconName: "pen-tool"; text: qsTr("Pen"); showLabel: true; checked: root.develop && root.develop.penMode; enabled: engine.imageId >= 0 && root.develop !== null; tip: qsTr("Draw your own mask. Click for corners, drag for curves, then click the first point to close."); onClicked: root.develop.startPen(true) }
    }
    AiPanel { kind: "mask"; develop: root.develop; compact: true }
    Text {
        objectName: "luminosityPickHint"
        visible: root.develop && root.develop.pickChannel === 4
        x: Theme.s3; width: parent.width - Theme.s3 * 2; wrapMode: Text.WordWrap
        text: qsTr("Click or drag over the brightness to select. Escape cancels.")
        color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
    }
    Text {
        objectName: "colourRangePickHint"
        visible: root.develop && root.develop.pickChannel === 3
        x: Theme.s3; width: parent.width - Theme.s3 * 2; wrapMode: Text.WordWrap
        text: qsTr("Click or drag over the colour to select. Escape cancels. Use a shape to protect similar colours elsewhere.")
        color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
    }
    Flow {
        x: Theme.s3; width: parent.width - Theme.s3 * 2; spacing: Theme.s1
    ToolButton {
        objectName: "previewLocalSelection"
        visible: root.locals.length > 0
        text: qsTr("Preview selection"); iconName: "scan-eye"; showLabel: true; checked: root.coverageVisible
        tip: qsTr("Show the selected area as a colour overlay. Turn it off to judge your correction. Hold M for a quick preview.")
        onClicked: {
            const show = !root.coverageVisible
            if (show && root.develop) root.develop.maskVisualsShown = true
            engine.maskShown = show
        }
    }
    ToolButton {
        objectName: "toggleMaskDisplay"
        visible: root.locals.length > 0 && root.develop !== null
        enabled: engine.ai.mode === ""
        iconName: root.develop && root.develop.maskVisualsShown ? "eye-off" : "eye"
        text: root.develop && root.develop.maskVisualsShown ? qsTr("Hide mask") : qsTr("Show mask")
        showLabel: true
        tip: qsTr("Shows or hides mask outlines, points and colour coverage. Your adjustments stay active.")
        onClicked: root.develop.maskVisualsShown = !root.develop.maskVisualsShown
    }
    }
    Column {
        visible: root.develop && root.develop.penMode
        x: Theme.s3; width: parent.width - Theme.s3 * 2; spacing: Theme.s2
        Text { width: parent.width; wrapMode: Text.WordWrap; text: qsTr("Click for corners; drag for curves. Click the first point or press Enter to close. Backspace removes the last point; Escape cancels."); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel }
        SliderField {
            objectName: "draftMaskFeather"
            width: parent.width; stacked: true
            label: qsTr("Feather"); from: 0; to: 100; stepSize: 0.01; decimals: 2; suffix: "%"
            responsePower: 4
            tip: qsTr("Width of the soft edge, as a percentage of the photo's short side. The start of the slider gives fine control; 0.10% is 5 pixels on a 5000-pixel short side.")
            value: (root.develop ? root.develop.penFeather : 0.02) * 100
            onEditingFinished: v => { if (root.develop) root.develop.penFeather = v / 100 }
        }
        Row {
            spacing: Theme.s2
            ToolButton { objectName: "closePenMask"; text: qsTr("Close shape"); enabled: root.develop && root.develop.penEditor.canClose; onClicked: root.develop.penEditor.finish() }
            ToolButton { text: qsTr("Cancel"); onClicked: root.develop.penEditor.cancel() }
        }
    }
    Text {
        x: Theme.s3; width: parent.width - Theme.s3 * 2; wrapMode: Text.WordWrap
        text: root.locals.length === 0
              ? (engine.imageId >= 0 ? qsTr("Pick a colour or draw a shape, then adjust the selected area.")
                                     : qsTr("Open a photo in the engine first."))
              : qsTr("Select a mask below. Open one section at a time to adjust or refine it.")
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
    }
    Repeater {
        model: localList.model
        Column {
            id: item
            required property int index
            required property var modelData
            readonly property var live: root.locals[index] || modelData
            StableList { id: shapeList; source: item.live.shapes }
            StableList { id: rangeList; source: item.live.ranges }
            readonly property bool active: engine.activeLocal === live.priority
            readonly property bool radial: live.shape === 1
            readonly property var lumaRange: (live.ranges || []).find(r => r.channel === 0) || ({})
            readonly property var hueRange: (live.ranges || []).find(r => r.channel === 1) || ({})
            // A luminosity mask: no shapes, selected by brightness and not by colour.
            readonly property bool luminosity: live.shapes.length === 0 && lumaRange.active === true && hueRange.active !== true
            readonly property real lumaFrom: lumaRange.active ? 100 * lumaRange.p1 : 0
            readonly property real lumaTo: lumaRange.active ? 100 * lumaRange.p2 : 100
            readonly property real lumaSoftness: lumaRange.active ? 100 * Math.max(lumaRange.p1 - lumaRange.p0, lumaRange.p3 - lumaRange.p2) : 15
            function setLuminosity(from, to, softness, inverse) { engine.setLuminosityRange(from / 100, to / 100, softness / 100, inverse) }
            property string section: luminosity ? "luminosity" : live.shapes.length === 0 ? "range" : "tone"
            property bool advancedRanges: false
            readonly property real hueWidth: hueRange.active ? 360 * (hueRange.inverse ? 1 - hueRange.p3 + hueRange.p0 : hueRange.p2 - hueRange.p1) : 360
            readonly property real hueSoftness: hueRange.active ? 180 * (hueRange.p1 - hueRange.p0 + hueRange.p3 - hueRange.p2) : 9
            property bool renaming: false
            Connections {
                target: engine.ai
                function onMaskCreated() {
                    if (!item.active) return
                    item.section = "shapes"
                    if (root.develop) root.develop.maskVisualsShown = true
                    root.revealRequested(item)
                }
            }
            width: parent.width
            spacing: 0
            Rectangle {
                width: parent.width; height: Theme.hRow + Theme.s1
                color: item.active ? Theme.controlBg : "transparent"
                Rectangle { anchors.left: parent.left; width: 2; height: parent.height; color: Theme.accent; visible: item.active }
                Icon {
                    anchors.left: parent.left; anchors.leftMargin: Theme.s3
                    anchors.verticalCenter: parent.verticalCenter
                    name: item.luminosity ? "sun" : item.live.shapes.length === 0 ? "pipette" : item.radial ? "circle-dot" : item.live.shape === 4 ? "pen-tool" : "diff"; size: 13; color: item.active ? Theme.accent : Theme.textMuted
                }
                Text {
                    visible: !item.renaming
                    anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 20
                    anchors.right: parent.right; anchors.rightMargin: 96   // clear of the row's buttons, as the rename field
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    text: item.live.shapes.length > 1
                          ? qsTr("%1 · %2 shapes").arg(item.live.name).arg(item.live.shapes.length)
                          : item.live.name
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; font.weight: Theme.wHeading
                    color: item.live.enabled ? Theme.textPrimary : Theme.textMuted
                }
                // Rename in place: the pencil opens a field over the name.
                SearchField {
                    id: localName
                    visible: item.renaming
                    anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 20
                    anchors.right: parent.right; anchors.rightMargin: 96
                    anchors.verticalCenter: parent.verticalCenter
                    placeholder: qsTr("Name this local")
                    tip: qsTr("The name History and the list show for this local, such as Sky or Face.")
                    live: false
                    onAccepted: t => { if (t.trim() !== "") engine.renameLocal(item.live.priority, t); item.renaming = false }
                    onActiveChanged: if (!active) item.renaming = false
                }
                Row {
                    anchors.right: parent.right; anchors.rightMargin: Theme.s2
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.s1
                    IconButton {
                        iconName: "pencil"; text: qsTr("Rename this local")
                        tip: qsTr("Give it a name that says what it is for.")
                        onClicked: { localName.text = item.live.name; item.renaming = true; localName.focusInput() }
                    }
                    IconButton {
                        objectName: "resetLocal"
                        iconName: "rotate-ccw"; text: qsTr("Reset this local's sliders")
                        tip: qsTr("Puts exposure, black and every slider under this mask back to zero. The mask itself stays as it is.")
                        enabled: root.localKeys.some(k => Math.abs(item.live[k] || 0) > 1e-6)
                        onClicked: root.resetLocal(item.live)
                    }
                    IconButton { iconName: "trash-2"; text: qsTr("Remove this local"); tip: qsTr("Takes the mask and every slider under it off the photo."); onClicked: engine.removeLocal(item.live.priority) }
                    EyeToggle { anchors.verticalCenter: parent.verticalCenter; checked: item.live.enabled; text: qsTr("this masked adjustment"); tip: qsTr("Shows or hides everything this mask does, together."); onClicked: engine.setLocalEnabled(item.live.priority, !item.live.enabled) }
                }
                TapHandler { onTapped: engine.activeLocal = item.live.priority }
            }
            Column {
                visible: item.active
                width: parent.width
                spacing: Theme.s1
                AccordionHeader {
                    objectName: "maskSection_luminosity"; width: parent.width; text: qsTr("Luminosity range")
                    visible: item.luminosity
                    open: item.section === "luminosity"
                    onClicked: item.section = open ? "" : "luminosity"
                }
                Column {
                    objectName: "maskControls_luminosity"; width: parent.width; spacing: Theme.s1
                    visible: item.luminosity && item.section === "luminosity"
                    SliderField {
                        objectName: "luminosityFrom"; x: Theme.s3; width: parent.width - Theme.s3 * 2
                        label: qsTr("From"); from: 0; to: 100; stepSize: 1; decimals: 0; value: item.lumaFrom
                        tip: qsTr("The darkest tone fully in the mask. 0 includes the deepest shadows.")
                        onEditingFinished: v => item.setLuminosity(Math.min(v, item.lumaTo), item.lumaTo, item.lumaSoftness, item.lumaRange.inverse === true)
                    }
                    SliderField {
                        objectName: "luminosityTo"; x: Theme.s3; width: parent.width - Theme.s3 * 2
                        label: qsTr("To"); from: 0; to: 100; stepSize: 1; decimals: 0; value: item.lumaTo
                        tip: qsTr("The brightest tone fully in the mask. 100 includes the brightest highlights.")
                        onEditingFinished: v => item.setLuminosity(item.lumaFrom, Math.max(v, item.lumaFrom), item.lumaSoftness, item.lumaRange.inverse === true)
                    }
                    SliderField {
                        objectName: "luminositySoftness"; x: Theme.s3; width: parent.width - Theme.s3 * 2
                        label: qsTr("Softness"); from: 0; to: 50; stepSize: 1; decimals: 0; value: item.lumaSoftness
                        tip: qsTr("Fade the mask gently into the neighbouring tones.")
                        onEditingFinished: v => item.setLuminosity(item.lumaFrom, item.lumaTo, v, item.lumaRange.inverse === true)
                    }
                    CheckField {
                        objectName: "luminosityInvert"; x: Theme.s3
                        text: qsTr("Invert"); checked: item.lumaRange.inverse === true
                        tip: qsTr("Select every tone except this range.")
                        onClicked: item.setLuminosity(item.lumaFrom, item.lumaTo, item.lumaSoftness, checked)
                    }
                }
                AccordionHeader {
                    objectName: "maskSection_range"; width: parent.width; text: qsTr("Colour range")
                    open: item.section === "range"
                    onClicked: item.section = open ? "" : "range"
                }
                Column {
                    objectName: "maskControls_range"; width: parent.width; spacing: Theme.s1
                    visible: item.section === "range"
                    ToolButton {
                        objectName: "sampleLocalColour"; x: Theme.s3
                        iconName: "pipette"; text: qsTr("Sample colour…"); showLabel: true
                        enabled: !engine.busy && root.develop !== null
                        onClicked: root.develop.startColourRange(false)
                    }
                    SliderField {
                        objectName: "colourRangeWidth"; x: Theme.s3; width: parent.width - Theme.s3 * 2
                        label: qsTr("Range width"); from: 1; to: 360; stepSize: 1; decimals: 0; suffix: "°"
                        enabled: item.hueRange.active === true; value: item.hueWidth
                        tip: qsTr("How much of the neighbouring hues to include around the sampled colour.")
                        onEditingFinished: v => engine.setColourRange(v, item.hueSoftness)
                    }
                    SliderField {
                        objectName: "colourRangeSoftness"; x: Theme.s3; width: parent.width - Theme.s3 * 2
                        label: qsTr("Softness"); from: 0; to: 90; stepSize: 1; decimals: 0; suffix: "°"
                        enabled: item.hueRange.active === true; value: item.hueSoftness
                        tip: qsTr("Fade the colour selection gently into neighbouring hues.")
                        onEditingFinished: v => engine.setColourRange(item.hueWidth, v)
                    }
                    CheckField {
                        objectName: "advancedColourRanges"; x: Theme.s3
                        text: qsTr("Refine individual ranges"); checked: item.advancedRanges
                        onClicked: item.advancedRanges = checked
                    }
                    Column {
                        visible: item.advancedRanges; width: parent.width; spacing: Theme.s1
                Row {
                    x: Theme.s3; spacing: Theme.s2
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("RANGE"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsRuler
                        font.letterSpacing: 0.6; color: Theme.textMuted
                    }
                    IconButton {
                        iconName: "flip-vertical"
                        text: item.live.maskInverted ? qsTr("Whole mask inverted, click to un-invert") : qsTr("Invert the whole mask")
                        tip: qsTr("Applies the local everywhere except where the shapes and ranges say.")
                        iconColor: item.live.maskInverted ? Theme.accent : Theme.textSecondary
                        onClicked: engine.setMaskInverted(!item.live.maskInverted)
                    }
                }
                Repeater {
                    model: rangeList.model
                    Column {
                        id: rangeItem
                        objectName: "localRange_" + live.channel
                        required property int index
                        required property var modelData
                        readonly property var live: (item.live.ranges || [])[index] || modelData
                        readonly property var labels: ({ luma: qsTr("Luminance"), hue: qsTr("Hue"), chroma: qsTr("Colour") })
                        width: parent ? parent.width : 0
                        spacing: 0
                        function push(active, inverse, a, b, c, d) {
                            engine.setRange(rangeItem.live.channel, active, inverse, a, b, c, d)
                        }
                        Rectangle {
                            width: parent.width; height: Theme.hRow
                            color: "transparent"
                            Text {
                                anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: rangeItem.labels[rangeItem.live.name]
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
                                color: rangeItem.live.active ? Theme.textPrimary : Theme.textMuted
                            }
                            Row {
                                anchors.right: parent.right; anchors.rightMargin: Theme.s2
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.s1
                                IconButton {
                                    iconName: "pipette"
                                    readonly property bool picking: root.develop ? root.develop.pickChannel === rangeItem.live.channel : false
                                    text: picking ? qsTr("Click the picture to set the band, click here to stop") : qsTr("Pick the band from a spot on the picture")
                                    tip: qsTr("Sets the range around the value found under your click.")
                                    iconColor: picking ? Theme.accent : Theme.textSecondary
                                    onClicked: if (root.develop) root.develop.pickChannel = picking ? -1 : rangeItem.live.channel
                                }
                                IconButton {
                                    iconName: "flip-vertical"
                                    text: rangeItem.live.inverse ? qsTr("Band inverted, click to un-invert") : qsTr("Keep everything outside the band instead")
                                    tip: qsTr("Swaps which side of the range the local applies to.")
                                    iconColor: rangeItem.live.inverse ? Theme.accent : Theme.textSecondary
                                    enabled: rangeItem.live.active
                                    onClicked: rangeItem.push(true, !rangeItem.live.inverse, rangeItem.live.p0, rangeItem.live.p1, rangeItem.live.p2, rangeItem.live.p3)
                                }
                                Toggle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    checked: rangeItem.live.active
                                    text: qsTr("Range")
                                    tip: qsTr("Limits the local to a band of this channel, on top of the shapes.")
                                    // Switching on with the band still wide open would do
                                    // nothing visible, so start from a middle band.
                                    onClicked: rangeItem.live.active
                                               ? rangeItem.push(false, false, rangeItem.live.p0, rangeItem.live.p1, rangeItem.live.p2, rangeItem.live.p3)
                                               : rangeItem.push(true, rangeItem.live.inverse,
                                                                rangeItem.live.p3 >= 1 && rangeItem.live.p0 <= 0 ? 0.0 : rangeItem.live.p0,
                                                                rangeItem.live.p3 >= 1 && rangeItem.live.p0 <= 0 ? 0.05 : rangeItem.live.p1,
                                                                rangeItem.live.p3 >= 1 && rangeItem.live.p0 <= 0 ? 0.45 : rangeItem.live.p2,
                                                                rangeItem.live.p3 >= 1 && rangeItem.live.p0 <= 0 ? 0.6 : rangeItem.live.p3)
                                }
                            }
                        }
                        Column {
                            visible: rangeItem.live.active
                            width: parent.width
                            spacing: 0
                            SliderField {
                                x: Theme.s3 + 24; width: parent.width - Theme.s3 * 2 - 24
                                label: qsTr("From"); from: 0; to: 1; stepSize: 0.01; decimals: 2
                                tip: qsTr("Where the band starts to apply in full.")
                                value: rangeItem.live.p1
                                onEditingFinished: v => rangeItem.push(true, rangeItem.live.inverse, Math.min(rangeItem.live.p0, v), v,
                                                                      Math.max(v, rangeItem.live.p2), Math.max(v, rangeItem.live.p3))
                            }
                            SliderField {
                                x: Theme.s3 + 24; width: parent.width - Theme.s3 * 2 - 24
                                label: qsTr("To"); from: 0; to: 1; stepSize: 0.01; decimals: 2
                                tip: qsTr("Where the band stops applying in full.")
                                value: rangeItem.live.p2
                                onEditingFinished: v => rangeItem.push(true, rangeItem.live.inverse, rangeItem.live.p0, Math.min(rangeItem.live.p1, v),
                                                                      v, Math.max(v, rangeItem.live.p3))
                            }
                            SliderField {
                                x: Theme.s3 + 24; width: parent.width - Theme.s3 * 2 - 24
                                label: qsTr("Falloff"); from: 0; to: 0.5; stepSize: 0.01; decimals: 2
                                tip: qsTr("How softly the band fades out on both sides.")
                                value: Math.max(rangeItem.live.p1 - rangeItem.live.p0, rangeItem.live.p3 - rangeItem.live.p2)
                                onEditingFinished: v => rangeItem.push(true, rangeItem.live.inverse, Math.max(0, rangeItem.live.p1 - v), rangeItem.live.p1,
                                                                      rangeItem.live.p2, Math.min(1, rangeItem.live.p2 + v))
                            }
                        }
                    }
                }

                // ── edge: soften or tighten the finished mask ───────────
                Item { width: 1; height: Theme.s1 }
                    }

                }
                AdjustmentGroup {
                    owner: item; key: "colour"; title: qsTr("Colour")
                    fields: [
                        {key:"hue", label:qsTr("Hue shift"), from:-180, to:180, step:.5, decimals:1, suffix:"°", tip:qsTr("Shift the selected colours around the colour wheel.")},
                        {key:"saturation", label:qsTr("Saturation"), from:-100, to:100, factor:100, tip:qsTr("Strength of colour inside the mask.")},
                        {key:"lightness", label:qsTr("Lightness"), from:-100, to:100, factor:100, tip:qsTr("Make the selected colours lighter or darker.")},
                        {key:"warmth", label:qsTr("Warmth"), from:-100, to:100, tip:qsTr("Cooler to the left, warmer to the right. A local colour correction, independent of the photo's white balance.")},
                        {key:"tint", label:qsTr("Tint"), from:-100, to:100, tip:qsTr("Green to the left, magenta to the right, within the mask.")},
                        {key:"vibrance", label:qsTr("Vibrance"), from:-100, to:100, factor:100, tip:qsTr("Boost muted colours more than strong colours inside the mask.")}
                    ]
                }
                AdjustmentGroup {
                    owner: item; key: "tone"; title: qsTr("Tone")
                    fields: [
                        {key:"exposure",label:qsTr("Exposure"),from:-3,to:3,step:.05,decimals:2,suffix:" EV",tip:qsTr("Brighten or darken the area selected by this mask.")},
                        {key:"black",label:qsTr("Black"),from:-.1,to:.1,step:.002,decimals:3,tip:qsTr("Set the black level inside the mask.")},
                        {key:"contrast",label:qsTr("Contrast"),from:-100,to:100,factor:100},
                        {key:"highlights",label:qsTr("Highlights"),from:-100,to:100,factor:100},
                        {key:"shadows",label:qsTr("Shadows"),from:-100,to:100,factor:100}
                    ]
                }
                AdjustmentGroup {
                    owner: item; key: "detail"; title: qsTr("Detail")
                    fields: [
                        {key:"clarity",label:qsTr("Clarity"),from:-100,to:100,factor:100,tip:qsTr("Soften or strengthen local texture inside the mask.")},
                        {key:"sharpness",label:qsTr("Sharpness"),from:0,to:2,step:.05,decimals:2,tip:qsTr("Strengthen fine edges inside the mask.")},
                        {key:"moire",label:qsTr("Colour moiré radius"),from:0,to:100,step:.5,decimals:1,suffix:" px",tip:qsTr("Suppress coloured moiré patterns within the mask. Larger radii affect broader patterns.")}
                    ]
                }
                AccordionHeader {
                    objectName: "maskSection_shapes"; width: parent.width; text: qsTr("Shapes & overlay")
                    open: item.section === "shapes"
                    onClicked: item.section = open ? "" : "shapes"
                }
                Column {
                    objectName: "maskControls_shapes"; width: parent.width; spacing: Theme.s1
                    visible: item.section === "shapes"
                Flow {
                    x: Theme.s3; width: parent.width - Theme.s3 * 2; spacing: Theme.s1
                    IconButton { iconName: "circle-dot"; text: qsTr("Add a radial to this mask"); tip: qsTr("Another ellipse combined with the shapes already here."); onClicked: { root.prepareShape(); engine.addShape(1) } }
                    IconButton { iconName: "diff"; text: qsTr("Add a gradient to this mask"); tip: qsTr("Another gradient combined with the shapes already here."); onClicked: { root.prepareShape(); engine.addShape(2) } }
                    IconButton { objectName: "addPenShape"; iconName: "pen-tool"; text: qsTr("Add a pen shape to this mask"); enabled: root.develop !== null; onClicked: root.develop.startPen(false) }
                    IconButton {
                        iconName: "brush"; text: qsTr("Paint a brush stroke on the picture")
                        tip: qsTr("Then drag on the picture; each stroke becomes a shape in this mask.")
                        iconColor: root.develop && root.develop.brushMode ? Theme.accent : Theme.textSecondary
                        onClicked: if (root.develop) root.develop.brushMode = !root.develop.brushMode
                    }
                    IconButton {
                        objectName: "toggleMaskCoverage"
                        iconName: "eye"
                        text: root.coverageVisible ? qsTr("Hide mask colour overlay") : qsTr("Show what the mask covers")
                        tip: qsTr("Paints a colour over the picture where this local applies.")
                        iconColor: root.coverageVisible ? Theme.accent : Theme.textSecondary
                        onClicked: {
                            const show = !root.coverageVisible
                            if (show && root.develop) root.develop.maskVisualsShown = true
                            engine.maskShown = show
                        }
                    }
                    IconButton { iconName: "copy"; text: qsTr("Copy this local"); shortcut: "Ctrl+Shift+C"; tip: qsTr("Mask, ranges and sliders together, to paste onto another photo."); onClicked: engine.copyMask() }
                    IconButton {
                        iconName: "stamp"; text: qsTr("Paste the copied local onto this photo"); shortcut: "Ctrl+Shift+V"
                        tip: qsTr("Adds the copied local, with its mask and sliders, to this photo.")
                        enabled: engine.hasMaskClipboard
                        onClicked: engine.pasteMask()
                    }
                }
                // Colour and strength of the overlay, as the brief asks.
                Row {
                    visible: engine.maskShown
                    x: Theme.s3 + 12; spacing: Theme.s1
                    Repeater {
                        model: ["#ff3b30", "#34c759", "#0a84ff", "#ffffff"]
                        Rectangle {
                            required property var modelData
                            width: 16; height: 16; radius: 3
                            color: modelData
                            border.width: engine.maskColour === modelData ? 2 : 1
                            border.color: engine.maskColour === modelData ? Theme.accent : Theme.border
                            TapHandler { onTapped: engine.maskColour = parent.modelData }
                        }
                    }
                }
                SliderField {
                    visible: engine.maskShown
                    x: Theme.s3 + 12; width: parent.width - Theme.s3 * 2 - 12
                    label: qsTr("Overlay strength"); from: 0.1; to: 1; stepSize: 0.05; decimals: 2
                    tip: qsTr("How solid the mask overlay is painted; it never affects the photo.")
                    value: engine.maskStrength
                    onEdited: v => engine.maskStrength = v
                }
                Column {
                    visible: root.develop ? root.develop.brushMode : false
                    width: parent.width
                    spacing: 0
                    SliderField {
                        x: Theme.s3; width: parent.width - Theme.s3 * 2
                        label: qsTr("Brush size"); from: 0.005; to: 0.3; stepSize: 0.005; decimals: 3
                        tip: qsTr("Width of the next stroke, as a fraction of the picture.")
                        value: root.develop ? root.develop.brushSize : 0.05
                        onEdited: v => { if (root.develop) root.develop.brushSize = v }
                    }
                    SliderField {
                        x: Theme.s3; width: parent.width - Theme.s3 * 2
                        label: qsTr("Brush hardness"); from: 0.05; to: 1; stepSize: 0.05; decimals: 2
                        tip: qsTr("How sharp the next stroke's edge is; lower feathers it.")
                        value: root.develop ? root.develop.brushHardness : 0.6
                        onEdited: v => { if (root.develop) root.develop.brushHardness = v }
                    }
                    SliderField {
                        x: Theme.s3; width: parent.width - Theme.s3 * 2
                        label: qsTr("Brush flow"); from: 0.05; to: 1; stepSize: 0.05; decimals: 2
                        tip: qsTr("How strongly the next stroke applies; lower builds up over passes.")
                        value: root.develop ? root.develop.brushFlow : 1
                        onEdited: v => { if (root.develop) root.develop.brushFlow = v }
                    }
                    Text {
                        x: Theme.s3; width: parent.width - Theme.s3 * 2; wrapMode: Text.WordWrap
                        text: qsTr("Drag on the picture to lay a stroke.")
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                    }
                }
                Repeater {
                    model: shapeList.model
                    Column {
                        id: shapeItem
                        required property int index
                        required property var modelData
                        readonly property var live: (item.live.shapes || [])[index] || modelData
                        readonly property bool activeShape: engine.activeShape === index
                        readonly property bool isRadial: live.shape === 1
                        readonly property bool isGradient: live.shape === 2
                        readonly property bool isBrush: live.shape === 3
                        readonly property bool isPen: live.shape === 4
                        readonly property bool on: live.enabled !== false
                        readonly property string kind: isRadial ? qsTr("Radial") : isBrush ? qsTr("Brush") : isPen ? qsTr("Pen") : qsTr("Gradient")
                        property bool renaming: false
                        // A delegate is parented after it is built, so the
                        // binding must survive a null parent.
                        width: parent ? parent.width : 0
                        spacing: 0
                        Rectangle {
                            width: parent.width; height: Theme.hRow
                            color: shapeItem.activeShape ? Theme.controlBg : "transparent"
                            Icon {
                                anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 12
                                anchors.verticalCenter: parent.verticalCenter
                                name: shapeItem.isRadial ? "circle-dot" : shapeItem.isBrush ? "brush" : shapeItem.isPen ? "pen-tool" : "diff"
                                size: 12; color: shapeItem.activeShape ? Theme.accent : Theme.textMuted
                                opacity: shapeItem.on ? 1 : 0.4
                            }
                            Text {
                                visible: !shapeItem.renaming
                                anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 30
                                anchors.right: parent.right; anchors.rightMargin: 120
                                anchors.verticalCenter: parent.verticalCenter
                                textFormat: Text.PlainText
                                text: shapeItem.live.name ? shapeItem.live.name : shapeItem.kind
                                elide: Text.ElideRight
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
                                font.strikeout: !shapeItem.on
                                color: !shapeItem.on ? Theme.textMuted : shapeItem.activeShape ? Theme.textPrimary : Theme.textSecondary
                            }
                            SearchField {
                                id: shapeName
                                visible: shapeItem.renaming
                                anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 30
                                anchors.right: parent.right; anchors.rightMargin: 120
                                anchors.verticalCenter: parent.verticalCenter
                                placeholder: shapeItem.kind
                                tip: qsTr("A name for this shape in the list.")
                                live: false
                                onAccepted: t => { engine.renameShape(shapeItem.index, t.trim()); shapeItem.renaming = false }
                                onActiveChanged: if (!active) shapeItem.renaming = false
                            }
                            Row {
                                anchors.right: parent.right; anchors.rightMargin: Theme.s2
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.s1
                                // Nothing sits under the first shape, so it has no operator.
                                IconButton {
                                    visible: shapeItem.index > 0
                                    iconName: root.combineIcons[shapeItem.live.combine]
                                    text: qsTr("Combine: %1").arg(root.combineNames[shapeItem.live.combine])
                                    tip: qsTr("How this shape joins the ones above it: Add, Intersect, Subtract or Exclude. Click for the next.")
                                    iconColor: shapeItem.live.combine === 0 ? Theme.textSecondary : Theme.accent
                                    onClicked: engine.setShapeCombine(shapeItem.index, (shapeItem.live.combine + 1) % 4, shapeItem.live.inverse)
                                }
                                IconButton {
                                    iconName: "flip-vertical"
                                    text: shapeItem.live.inverse ? qsTr("Inverted, click to un-invert") : qsTr("Invert this shape")
                                    tip: qsTr("Swaps inside and outside for this shape alone.")
                                    iconColor: shapeItem.live.inverse ? Theme.accent : Theme.textSecondary
                                    onClicked: engine.setShapeCombine(shapeItem.index, shapeItem.live.combine, !shapeItem.live.inverse)
                                }
                                IconButton {
                                    iconName: shapeItem.on ? "eye" : "eye-off"
                                    text: shapeItem.on ? qsTr("Bypass this shape") : qsTr("Bypassed, click to switch it back on")
                                    tip: qsTr("Leaves the shape out of the mask; it keeps its place and settings.")
                                    iconColor: shapeItem.on ? Theme.textSecondary : Theme.accent
                                    onClicked: engine.setShapeEnabled(shapeItem.index, !shapeItem.on)
                                }
                                IconButton {
                                    iconName: "trash-2"
                                    // The last drawn shape can only go when a range is
                                    // left holding the mask together.
                                    text: qsTr("Remove this shape")
                                    tip: qsTr("Deletes it from the mask; the last shape can only go when a range holds the mask.")
                                    enabled: item.live.shapes.length > 1
                                             || item.live.ranges.some(r => r.active)
                                    onClicked: engine.removeShape(shapeItem.index)
                                }
                            }
                            TapHandler { onTapped: engine.activeShape = shapeItem.index }
                        }
                        Column {
                            visible: shapeItem.activeShape
                            width: parent.width
                            spacing: 0
                            // Housekeeping: order in the combine, a copy, a name.
                            Row {
                                x: Theme.s3 + 12; spacing: Theme.s1
                                IconButton {
                                    iconName: "chevron-up"; text: qsTr("Move up")
                                    tip: qsTr("Shapes combine top to bottom; this one combines earlier.")
                                    enabled: shapeItem.index > 0
                                    onClicked: engine.moveShape(shapeItem.index, shapeItem.index - 1)
                                }
                                IconButton {
                                    iconName: "chevron-down"; text: qsTr("Move down")
                                    tip: qsTr("Shapes combine top to bottom; this one combines later.")
                                    enabled: shapeItem.index < item.live.shapes.length - 1
                                    onClicked: engine.moveShape(shapeItem.index, shapeItem.index + 1)
                                }
                                IconButton { iconName: "copy"; text: qsTr("Duplicate this shape"); tip: qsTr("A copy with the same settings, just beside it."); onClicked: engine.duplicateShape(shapeItem.index) }
                                IconButton {
                                    iconName: "pencil"; text: qsTr("Name this shape")
                                    tip: qsTr("Give it a name that says what it covers.")
                                    onClicked: { shapeName.text = shapeItem.live.name || ""; shapeItem.renaming = true; shapeName.focusInput() }
                                }
                            }
                            SliderField {
                                visible: shapeItem.isRadial
                                x: Theme.s3 + 12; width: parent.width - Theme.s3 * 2 - 12
                                label: qsTr("Radius"); from: 0.02; to: 1; stepSize: 0.01; decimals: 2
                                tip: qsTr("Size of the ellipse, as a fraction of the picture; drag its ring on the picture too.")
                                value: shapeItem.live.radius
                                onEditingFinished: v => engine.setShape(shapeItem.index, shapeItem.live.cx, shapeItem.live.cy, v, shapeItem.live.border, 0, shapeItem.live.opacity)
                            }
                            SliderField {
                                objectName: "shapeMaskFeather"
                                visible: shapeItem.isRadial || shapeItem.isPen
                                x: Theme.s3 + 12; width: parent.width - Theme.s3 * 2 - 12
                                stacked: true
                                label: qsTr("Feather"); from: 0; to: 100; stepSize: 0.01; decimals: 2; suffix: "%"
                                responsePower: 4
                                tip: qsTr("Width of the soft edge, as a percentage of the photo's short side. The start of the slider gives fine control; 0.10% is 5 pixels on a 5000-pixel short side.")
                                value: shapeItem.live.border * 100
                                onEditingFinished: v => engine.setShape(shapeItem.index, shapeItem.live.cx, shapeItem.live.cy, shapeItem.isPen ? v / 100 : shapeItem.live.radius, v / 100, 0, shapeItem.live.opacity)
                            }
                            Column {
                                visible: shapeItem.isPen
                                x: Theme.s3 + 12; width: parent.width - Theme.s3 * 2 - 12; spacing: Theme.s2
                                Text { width: parent.width; wrapMode: Text.WordWrap; text: qsTr("Drag points or curve handles. Drag the centre cross to move the shape. Double-click an edge to add a point; Alt-drag a handle to move it independently."); font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted }
                                Flow {
                                    width: parent.width; spacing: Theme.s1
                                    readonly property var editor: root.develop ? root.develop.penEditor : null
                                    enabled: editor && !editor.drawing && editor.selectedNode >= 0
                                    ToolButton { text: qsTr("Smooth"); onClicked: parent.editor.smoothPoint() }
                                    ToolButton { text: qsTr("Corner"); onClicked: parent.editor.cornerPoint() }
                                    ToolButton { text: qsTr("Delete point"); enabled: parent.enabled && parent.editor.nodes.length > 3; onClicked: parent.editor.removePoint() }
                                }
                            }
                            SliderField {
                                visible: shapeItem.isGradient
                                x: Theme.s3 + 12; width: parent.width - Theme.s3 * 2 - 12
                                label: qsTr("Rotation"); from: -180; to: 180; stepSize: 1; decimals: 0; suffix: "°"; origin: 0
                                tip: qsTr("Which way the gradient runs.")
                                value: shapeItem.live.rotation
                                onEditingFinished: v => engine.setShape(shapeItem.index, shapeItem.live.cx, shapeItem.live.cy, shapeItem.live.compression, 0, v, shapeItem.live.opacity)
                            }
                            SliderField {
                                visible: shapeItem.isGradient
                                x: Theme.s3 + 12; width: parent.width - Theme.s3 * 2 - 12
                                label: qsTr("Softness"); from: 0.01; to: 1; stepSize: 0.01; decimals: 2
                                tip: qsTr("How wide the fade across the gradient is.")
                                value: shapeItem.live.compression
                                onEditingFinished: v => engine.setShape(shapeItem.index, shapeItem.live.cx, shapeItem.live.cy, v, 0, shapeItem.live.rotation, shapeItem.live.opacity)
                            }
                            SliderField {
                                visible: shapeItem.isBrush
                                x: Theme.s3 + 12; width: parent.width - Theme.s3 * 2 - 12
                                label: qsTr("Stroke width"); from: 0.005; to: 0.3; stepSize: 0.005; decimals: 3
                                tip: qsTr("Width of this stroke, as a fraction of the picture.")
                                value: shapeItem.live.size
                                onEditingFinished: v => engine.setBrush(shapeItem.index, v, 0, -1)
                            }
                            SliderField {
                                visible: shapeItem.isBrush
                                x: Theme.s3 + 12; width: parent.width - Theme.s3 * 2 - 12
                                label: qsTr("Hardness"); from: 0.05; to: 1; stepSize: 0.05; decimals: 2
                                tip: qsTr("How sharp this stroke's edge is; lower feathers it.")
                                value: shapeItem.live.hardness
                                onEditingFinished: v => engine.setBrush(shapeItem.index, 0, v, -1)
                            }
                            SliderField {
                                visible: shapeItem.isBrush
                                x: Theme.s3 + 12; width: parent.width - Theme.s3 * 2 - 12
                                label: qsTr("Flow"); from: 0; to: 1; stepSize: 0.05; decimals: 2
                                tip: qsTr("How strongly this stroke applies.")
                                value: shapeItem.live.flow === undefined ? 1 : shapeItem.live.flow
                                onEditingFinished: v => engine.setBrush(shapeItem.index, 0, 0, v)
                            }
                            SliderField {
                                x: Theme.s3 + 12; width: parent.width - Theme.s3 * 2 - 12
                                label: qsTr("Opacity"); from: 0; to: 1; stepSize: 0.05; decimals: 2
                                tip: qsTr("How much of the adjustment this shape lets through.")
                                value: shapeItem.live.opacity
                                onEditingFinished: v => engine.setShape(shapeItem.index, shapeItem.live.cx, shapeItem.live.cy,
                                                                       shapeItem.isPen ? shapeItem.live.border : shapeItem.isRadial ? shapeItem.live.radius : shapeItem.isBrush ? shapeItem.live.size : shapeItem.live.compression,
                                                                       shapeItem.isRadial ? shapeItem.live.border : shapeItem.isBrush ? shapeItem.live.hardness : 0,
                                                                       shapeItem.isGradient ? shapeItem.live.rotation : 0, v)
                            }
                        }
                    }
                }

                // ── range: narrow the mask by pixel value ───────────────
                Item { width: 1; height: Theme.s1 }

                }
                AccordionHeader {
                    objectName: "maskSection_edges"; width: parent.width; text: qsTr("Edge refinement")
                    open: item.section === "edges"
                    onClicked: item.section = open ? "" : "edges"
                }
                Column {
                    objectName: "maskControls_edges"; width: parent.width; spacing: Theme.s1
                    visible: item.section === "edges"
                Text {
                    x: Theme.s3
                    text: qsTr("EDGE"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsRuler
                    font.letterSpacing: 0.6; color: Theme.textMuted
                }
                Column {
                    id: edge
                    width: parent.width
                    spacing: 0
                    readonly property real blur: item.live.blur || 0
                    readonly property real feather: item.live.feather || 0
                    readonly property int guide: item.live.guide || 0
                    readonly property real mContrast: item.live.maskContrast || 0
                    readonly property real mBrightness: item.live.maskBrightness || 0
                    function push(b, f, g, c, br) { engine.setMaskRefine(b, f, g, c, br) }
                    SliderField {
                        x: Theme.s3 + 12; width: parent.width - Theme.s3 * 2 - 12
                        label: qsTr("Blur"); from: 0; to: 100; stepSize: 1; decimals: 0; suffix: " px"
                        tip: qsTr("Softens the whole mask's edge by this many pixels.")
                        value: edge.blur
                        onEditingFinished: v => edge.push(v, edge.feather, edge.guide, edge.mContrast, edge.mBrightness)
                    }
                    SliderField {
                        x: Theme.s3 + 12; width: parent.width - Theme.s3 * 2 - 12
                        label: qsTr("Refine edge"); from: 0; to: 250; stepSize: 1; decimals: 0; suffix: " px"
                        tip: qsTr("Snaps the mask's edge to edges in the picture within this distance.")
                        value: edge.feather
                        onEditingFinished: v => edge.push(edge.blur, v, edge.guide, edge.mContrast, edge.mBrightness)
                    }
                    Item {
                        visible: edge.feather > 0
                        width: parent.width; height: Theme.hRow + Theme.s1
                        Text {
                            anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 12; width: 60
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Follow")
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                        }
                        SegmentedControl {
                            anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 12 + 60
                            anchors.verticalCenter: parent.verticalCenter
                            labels: [qsTr("Picture"), qsTr("Result")]
                            tips: [qsTr("Edge refinement follows edges in the picture as it comes in."), qsTr("Edge refinement follows edges in the picture as this local leaves it.")]
                            currentIndex: edge.guide === 1 ? 1 : 0
                            onActivated: i => edge.push(edge.blur, edge.feather, i, edge.mContrast, edge.mBrightness)
                        }
                    }
                    Text {
                        visible: edge.feather > 0 && engine.maskShown
                        x: Theme.s3 + 12; width: parent.width - Theme.s3 * 2 - 12; wrapMode: Text.WordWrap
                        text: qsTr("Preview the selection to inspect the refined edge, then hide it to judge the correction.")
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                    }
                    SliderField {
                        x: Theme.s3 + 12; width: parent.width - Theme.s3 * 2 - 12
                        label: qsTr("Mask contrast"); from: -1; to: 1; stepSize: 0.05; decimals: 2; origin: 0
                        tip: qsTr("Makes the mask's soft parts harder or softer.")
                        value: edge.mContrast
                        onEditingFinished: v => edge.push(edge.blur, edge.feather, edge.guide, v, edge.mBrightness)
                    }
                    SliderField {
                        x: Theme.s3 + 12; width: parent.width - Theme.s3 * 2 - 12
                        label: qsTr("Mask brightness"); from: -1; to: 1; stepSize: 0.05; decimals: 2; origin: 0
                        tip: qsTr("Grows or shrinks the mask's coverage overall.")
                        value: edge.mBrightness
                        onEditingFinished: v => edge.push(edge.blur, edge.feather, edge.guide, edge.mContrast, v)
                    }
                }

                }
                Item { width: 1; height: Theme.s2 }
            }
        }
    }
}
