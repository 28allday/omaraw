pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

Column {
    id: root
    property bool embedded: false
    readonly property var operations: ["exposure", "toneequal", "colorbalancergb", "contrastntexture", "bilat", "sharpen", "atrous", "primaries"]
    property int selection: 0
    readonly property string operation: operations[selection]
    readonly property string path: engine.toolState.rasterFile || ""
    readonly property var binding: (engine.toolState.rasterTargets || []).find(t => t.op === root.operation) || null
    function apply(inverse, opacity) { engine.toolAction("rasterfile", "attach", {path: path, target: operation, inverse: inverse, opacity: opacity}) }
    width: parent ? parent.width : 320; spacing: Theme.s1
    FilePicker {
        id: chooser; title: qsTr("Import a selection mask")
        nameFilters: [qsTr("Mask images (*.png *.PNG *.tif *.tiff *.TIF *.TIFF *.jpg *.jpeg *.JPG *.JPEG)"), qsTr("All images (*)")]
        onAccepted: engine.toolAction("rasterfile", "import", {path: selectedFile.toString(), target: root.operation})
    }
    Text { visible: !root.embedded; x: Theme.s3; text: qsTr("External mask"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; font.weight: Theme.wHeading; font.capitalization: Font.AllUppercase; font.letterSpacing: 0.6; color: Theme.textSecondary }
    Text {
        x: Theme.s3; width: parent.width - Theme.s3 * 2; wrapMode: Text.WordWrap
        text: qsTr("White selects, black protects. Import a mask aligned to the uncropped, unrotated source; lens and crop changes follow automatically. A copy is stored with the engine library.")
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
    }
    ComboField {
        x: Theme.s3; width: parent.width - Theme.s3 * 2
        model: root.operations.map(op => Names.module(op, op)); currentIndex: root.selection
        tipTitle: qsTr("Apply mask to"); tip: qsTr("The selected adjustment will affect only the imported selection.")
        onActivated: i => root.selection = i
    }
    Flow {
        x: Theme.s3; width: parent.width - Theme.s3 * 2; spacing: Theme.s1
        ToolButton { iconName: "folder-open"; text: qsTr("Import…"); showLabel: true; enabled: engine.imageId >= 0; onClicked: chooser.open() }
        ToolButton { iconName: "link"; text: qsTr("Use mask"); showLabel: true; enabled: root.path !== "" && !root.binding; onClicked: root.apply(false, 1) }
        ToolButton { iconName: "unlink"; text: qsTr("Disconnect"); showLabel: true; enabled: root.binding !== null; onClicked: engine.toolAction("rasterfile", "detach", {target: root.operation}) }
        ToolButton { iconName: "flip-horizontal"; text: qsTr("Invert"); showLabel: true; checked: root.binding !== null && root.binding.inverse; enabled: root.binding !== null; onClicked: root.apply(!root.binding.inverse, root.binding.opacity) }
    }
    Text {
        x: Theme.s3; width: parent.width - Theme.s3 * 2; wrapMode: Text.WordWrap
        text: root.path === "" ? qsTr("No mask imported.") : root.binding ? qsTr("Connected to %1.").arg(Names.module(root.operation, root.operation)) : qsTr("Mask available. Press Use mask to connect this adjustment.")
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
    }
    SliderField {
        x: Theme.s3; width: parent.width - Theme.s3 * 2; visible: root.binding !== null
        label: qsTr("Opacity"); from: 0; to: 100; decimals: 0; suffix: " %"
        value: root.binding ? root.binding.opacity * 100 : 100
        onEditingFinished: v => { if(root.binding) root.apply(root.binding.inverse, v / 100) }
    }
}
