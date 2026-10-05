pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as C

// Dropdown. Styled rather than themed, because the Material combo brings its
// own ripple, elevation and radius that all fight the brief.
C.ComboBox {
    id: root

    // Optional leading caption rendered inside the control, for the dense
    // Render Settings rows where a separate label column would waste width.
    property string caption: ""
    // Tooltip: what the choice changes, under a title (the caption unless
    // the owner names the control otherwise).
    property string tip: ""
    property string tipTitle: caption
    // A click never takes the keyboard from the editing shortcuts; Tab still reaches it.
    focusPolicy: Qt.TabFocus
    hoverEnabled: true

    Tooltip {
        text: root.tipTitle
        description: root.tip
        visible: (root.enabled && root.hovered) && !root.pressed && !root.popup.visible && root.tip !== ""
    }

    implicitWidth: 140
    implicitHeight: Theme.hField
    leftPadding: Theme.s3
    rightPadding: indicator.width + Theme.s2 * 2
    topPadding: 0
    bottomPadding: 0
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fsControl
    opacity: enabled ? 1.0 : Theme.disabledOpacity

    Accessible.role: Accessible.ComboBox
    // Most Develop dropdowns have no caption of their own, only a tip title.
    Accessible.name: caption !== "" ? caption : tipTitle

    background: Rectangle {
        color: root.pressed ? Theme.pressedOn(Theme.controlBg)
             : (root.enabled && root.hovered) ? Theme.hovered(Theme.controlBg) : Theme.controlBg
        radius: Theme.rControl
        border.width: root.visualFocus ? Theme.focusRing : Theme.hairline
        border.color: root.visualFocus ? Theme.accent : (root.enabled && root.hovered) ? Theme.borderStrong : Theme.border

        Behavior on color {
            ColorAnimation { duration: Theme.dFast; easing.type: Theme.easing }
        }
    }

    TextMetrics { id: captionMetrics; text: root.caption; font: captionLabel.font }
    contentItem: Item {
        Text {
            id: captionLabel
            objectName: "comboCaption"
            text: root.caption
            textFormat: Text.PlainText
            visible: root.caption !== ""
            width: visible ? Math.min(captionMetrics.advanceWidth, Math.max(0, parent.width * 0.4)) : 0
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsLabel
            color: Theme.textMuted
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            objectName: "comboValue"
            text: root.displayText
            textFormat: Text.PlainText
            font: root.font
            color: Theme.textPrimary
            elide: Text.ElideRight
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: captionLabel.right
            anchors.leftMargin: captionLabel.visible ? Theme.s2 : 0
            anchors.right: parent.right
        }
    }

    indicator: Icon {
        name: "chevron-down"
        size: Theme.szIcon
        color: Theme.textMuted
        x: root.width - width - Theme.s2
        y: root.topPadding + (root.availableHeight - height) / 2
    }

    delegate: C.ItemDelegate {
        id: option
        required property int index
        required property var modelData

        width: ListView.view ? ListView.view.width : root.availableWidth
        height: Theme.hRow + Theme.s1 * 2
        leftPadding: Theme.s2
        rightPadding: Theme.s2
        topPadding: 0
        bottomPadding: 0
        highlighted: root.highlightedIndex === index

        contentItem: Text {
            text: option.modelData
            textFormat: Text.PlainText
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsControl
            color: option.highlighted ? Theme.textPrimary : Theme.textSecondary
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            color: option.highlighted ? Theme.hoverBg : "transparent"
        }
    }

    popup: C.Popup {
        id: choices
        property real slideOffset: 0
        y: root.height
        width: root.width
        implicitHeight: Math.min(contentItem.implicitHeight + Theme.s1 * 2, 320)
        padding: Theme.s1

        contentItem: ListView {
            transform: Translate { y: choices.slideOffset }
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            C.ScrollIndicator.vertical: C.ScrollIndicator {}
        }

        background: Rectangle {
            transform: Translate { y: choices.slideOffset }
            color: Theme.panelRaised
            radius: Theme.rMenu
            border.width: Theme.hairline
            border.color: Theme.border
        }

        enter: Transition {
            NumberAnimation { target: choices; property: "slideOffset"; from: -Theme.s2; to: 0; duration: Theme.dSlow; easing.type: Theme.easing }
            NumberAnimation {
                property: "opacity"; from: 0; to: 1
                duration: Theme.dFast; easing.type: Theme.easing
            }
        }
        exit: Transition {
            NumberAnimation { target: choices; property: "slideOffset"; from: 0; to: -Theme.s2; duration: Theme.dFast; easing.type: Theme.easing }
            NumberAnimation {
                property: "opacity"; from: 1; to: 0
                duration: Theme.dFast; easing.type: Theme.easing
            }
        }
    }
}
