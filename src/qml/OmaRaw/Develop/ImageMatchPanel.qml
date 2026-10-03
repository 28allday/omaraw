pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

Column {
    id: root
    objectName: "imageMatchPanel"
    spacing: Theme.s2
    readonly property var match: engine.imageMatch
    readonly property var savedRows: (engine.paramsVersion, engine.paramsFor("omarawmatch"))
    readonly property bool editingApplied: savedRows.some(p => p.enabled || p.configured)
                                          && (!match.hasPreview || match.previewApplied)
    function savedValue(field, fallback) { const row = savedRows.find(p => p.field === field); return row ? row.value : fallback }
    property bool grainOpen: false
    property bool appliedOpen: false
    function option(key, value) { match.options = Object.assign({}, match.options, {[key]:value}) }
    function reset() { comparison.close(); match.reset() }
    Column {
        x: Theme.s3; width: parent.width - x * 2; spacing: Theme.s2
        Rectangle {
            width: parent.width; height: 132
            color: Theme.inputBg; border.color: drop.containsDrag ? Theme.accent : Theme.border
            Image {
                anchors.fill: parent; anchors.margins: Theme.s1; fillMode: Image.PreserveAspectFit
                source: root.match.referenceSource.replace("referenceDetail", "reference")
                cache: false; asynchronous: true
            }
            Text {
                anchors.centerIn: parent; width: parent.width - Theme.s4
                visible: !root.match.hasReference; horizontalAlignment: Text.AlignHCenter
                text: qsTr("Drop a reference photograph here\nJPEG, PNG, TIFF or WebP")
                color: Theme.textMuted; wrapMode: Text.WordWrap
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
            }
            DropArea {
                id: drop; objectName: "imageMatchDropArea"; anchors.fill: parent; enabled: !root.match.busy
                onDropped: event => { if (event.hasUrls && event.urls.length > 0) { root.match.setReference(event.urls[0].toString()); event.acceptProposedAction() } }
            }
        }
        Text {
            width: parent.width; text: root.match.referenceName; visible: text !== ""; elide: Text.ElideMiddle
            color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        }
        Flow {
            width: parent.width; spacing: Theme.s1; enabled: !root.match.busy
            ToolButton { objectName: "chooseImageMatchReference"; iconName: "folder-open"; text: root.match.hasReference ? qsTr("Replace…") : qsTr("Choose…"); showLabel: true; onClicked: referencePicker.open() }
            ToolButton { objectName: "useImageMatchCurrent"; iconName: "image"; text: qsTr("Use current edit"); showLabel: true; enabled: engine.imageId >= 0 && !engine.busy; onClicked: root.match.useCurrent() }
            ToolButton { objectName: "clearImageMatchReference"; iconName: "x"; text: qsTr("Clear reference"); enabled: root.match.hasReference; onClicked: root.match.clearReference() }
        }
        SegmentedControl {
            objectName: "imageMatchMode"; width: parent.width; enabled: !root.match.busy
            labels: [qsTr("Consistency"), qsTr("Creative Look")]
            currentIndex: root.match.options.mode === "creative" ? 1 : 0
            onActivated: i => root.option("mode", i === 1 ? "creative" : "consistency")
        }
        Text {
            width: parent.width; wrapMode: Text.WordWrap
            text: root.match.options.mode === "creative" ? qsTr("Transfer the reference's palette and contrast. Grain is optional.")
                  : qsTr("Bring a shoot into agreement on exposure, white balance, tone and colour.")
            color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        }
        Column {
            width: parent.width; spacing: Theme.s1; enabled: !root.match.busy
            Repeater {
                model: [{key:"exposure", field:"exposure_on", label:qsTr("Exposure")}, {key:"whiteBalance", field:"wb_on", label:qsTr("White balance")},
                    {key:"tone", field:"tone_on", label:qsTr("Tone / contrast")}, {key:"colour", field:"colour_on", label:qsTr("Colour")}, {key:"grain", field:"grain_on", label:qsTr("Grain")}]
                CheckField {
                    required property var modelData
                    objectName: "imageMatchToggle_" + modelData.key
                    width: parent.width; text: modelData.label
                    checked: root.editingApplied ? root.savedValue(modelData.field, 1) > .5 : root.match.options[modelData.key] === true
                    onClicked: {
                        if (root.editingApplied) { root.match.editApplied(modelData.field, checked ? 1 : 0); engine.endGesture() }
                        else root.option(modelData.key, checked)
                    }
                }
            }
            Repeater {
                model: [{key:"colourStrength", field:"colour_strength", toggle:"colour", label:qsTr("Colour strength")}, {key:"toneStrength", field:"tone_strength", toggle:"tone", label:qsTr("Tone strength")}, {key:"grainStrength", field:"grain_strength", toggle:"grain", label:qsTr("Grain strength")}]
                SliderField {
                    id: strength
                    required property var modelData
                    objectName: "imageMatchStrength_" + modelData.key
                    width: parent.width; stacked: true; label: modelData.label
                    from: 0; to: 100; stepSize: 1; decimals: 0; suffix: " %"
                    origin: 100; resetOnDoubleClick: true
                    value: (root.editingApplied ? root.savedValue(modelData.field, 1) : root.match.options[modelData.key]) * 100
                    enabled: root.editingApplied ? root.savedValue(modelData.toggle + "_on", 1) > .5 : root.match.options[modelData.toggle] === true
                    onEdited: value => { if (root.editingApplied) strengthEdit.push(value / 100) }
                    onEditingFinished: value => {
                        if (root.editingApplied) { strengthEdit.flush(value / 100); engine.endGesture() }
                        else root.option(modelData.key, value / 100)
                    }
                    EditCoalescer { id: strengthEdit; onSend: value => root.match.editApplied(strength.modelData.field, value) }
                }
            }
            CheckField {
                objectName: "imageMatchProtectSkin"
                width: parent.width; text: qsTr("Protect skin-like colours")
                checked: root.editingApplied ? root.savedValue("protect_skin", 1) > .5 : root.match.options.protectSkin !== false
                onClicked: {
                    if (root.editingApplied) { root.match.editApplied("protect_skin", checked ? 1 : 0); engine.endGesture() }
                    else root.option("protectSkin", checked)
                }
            }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: qsTr("Colour-based protection also affects similar colours in wood and other subjects. Turn it off to let those colours follow a stronger treatment.")
                color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
            }
        }
        Text {
            width: parent.width; wrapMode: Text.WordWrap
            text: root.editingApplied ? qsTr("Switches and strengths adjust the applied match on this photo. Switching an effect off keeps its settings. Creative strengths leave exposure and white balance unchanged; matching mode is used for the next preview.")
                                      : qsTr("Creative strengths leave exposure and white-balance corrections unchanged.")
            color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        }
        Flow {
            width: parent.width; spacing: Theme.s1
            ToolButton { objectName: "previewImageMatch"; text: qsTr("Preview match"); showLabel: true; iconName: "eye"; enabled: root.match.hasReference && !root.match.busy && engine.imageId >= 0 && !engine.busy; onClicked: { root.match.preview(); comparison.open() } }
            ToolButton { objectName: "applyImageMatch"; text: qsTr("Apply to photo"); showLabel: true; enabled: root.match.hasPreview && !root.match.busy && !engine.busy; onClicked: root.match.apply() }
            ToolButton { objectName: "applyImageMatchSelection"; text: qsTr("Match selection (%1)").arg(backend.selectedCount); showLabel: true; enabled: root.match.hasReference && !root.match.busy && !engine.busy && backend.selectedCount > 1; onClicked: root.match.applySelection(backend.exportItems(false)) }
            ToolButton { objectName: "compareImageMatch"; text: qsTr("Compare…"); showLabel: true; enabled: root.match.hasPreview; onClicked: comparison.open() }
            ToolButton { objectName: "undoImageMatchBatch"; text: qsTr("Undo last batch"); showLabel: true; visible: root.match.canUndoBatch; onClicked: root.match.undoBatch() }
            ToolButton { objectName: "cancelImageMatch"; text: qsTr("Stop matching"); showLabel: true; visible: root.match.busy; onClicked: root.match.cancel() }
        }
        Text {
            objectName: "imageMatchStatus"; width: parent.width; wrapMode: Text.WordWrap; textFormat: Text.PlainText
            text: root.match.status; visible: text !== ""
            color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        }
        Text {
            objectName: "imageMatchWarnings"; width: parent.width; wrapMode: Text.WordWrap; textFormat: Text.PlainText
            text: (root.match.report.warnings || []).join("\n"); visible: text !== ""
            color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        }
        ToolButton { objectName: "imageMatchGrainAdjustment"; text: qsTr("Grain adjustment"); showLabel: true; iconName: root.grainOpen ? "chevron-up" : "chevron-down"; enabled: root.editingApplied || root.match.hasPreview; onClicked: root.grainOpen = !root.grainOpen }
        Column {
            width: parent.width; spacing: Theme.s1; visible: root.grainOpen && (root.editingApplied || root.match.hasPreview); enabled: !root.match.busy
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: qsTr("Estimated from quiet areas at native resolution. Size is relative to a 3000-pixel long edge. Check at 100% before applying.")
                color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
            }
            Repeater {
                model: [{field:"grain_amount", label:qsTr("Amount"), max:32, min:0, factor:1},
                    {field:"grain_size", label:qsTr("Size"), max:12, min:.2, factor:1},
                    {field:"grain_roughness", label:qsTr("Roughness"), max:1, min:0, factor:100},
                    {field:"grain_colour", label:qsTr("Coloured grain"), max:1, min:0, factor:100},
                    {field:"grain_shadows", label:qsTr("Shadows"), max:2, min:0, factor:100},
                    {field:"grain_midtones", label:qsTr("Midtones"), max:2, min:0, factor:100},
                    {field:"grain_highlights", label:qsTr("Highlights"), max:2, min:0, factor:100}]
                SliderField {
                    id: grain
                    required property var modelData
                    objectName: "imageMatchDraft_" + modelData.field
                    width: parent.width; stacked: true; label: modelData.label
                    from: modelData.min * modelData.factor; to: modelData.max * modelData.factor
                    decimals: modelData.factor === 1 ? 2 : 0; stepSize: modelData.factor === 1 ? .01 : 1
                    value: (root.editingApplied ? root.savedValue(modelData.field, 0) : ((root.match.report.parameters || {})[modelData.field] || 0)) * modelData.factor
                    onEdited: value => { if (root.editingApplied) grainEdit.push(value / modelData.factor) }
                    onEditingFinished: value => {
                        if (root.editingApplied) { grainEdit.flush(value / modelData.factor); engine.endGesture() }
                        else root.match.editDraft(modelData.field, value / modelData.factor)
                    }
                    EditCoalescer { id: grainEdit; onSend: value => root.match.editApplied(grain.modelData.field, value) }
                }
            }
            ToolButton { objectName: "resetImageMatchGrain"; text: qsTr("Reset added grain"); showLabel: true; iconName: "rotate-ccw"; onClicked: { if (root.editingApplied) { root.match.editApplied("grain_amount", 0); engine.endGesture() } else root.match.editDraft("grain_amount", 0) } }
        }
        ToolButton { text: qsTr("Edit applied match"); showLabel: true; iconName: root.appliedOpen ? "chevron-up" : "chevron-down"; onClicked: root.appliedOpen = !root.appliedOpen }
        Text {
            width: parent.width; wrapMode: Text.WordWrap; visible: root.appliedOpen
            text: qsTr("These are saved photo adjustments. They also work without a reference and are included in Undo, presets and sidecars.")
            color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        }
        Column {
            width: parent.width; visible: root.appliedOpen; enabled: !root.match.busy; spacing: Theme.s1
            Repeater {
                model: [{field:"exposure_on", label:qsTr("Exposure correction enabled")}, {field:"wb_on", label:qsTr("White-balance correction enabled")},
                    {field:"tone_on", label:qsTr("Tone correction enabled")}, {field:"colour_on", label:qsTr("Colour correction enabled")}, {field:"grain_on", label:qsTr("Grain enabled")}]
                CheckField {
                    required property var modelData
                    width: parent.width; text: modelData.label
                    checked: ((root.savedRows.find(p => p.field === modelData.field) || {}).value || 0) > .5
                    onClicked: { root.match.editApplied(modelData.field, checked ? 1 : 0); engine.endGesture() }
                }
            }
        }
        ModuleRows {
            objectName: "imageMatchAppliedControls"; width: parent.width; visible: root.appliedOpen; headings: false; enabled: !root.match.busy
            rows: root.savedRows.filter(p => !p.internal && !["exposure_on", "wb_on", "tone_on", "colour_on", "grain_on"].includes(p.field))
        }
        Repeater {
            model: root.match.batchResults
            Text {
                required property var modelData
                width: parent.width; wrapMode: Text.WordWrap; textFormat: Text.PlainText
                text: modelData.path.split("/").pop() + ": " + (modelData.ok ? ((modelData.report || {}).warnings || []).join(" ") : modelData.error)
                visible: !modelData.ok || (((modelData.report || {}).warnings || []).length > 0)
                color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
            }
        }
    }
    FilePicker {
        id: referencePicker; objectName: "imageMatchReferencePicker"; title: qsTr("Choose Image Match reference")
        imagePreview: true
        nameFilters: [qsTr("Photographs (*.jpg *.jpeg *.png *.tif *.tiff *.webp)")]
        onAccepted: root.match.setReference(selectedFile.toString())
    }
    ImageMatchComparison { id: comparison; match: root.match }
}
