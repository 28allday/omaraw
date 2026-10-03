import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

Column {
    id: root
    spacing: Theme.s2
    readonly property var denoise: engine.denoise
    property bool showAfter: true
    function reset() { compare.close(); root.denoise.reset(); root.showAfter = true }
    Text {
        x: Theme.s3; width: parent.width - 2*x; wrapMode: Text.WordWrap
        text: qsTr("Reduce noise in a Bayer or X-Trans RAW photo, then save an editable DNG copy. The sensor and model are chosen automatically. Apply creates and opens a DNG copy with your edits, stacked with the unchanged original.")
        color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
    }
    Column {
        x: Theme.s3; width: parent.width - 2*x; spacing: Theme.s2
        visible: !root.denoise.installed
        Text {
            width: parent.width; wrapMode: Text.WordWrap
            text: qsTr("AI denoise is included with OmaRAW. Its files are missing or damaged; reinstall the OmaRAW package to restore them. Photos stay on this computer.")
            color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        }
    }
    Column {
        x: Theme.s3; width: parent.width - 2*x; spacing: Theme.s2
        enabled: !root.denoise.busy
        Text {
            objectName: "aiDenoiseDetectedModel"
            width: parent.width; wrapMode: Text.WordWrap
            text: root.denoise.detectedSensor === "" ? qsTr("Sensor: auto-detect on preview or save")
                  : root.denoise.detectedSensor === "xtrans" ? qsTr("X-Trans detected · Restormer") : qsTr("Bayer detected · RawForge Heavy")
            color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
        }
        SliderField {
            objectName: "aiDenoiseStrength"; width: parent.width; stacked: true
            label: qsTr("Strength"); from: 0; to: 100; stepSize: 1; decimals: 0; suffix: " %"
            value: root.denoise.strength * 100
            tip: qsTr("Start around 60%. Use less to retain fine texture; check the preview at 100%.")
            onEditingFinished: v => root.denoise.strength = v / 100
        }
        CheckField {
            width: parent.width; text: qsTr("Use GPU when faster"); checked: root.denoise.backend === "auto"
            tip: qsTr("Tests Vulkan accuracy and speed, then chooses GPU or CPU automatically. Untick to use CPU.")
            onClicked: root.denoise.backend = root.denoise.backend === "auto" ? "cpu" : "auto"
        }
        SearchField {
            objectName: "aiDenoiseIsoOverride"; visible: root.denoise.detectedSensor !== "xtrans"
            width: parent.width; placeholder: qsTr("ISO override (blank uses photo)"); glyph: "camera"
            text: root.denoise.isoOverride > 0 ? root.denoise.isoOverride.toString() : ""
            tip: qsTr("Only needed for Bayer photos with missing ISO metadata. X-Trans does not use ISO conditioning.")
            onAccepted: value => { if (value.trim() === "") root.denoise.isoOverride = 0; else if (/^[0-9]+$/.test(value) && Number(value) <= 10000000) root.denoise.isoOverride = Number(value) }
        }
        Flow {
            width: parent.width; spacing: Theme.s1
            ToolButton { objectName: "previewAiDenoise"; text: qsTr("Preview denoise"); enabled: root.denoise.installed && engine.imageId >= 0 && !engine.busy; onClicked: { compare.open(); root.denoise.preview() } }
            ToolButton {
                objectName: "applyAiDenoise"; text: qsTr("Apply AI denoise")
                enabled: root.denoise.installed && engine.imageId >= 0 && !engine.busy
                tip: qsTr("Creates a DNG beside the original, keeps your edits and opens the new copy. The original stays unchanged.")
                onClicked: root.denoise.apply()
            }
            ToolButton { objectName: "saveAiDenoise"; text: qsTr("Save copy as…"); enabled: root.denoise.installed && engine.imageId >= 0 && !engine.busy; onClicked: saveDialog.open() }
        }
        ToolButton { visible: root.denoise.after !== ""; text: qsTr("Show comparison"); onClicked: compare.open() }
    }
    Text {
        x: Theme.s3; width: parent.width - 2*x; wrapMode: Text.WordWrap; textFormat: Text.PlainText
        text: root.denoise.status; visible: text !== ""
        color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
    }
    ToolButton { x: Theme.s3; visible: root.denoise.canCancel; text: qsTr("Cancel denoise"); onClicked: root.denoise.cancel() }
    FilePicker {
        id: saveDialog; title: qsTr("Save denoised DNG copy"); fileMode: PathPicker.SaveFile
        defaultSuffix: "dng"; selectedFile: engine.imagePath.replace(/\.[^/.]+$/, "") + "-denoised.dng"
        acceptLabel: qsTr("Save copy"); nameFilters: [qsTr("Digital negative (*.dng)")]
        onAccepted: root.denoise.saveCopy(selectedFile.toString())
    }
    ModalPanel {
        id: compare; objectName: "aiDenoiseComparison"
        width: Math.min(620, parent ? parent.width - 32 : 620)
        height: Math.min(830, parent ? parent.height - 32 : 830)
        contentItem: Flickable {
            clip: true; contentHeight: content.implicitHeight; boundsBehavior: Flickable.StopAtBounds
            Column {
                id: content; width: parent.width; spacing: Theme.s2
                Text { text: qsTr("AI denoise · detail comparison"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; color: Theme.textPrimary }
                Text {
                    width: parent.width; wrapMode: Text.WordWrap
                    text: qsTr("Your current colour and exposure adjustments are shown on both previews. Drag to inspect another area. Crop, masks and other detail effects are excluded. Choose 100% to judge texture.")
                    color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
                }
                SegmentedControl {
                    labels: [qsTr("Before"), qsTr("After")]; currentIndex: root.showAfter ? 1 : 0
                    onActivated: i => root.showAfter = i === 1
                }
                DenoisePreview {
                    width: parent.width; viewHeight: Math.min(width, compare.height * .48)
                    denoise: root.denoise; showAfter: root.showAfter
                    displayScale: backend.displayColour.windowScale
                    active: compare.opened
                }
                Text {
                    width: parent.width; wrapMode: Text.WordWrap; textFormat: Text.PlainText
                    text: root.denoise.status; visible: text !== ""
                    color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
                }
                SliderField {
                    width: parent.width; stacked: true; label: qsTr("Strength"); from: 0; to: 100; stepSize: 1; decimals: 0
                    enabled: !root.denoise.busy; value: root.denoise.strength * 100; onEditingFinished: v => root.denoise.strength = v / 100
                }
                Flow {
                    width: parent.width; spacing: Theme.s2
                    ToolButton {
                        objectName: "applyAiDenoiseComparison"; text: qsTr("Apply AI denoise")
                        enabled: root.denoise.installed && !root.denoise.busy && engine.imageId >= 0 && !engine.busy
                        onClicked: { compare.close(); root.denoise.apply() }
                    }
                    ToolButton { text: qsTr("Update preview"); enabled: !root.denoise.busy; onClicked: root.denoise.preview() }
                    ToolButton { text: root.denoise.canCancel ? qsTr("Cancel") : qsTr("Close"); onClicked: { if (root.denoise.canCancel) root.denoise.cancel(); compare.close() } }
                }
            }
        }
    }
}
