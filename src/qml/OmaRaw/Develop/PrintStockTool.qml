pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

ToolCard {
    id: root
    key: "omarawprint"; operation: key; tab: "Color"; title: qsTr("Print stock")
    tip: qsTr("Choose a film look for digital viewing, then refine its colour and tone. Grain and halation have their own tools.")
    readonly property var rows: (engine.paramsVersion, engine.paramsFor(operation))
    readonly property var stock: row("stock")
    readonly property var stockOptions: stock.options || []
    readonly property bool digitalCinema: stock.value === 23 || (stock.value >= 15 && stock.value <= 21)
    readonly property bool advanced: controller.advancedTools[key] === true
    readonly property var balanceFields: ["trim_r", "trim_g", "trim_b"]
    readonly property var contrastFields: ["gain_r", "gain_g", "gain_b"]
    readonly property var refinementFields: balanceFields.concat(contrastFields, digitalCinema ? ["white"] : ["white", "paper"])
    readonly property bool refinementsChanged: rows.some(p => refinementFields.includes(p.field) && Math.abs(p.value - p.def) > 1e-6)
    readonly property bool hasProfile: stock.value === 22 || (row("profile").text || "") !== ""
    available: rows.length > 0
    function row(field) { return rows.find(p => p.field === field) || {op:operation, field:field} }
    function ordered(fields) { return fields.map(field => rows.find(p => p.field === field)).filter(p => p !== undefined) }
    function stockDescription(value) {
        const n = value === 23 ? 0 : value >= 14 && value <= 21 ? value - 14 : value
        const looks = [qsTr("Cinema · Warm colour, rich shadows"), qsTr("Cinema · Strong contrast, deep blacks"),
            qsTr("Cinema · Gentle contrast, cooler colour"), qsTr("Cinema · Balanced contrast, cooler colour"),
            qsTr("Cinema · Crisp contrast, deep shadows"), qsTr("Cinema · Crisp contrast, clean blues"),
            qsTr("Cinema · Cooler magentas and greens"), qsTr("Cinema · Black and white"),
            qsTr("Photo film · Soft contrast, gentle colour"), qsTr("Photo film · Soft contrast, gentle colour"),
            qsTr("Photo film · Soft contrast, gentle colour"), qsTr("Photo film · Clean, vivid colour"),
            qsTr("Photo film · Warm colour"), qsTr("Photo film · Rich colour")]
        return n === 22 ? qsTr("Camera profile · %1").arg((engine.cameraProfileState.details || {}).name || qsTr("Imported look")) : looks[n] || qsTr("Film colour and tone")
    }
    function whiteRow(field) {
        const p = row(field)
        const labels = field === "white" ? [qsTr("Neutral (D65)"), qsTr("Slightly warm (D60)"), qsTr("Warm (D55)"), qsTr("Warmest (D50)")]
                                         : [qsTr("Bright whites (legacy)"), qsTr("Film-base whites (legacy)"), qsTr("Balanced print")]
        return Object.assign({}, p, {options:(p.options || []).map(o => Object.assign({}, o, {label:labels[o.value] || o.label}))})
    }
    function resetRefinements() {
        if (!moduleOn || !refinementsChanged) return
        engine.applyValues(ordered(refinementFields).map(p => ({op:operation, field:p.field, value:p.def})))
    }
    component Guide: Text {
        x: Theme.s3; width: parent.width - Theme.s3 * 2
        textFormat: Text.PlainText; wrapMode: Text.WordWrap
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
    }
    component SectionLabel: Text {
        x: Theme.s3; width: parent.width - Theme.s3 * 2
        height: implicitHeight + Theme.s2; verticalAlignment: Text.AlignBottom
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl; font.weight: Theme.wHeading
        color: Theme.textSecondary
    }
    Item {
        width: parent.width; height: Theme.hRow + Theme.hControl + Theme.s1
        Text {
            x: Theme.s3; width: parent.width - Theme.s3 * 2; height: Theme.hRow
            text: qsTr("Film stock"); verticalAlignment: Text.AlignVCenter
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl; color: Theme.textSecondary
        }
        ComboField {
            id: picker
            objectName: "printStockChoice"
            x: Theme.s3; y: Theme.hRow; width: parent.width - Theme.s3 * 2
            model: root.stockOptions.map(o => Names.choice(o.label))
            currentIndex: root.moduleOn ? root.stockOptions.findIndex(o => o.value === root.stock.value) : -1
            displayText: root.moduleOn ? currentText : qsTr("Select stock…")
            tipTitle: qsTr("Film stock"); tip: qsTr("Choose a look to apply it. Each stock shapes colour and contrast; add grain separately with the Grain tool.")
            onActivated: index => { if (index >= 0 && index < root.stockOptions.length) engine.setParam(root.operation, "stock", root.stockOptions[index].value) }
            delegate: C.ItemDelegate {
                id: choice
                required property int index
                required property string modelData
                objectName: "printStockOption_" + index
                text: modelData
                Accessible.description: root.stockDescription((root.stockOptions[index] || {}).value)
                width: picker.popup.availableWidth
                leftPadding: Theme.s2
                rightPadding: Theme.s2
                topPadding: Theme.s1
                bottomPadding: Theme.s1
                height: implicitContentHeight + topPadding + bottomPadding
                highlighted: picker.highlightedIndex === index
                contentItem: Column {
                    id: labels
                    spacing: Theme.s1 / 2
                    Text { width: parent.width; text: choice.modelData; elide: Text.ElideRight; textFormat: Text.PlainText; font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl; color: Theme.textPrimary }
                    Text { width: parent.width; text: root.stockDescription((root.stockOptions[choice.index] || {}).value); elide: Text.ElideRight; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary }
                }
                background: Rectangle { color: choice.highlighted ? Theme.hoverBg : "transparent" }
                Tooltip { text: choice.modelData; description: choice.Accessible.description; visible: choice.hovered }
            }
            TapHandler { acceptedButtons: Qt.RightButton; onTapped: engine.resetParam(root.operation, "stock") }
        }
    }
    Guide { text: root.moduleOn ? root.stockDescription(root.stock.value) : qsTr("Choose a film look, then adjust its strength.") }
    AdjustmentRow { width: parent.width; param: Object.assign({}, root.row("strength"), {suffix:"%", decimals:0}) }
    Guide { text: qsTr("0% keeps your original look; 100% gives the full film look.") }
    Guide { text: qsTr("Use the status dot beside Print stock to compare the effect.") }
    Guide {
        visible: root.moduleOn && !root.digitalCinema && root.stock.value !== 22 && root.row("paper").value !== 2
        text: qsTr("This edit uses the earlier white rendering. Balanced print keeps highlight headroom and a neutral grey scale.")
    }
    ToolButton {
        objectName: "useBalancedPrint"
        visible: root.moduleOn && !root.digitalCinema && root.stock.value !== 22 && root.row("paper").value !== 2
        x: Theme.s3; text: qsTr("Use balanced print"); showLabel: true; iconName: "check"
        tip: qsTr("Update this edit's white rendering. Keep the stock, strength and colour controls. Undo restores the saved look.")
        onClicked: engine.applyValues([{op:root.operation, field:"paper", value:2}])
    }
    SectionLabel { text: qsTr("Colour balance") }
    Guide { text: qsTr("Move left or right towards the named colour. Centre keeps the stock's balance.") }
    ModuleRows { width: parent.width; headings: false; rows: root.ordered(root.balanceFields) }
    ToolButton {
        objectName: "advanced_omarawprint"
        x: Theme.s3; text: qsTr("Advanced"); showLabel: true
        iconName: root.advanced ? "chevron-up" : "chevron-down"
        tip: qsTr("Refine the print's whites and individual colour-channel contrast.")
        onClicked: root.controller.setAdvanced(root.key, !root.advanced)
    }
    SlideSection {
        width: parent.width; expanded: root.advanced
        SectionLabel { text: qsTr("Viewing") }
        AdjustmentRow { width: parent.width; param: root.whiteRow("white") }
        AdjustmentRow { objectName: "filmPaperRendering"; visible: !root.digitalCinema; width: parent.width; param: root.whiteRow("paper") }
        Guide {
            text: root.digitalCinema ? qsTr("Cinema looks include their own contrast, colour and highlight rendering.")
                                     : qsTr("Balanced print keeps neutral greys and highlight headroom. The legacy choices preserve earlier edits.")
        }
        SectionLabel { text: qsTr("Channel contrast") }
        Guide { text: qsTr("Fine-tune contrast in each colour channel. These controls can also shift colour; 0 keeps the stock's own response.") }
        ModuleRows { width: parent.width; headings: false; rows: root.ordered(root.contrastFields) }
        Column {
            visible: root.hasProfile; width: parent.width; spacing: Theme.s1
            SectionLabel { text: qsTr("Imported camera profile") }
            Guide { text: qsTr("Choose camera profiles in Prepare → Camera profile. Keep the saved tables to preserve this look.") }
            AdjustmentRow { width: parent.width; param: root.row("profile") }
        }
        ToolButton {
            objectName: "resetPrintRefinements"
            x: Theme.s3; text: qsTr("Reset refinements"); showLabel: true; iconName: "rotate-ccw"
            enabled: root.moduleOn && root.refinementsChanged
            tip: qsTr("Restore colour balance, print whites and channel contrast. Keep the selected stock, strength and imported profile.")
            onClicked: root.resetRefinements()
        }
    }
}
