import QtQuick

// Shape as well as colour distinguishes an applied effect from one kept off.
Rectangle {
    property bool applied: false
    objectName: "effectStatusDot"
    width: Theme.szStatusDot; height: width; radius: width / 2
    color: applied ? Theme.effectApplied : "transparent"
    border.width: applied ? 0 : Theme.hairline
    border.color: Theme.textMuted
    Accessible.role: Accessible.StaticText
    Accessible.name: applied ? qsTr("Effect applied") : qsTr("Effect not applied")
}
