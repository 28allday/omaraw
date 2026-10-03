import QtQuick

// The panel-sized "nothing here yet" placeholder: empty bin, no clip selected,
// empty render queue. Quiet by design — an empty panel should recede, not
// advertise itself — but always says what would fill it.
Item {
    id: root

    property string iconName: ""
    property string title: ""
    property string description: ""
    // Optional call to action. Left unset, the state is purely informational.
    property string actionText: ""

    signal actionTriggered()

    implicitWidth: 200
    implicitHeight: column.implicitHeight

    Column {
        id: column
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.s5 * 2, 260)
        spacing: Theme.s2

        Icon {
            name: root.iconName
            visible: root.iconName !== ""
            size: 28
            color: Theme.textMuted
            opacity: 0.7
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
            text: root.title
            visible: root.title !== ""
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsHeading
            font.weight: Theme.wHeading
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
        }

        Text {
            text: root.description
            visible: root.description !== ""
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsLabel
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }

        Item {
            width: 1
            height: Theme.s1
            visible: root.actionText !== ""
        }

        ToolButton {
            text: root.actionText
            visible: root.actionText !== ""
            showLabel: true
            anchors.horizontalCenter: parent.horizontalCenter
            onClicked: root.actionTriggered()
        }
    }
}
