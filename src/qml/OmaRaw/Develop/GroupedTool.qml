pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// Related short adjustments share one fold. Their effects and actions stay
// independent: resetting exposure must never reset contrast or shadows.
ToolCard {
    id: root
    required property var operations
    // A shared effect can contribute just one field here. Its whole-effect
    // actions stay with its main editor; a row still has its own value reset.
    property var fields: ({})
    readonly property var rows: (engine.paramsVersion, operations.reduce((all, op) => all.concat(Names.ordered(engine.paramsFor(op).filter(p => !fields[op] || fields[op].includes(p.field)))), []))
    available: rows.length > 0
    showStatus: true
    moduleOn: (engine.paramsVersion, operations.some(op => engine.moduleEnabled(op)))
    changed: controller.paramsChanged(rows)
    contributes: controller.paramsChanged(rows.filter(p => p.enabled))
    ModuleRows {
        width: parent.width
        rows: root.rows
        inlineActions: true
        actionOperations: root.operations.filter(op => !root.fields[op])
        saveEnabled: root.controller.develop !== null
        onSavePresetRequested: (operation, label) => root.controller.saveModulePreset(operation, label)
    }
}
