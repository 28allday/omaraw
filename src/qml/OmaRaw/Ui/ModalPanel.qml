import QtQuick
import QtQuick.Controls.Basic as C

// Shared surface for import/export browsers, options and confirmations.
C.Popup {
    parent: C.Overlay.overlay
    anchors.centerIn: parent
    padding: Theme.s5
    modal: true
    focus: true
    closePolicy: C.Popup.CloseOnEscape
    // Standard input/context controls inherit the panel's colours as well.
    palette.window: Theme.panelBg
    palette.windowText: Theme.textPrimary
    palette.base: Theme.inputBg
    palette.text: Theme.textPrimary
    palette.button: Theme.controlBg
    palette.buttonText: Theme.textPrimary
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentText
    palette.light: Theme.panelRaised
    palette.midlight: Theme.hoverBg
    palette.dark: Theme.borderStrong
    palette.placeholderText: Theme.textMuted
    background: Rectangle { color: Theme.panelBg; radius: Theme.rMenu; border.color: Theme.borderStrong }
    C.Overlay.modal: Rectangle { color: Theme.scrim }
}
