import QtQuick
import QtQuick.Controls.Basic
import OmaRaw.Ui

// Dropdown menu in the Oma chrome; entries are SMenuItems (declared items
// do not pick up a Menu's delegate, so the delegate here only styles
// submenu titles).
ContextMenu {
    id: m
    property string tip: ""
    delegate: SMenuItem {}
    implicitWidth: {
        let widest = 236
        for (let i = 0; i < m.count; ++i) {
            const item = m.itemAt(i)
            if (item) widest = Math.max(widest, item.implicitWidth + m.leftPadding + m.rightPadding)
        }
        return widest
    }
}
