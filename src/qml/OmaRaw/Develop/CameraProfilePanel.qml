pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

Column {
    id: root
    objectName: "cameraProfilePanel"
    property bool advanced: false
    property var toggleAdvanced: null
    property bool additional: false
    property bool showUnavailable: false
    readonly property var state: engine.cameraProfileState
    readonly property var rows: (engine.paramsVersion, engine.paramsFor("colorin"))
    readonly property var input: rows.find(p => p.field === "type") || {}
    readonly property string calibration: Names.choice(((input.options || []).find(p => p.value === input.value) || {}).label || "")
    readonly property var profiles: state.profiles || []
    readonly property var compatible: profiles.filter(p => p.supported)
    readonly property var unavailable: profiles.filter(p => !p.supported)
    readonly property var details: state.details || {}
    readonly property var cameraLook: state.cameraLook || {}
    readonly property bool hasCameraLook: (cameraLook.values || []).length > 0
    readonly property string selectedKey: state.active ? details.source || "current" : state.filmActive ? "film" : ""
    readonly property var choices: {
        let result = [{key:"", name:qsTr("Automatic colour")}]
        if (state.filmActive) result.push({key:"film", name:qsTr("Current print stock")})
        const query = search.text.trim().toLocaleLowerCase()
        for (const p of compatible) {
            const label = p.included ? qsTr("Community colour · included") : p.name
            if (!query || label.toLocaleLowerCase().includes(query) || p.name.toLocaleLowerCase().includes(query) || p.key === selectedKey)
                result.push({key:p.key, name:label})
        }
        if (state.active && !result.some(p => p.key === selectedKey))
            result.push({key:selectedKey, name:details.included ? qsTr("Community colour · included") : details.name || qsTr("Saved camera profile")})
        return result
    }
    spacing: Theme.s1
    component Guide: Text {
        x: Theme.s3; width: parent.width - 2*Theme.s3
        textFormat: Text.PlainText; wrapMode: Text.WordWrap
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
    }
    Guide {
        objectName: "cameraProfileIdentity"
        text: root.state.camera || (engine.imageId < 0 ? qsTr("Open a photo to see its camera profiles.") : qsTr("Camera not identified"))
        color: Theme.textPrimary
    }
    Guide {
        visible: root.state.raw === true
        text: qsTr("Colour is calibrated for this camera automatically.")
    }
    ToolButton {
        objectName: "applyCameraLook"
        visible: root.hasCameraLook
        x: Theme.s3; showLabel: true; iconName: "camera"
        text: root.cameraLook.applied ? qsTr("Camera look applied") : qsTr("Apply camera look")
        enabled: engine.imageId >= 0 && root.state.raw === true && !engine.busy && !root.cameraLook.applied
        onClicked: engine.applyCameraLook()
    }
    Guide {
        visible: root.hasCameraLook
        text: qsTr("A contrast and saturation preset inspired by your camera’s JPEGs. Exposure, white balance and local edits stay. Undo restores your previous look.")
    }
    Guide { visible: root.state.raw === true; text: qsTr("Colour profile"); color: Theme.textPrimary }
    SearchField {
        id: search; objectName: "cameraProfileSearch"
        x: Theme.s3; width: parent.width - 2*Theme.s3
        visible: root.compatible.length > 6
        placeholder: qsTr("Search camera profiles")
    }
    ComboField {
        objectName: "cameraProfileChoice"
        x: Theme.s3; width: parent.width - 2*Theme.s3
        model: root.choices.map(p => p.name)
        currentIndex: root.choices.findIndex(p => p.key === root.selectedKey)
        enabled: engine.imageId >= 0 && root.state.raw === true && !engine.busy
        tipTitle: qsTr("Camera profile")
        tip: qsTr("Only profiles for this camera are shown. Automatic colour uses your regular tone controls. Other profiles supply their own colour and contrast and replace Print stock.")
        onActivated: i => {
            const p = root.choices[i]
            if (p && p.key !== "film" && p.key !== "current") engine.selectCameraProfile(p.key)
        }
    }
    Guide {
        text: root.state.missing ? qsTr("The saved profile tables are missing. Select or import the profile again before exporting.")
            : root.state.raw !== true ? qsTr("Camera profiles apply to RAW photos. A JPEG already contains its camera rendering.")
            : root.state.active && root.details.included ? qsTr("Included community colour and contrast, created by RawTherapee contributors. Replaces Print stock; your other adjustments stay.")
            : root.state.active ? qsTr("%1 supplies colour and contrast in place of Print stock. Your other adjustments stay.").arg(root.details.name || qsTr("This profile"))
            : root.compatible.some(p => p.included) ? qsTr("Community colour is an included alternative. No download or setup is needed.")
            : qsTr("Automatic colour is ready to use. You can add your own camera profiles below.")
    }
    ToolButton {
        objectName: "additionalCameraProfiles"
        x: Theme.s3; showLabel: true; text: qsTr("Additional profiles")
        iconName: root.additional ? "chevron-up" : "chevron-down"
        onClicked: root.additional = !root.additional
    }
    SlideSection {
        expanded: root.additional; width: parent.width
        Column {
            width: parent.width; spacing: Theme.s1
            Flow {
                x: Theme.s3; width: parent.width - 2*Theme.s3; spacing: Theme.s1
                ToolButton { objectName: "importCameraProfile"; text: qsTr("Import profile…"); iconName: "plus"; showLabel: true; onClicked: filePicker.open() }
                ToolButton { objectName: "cameraProfilesFolder"; text: qsTr("Profiles folder…"); iconName: "folder-open"; showLabel: true; onClicked: folderPicker.open() }
                ToolButton { objectName: "refreshCameraProfiles"; text: qsTr("Refresh"); iconName: "refresh-cw"; showLabel: true; onClicked: engine.refreshCameraProfiles() }
            }
            Guide { text: qsTr("Add DCP camera profiles you already own. Only matching, supported profiles appear in the list.") }
            ToolButton {
                visible: root.unavailable.length > 0; x: Theme.s3; showLabel: true
                text: qsTr("%1 profiles unavailable").arg(root.unavailable.length)
                iconName: root.showUnavailable ? "chevron-up" : "chevron-down"
                onClicked: root.showUnavailable = !root.showUnavailable
            }
            Guide {
                visible: root.showUnavailable && root.unavailable.length > 0
                text: root.unavailable.map(p => p.name + ": " + p.error).join("\n")
            }
        }
    }
    ToolButton {
        x: Theme.s3; text: qsTr("Advanced input colour"); showLabel: true
        iconName: root.advanced ? "chevron-up" : "chevron-down"
        onClicked: { if (root.toggleAdvanced) root.toggleAdvanced(); else root.advanced = !root.advanced }
    }
    SlideSection {
        expanded: root.advanced; width: parent.width
        Column {
            width: parent.width; spacing: Theme.s1
            Guide { objectName: "cameraCalibration"; text: root.calibration ? qsTr("Input colour: %1").arg(root.calibration) : "" }
            ModuleRows { width: parent.width; headings: false; rows: root.rows }
        }
    }
    FilePicker {
        id: filePicker; title: qsTr("Import a camera profile")
        nameFilters: [qsTr("Camera profiles (*.dcp *.DCP)")]
        onAccepted: engine.importCameraProfile(selectedFile.toString())
    }
    FolderPicker {
        id: folderPicker; title: qsTr("Choose camera profiles folder")
        description: qsTr("DCP files in this folder and its subfolders will be matched to each photo's camera.")
        onAccepted: backend.setCameraProfilesFolder(selectedFolder.toString())
    }
}
