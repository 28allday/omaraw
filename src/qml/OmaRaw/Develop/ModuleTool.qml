pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

ToolCard {
    id: root
    key: operation
    title: Names.module(operation, operation)
    tip: Names.moduleTip(operation)
    property var fields: []
    readonly property var rows: (engine.paramsVersion, engine.paramsFor(operation)).filter(p => fields.length === 0 || fields.includes(p.field))
    readonly property var basicRows: Names.ordered(rows.filter(p => !Names.isAdvanced(p.op, p.field)))
    readonly property var advancedRows: rows.filter(p => Names.isAdvanced(p.op, p.field))
    // A fold only earns its place when it removes a substantial set of rows.
    readonly property bool hasAdvanced: basicRows.length > 0 && advancedRows.length > 2
    readonly property bool advanced: controller.advancedTools[key] === true
    available: rows.length > 0
    ModuleRows {
        width: parent.width; headings: false
        rows: root.hasAdvanced ? root.basicRows : Names.ordered(root.rows)
    }
    ToolButton {
        objectName: "advanced_" + root.key
        x: Theme.s3
        visible: root.hasAdvanced
        text: qsTr("Advanced"); showLabel: true
        iconName: root.advanced ? "chevron-up" : "chevron-down"
        tip: qsTr("Show the fine settings for this tool. Hiding them keeps their effect on the photo.")
        onClicked: root.controller.setAdvanced(root.key, !root.advanced)
    }
    SlideSection {
        expanded: root.hasAdvanced && root.advanced
        width: parent.width
        ModuleRows { width: parent.width; headings: false; rows: root.hasAdvanced ? root.advancedRows : [] }
    }
}
