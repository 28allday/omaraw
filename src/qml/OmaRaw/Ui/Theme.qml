pragma Singleton

import QtQuick
import OmaRaw.Desktop

// The single source of every colour, dimension, radius, duration and type
// size in OmaRAW. Nothing outside this file may contain a colour literal
// or a magic dimension. Palette is the shared Oma design system (same family
// as OmaEdit). Standard mode keeps its fixed surfaces and desktop accent.
// Light mode recolors the chrome only. The photograph's surround stays a
// neutral dark grey in every appearance, so a theme change does not regrade
// the picture. Colour Critical uses neutral interface colours and wins over
// Light. Photographic content and functional channel/label colours retain
// their meaning in every mode.
QtObject {
    id: theme

    // ── Identity ────────────────────────────────────────────────────────────
    readonly property string appName: "OmaRAW"

    // ── Accessibility modes ─────────────────────────────────────────────────
    // High contrast lifts the secondary and muted text and the borders so
    // every label clears WCAG AA on the panel ground; reduced motion turns
    // every duration to zero. Both are set from the window (View menu).
    property bool highContrast: false
    property bool reducedMotion: false
    property bool colourCritical: false
    // Light chrome. Colour Critical ignores this and keeps its neutral grey.
    property bool light: false

    // ── Colour ──────────────────────────────────────────────────────────────
    readonly property color windowBg: colourCritical ? "#2D2D2D" : light ? "#E8EDF3" : "#0B0E13"
    readonly property color panelBg: colourCritical ? "#353535" : light ? "#F7F9FB" : "#111722"
    readonly property color panelRaised: colourCritical ? "#404040" : light ? "#FFFFFF" : "#171E2A"
    readonly property color controlBg: colourCritical ? "#404040" : light ? "#FFFFFF" : "#1B2431"
    readonly property color inputBg: colourCritical ? "#202020" : light ? "#FFFFFF" : controlBg
    readonly property color hoverBg: hovered(controlBg)
    // Quiet table striping is separate from the deliberately visible rollover.
    readonly property color alternateRowBg: colourCritical ? "#323232" : light ? "#EEF2F6" : Qt.lighter(windowBg, 1.06)
    readonly property color border: colourCritical ? (highContrast ? "#777777" : "#484848")
                              : light ? (highContrast ? "#5C6B7E" : "#D0D7E2")
                              : highContrast ? "#4A5670" : "#293241"
    readonly property color borderStrong: colourCritical ? (highContrast ? "#AAAAAA" : "#737373")
                                    : light ? (highContrast ? "#2C3848" : "#8B99AB")
                                    : highContrast ? "#8998B4" : "#58657A"

    readonly property color textPrimary: colourCritical ? (highContrast ? "#F2F2F2" : "#D6D6D6")
                                   : light ? "#1A2330"
                                   : highContrast ? "#F5F8FD" : "#E7EDF7"
    readonly property color textSecondary: colourCritical ? (highContrast ? "#E0E0E0" : "#B3B3B3")
                                     : light ? (highContrast ? "#1A2330" : "#3A4758")
                                     : highContrast ? "#D3DBE8" : "#A7B1C1"
    readonly property color textMuted: colourCritical ? (highContrast ? "#BBBBBB" : "#909090")
                                 : light ? (highContrast ? "#2A3544" : "#5C6A7C")
                                 : highContrast ? "#A8B3C6" : "#727E90"

    // The desktop's accent when Omarchy is there, the Oma accent when it is
    // not (plain Arch, CI, a container) — and the app must look finished
    // either way, so the fallback is the colour this always used.
    readonly property color accent: colourCritical ? (highContrast ? "#E0E0E0" : "#B8B8B8") : OmarchyTheme.accent
    readonly property color accentPressed: colourCritical ? "#A0A0A0" : Qt.darker(accent, 1.35)
    readonly property color aiAccent: colourCritical ? accent : "#8B5CF6"
    // Light mode uses darker status colours so they stay readable as text on white.
    readonly property color success: colourCritical ? textSecondary : light ? "#067647" : "#34D399"
    readonly property color warning: colourCritical ? textPrimary : light ? "#8A5A00" : "#FBBF24"
    readonly property color danger: colourCritical ? textPrimary : light ? "#B42318" : "#EF4444"
    // Functional effect state keeps its meaning in both viewing modes.
    readonly property color effectApplied: "#5FBF66"

    // Text that stays legible on top of a filled accent control.
    // ⚠️ Derived, not fixed: an accent can be pale gold or deep violet and the
    // label on top has to stay legible on both. Rec.709 luminance of the
    // accent decides whether that text is near-black or near-white.
    readonly property color accentText: colourCritical ? "#202020" :
        (0.2126 * accent.r + 0.7152 * accent.g + 0.0722 * accent.b) > 0.55 ? "#04121A" : "#F2F7FF"
    readonly property color aiAccentText: colourCritical ? accentText : "#F5F3FF"

    // Derived interaction states. Every control derives from these rather
    // than inventing its own hover or pressed colour.
    // A fixed lightness step stays visible on dark wells and pale selected
    // fills. Equal channel steps keep critical greys neutral. Very pale
    // accents step down instead of clipping to white.
    function hovered(base) {
        const light = .2126 * base.r + .7152 * base.g + .0722 * base.b
        const step = (light > .8 ? -1 : 1) * (highContrast ? .13 : .09)
        return Qt.rgba(Math.max(0, Math.min(1, base.r + step)),
                       Math.max(0, Math.min(1, base.g + step)),
                       Math.max(0, Math.min(1, base.b + step)), base.a)
    }
    function pressedOn(base) { return Qt.rgba(base.r * .72, base.g * .72, base.b * .72, base.a) }
    readonly property real disabledOpacity: highContrast ? 0.55 : 0.38
    readonly property color dropValid: accent
    readonly property color dropInvalid: danger

    // ── Canvas surfaces ─────────────────────────────────────────────────────
    // The photograph's surround stays a neutral dark grey in dark, light and
    // Colour Critical. It does not follow the chrome, so Light does not
    // regrade the picture.
    readonly property color pasteboard: colourCritical ? "#262626" : "#0B0E13"
    readonly property color rulerBg: panelBg
    readonly property color rulerTick: textMuted
    readonly property color rulerText: textSecondary
    // Translucent scrims laid over image content. The plate stays dark in
    // every appearance, so glyphs on it stay light. Light chrome must not
    // put textSecondary here: that colour is dark, and the crop mark
    // disappears into the chip.
    readonly property color scrim: Qt.rgba(0, 0, 0, 0.55)
    readonly property color scrimHover: Qt.rgba(0, 0, 0, 0.72)
    readonly property color scrimPressed: Qt.rgba(0, 0, 0, 0.84)
    readonly property color scrimText: colourCritical ? textPrimary : "#E7EDF7"
    // The selection wash is the accent at a twelfth, not a literal of it:
    // a fixed cyan over an orange selection ring reads as two selections.
    readonly property color selectionFill: Qt.rgba(accent.r, accent.g, accent.b, 0.12)

    // Levels/Curves channel tint (GimpHistogramChannel: 1 R, 2 G, 3 B);
    // any other channel draws in the caller's neutral colour.
    readonly property color channelRed: "#E06C6C"
    readonly property color channelGreen: "#5FBF66"
    readonly property color channelBlue: "#6C8FE0"
    readonly property color channelRedFill: "#4A2A2E"
    readonly property color channelGreenFill: "#22402B"
    readonly property color channelBlueFill: "#26324F"
    function channelColor(ch, fallback) {
        return ch === 1 ? channelRed : ch === 2 ? channelGreen
             : ch === 3 ? channelBlue : fallback
    }
    function channelFill(ch, fallback) {
        return ch === 1 ? channelRedFill : ch === 2 ? channelGreenFill
             : ch === 3 ? channelBlueFill : fallback
    }

    // ── Spacing ─────────────────────────────────────────────────────────────
    // The brief's scale: 4, 8, 12, 16, 24. Named by step so call sites read
    // as intent rather than arithmetic.
    readonly property int s1: Math.round(4 * densityScale)
    readonly property int s2: Math.round(8 * densityScale)
    readonly property int s3: Math.round(12 * densityScale)
    readonly property int s4: Math.round(16 * densityScale)
    readonly property int s5: Math.round(24 * densityScale)

    // ── Radius ──────────────────────────────────────────────────────────────
    // Controls 3, menus and dialogs 4, major docks square.
    readonly property int rControl: 3
    readonly property int rMenu: 4
    readonly property int rPanel: 0

    // ── Lines ───────────────────────────────────────────────────────────────
    readonly property int hairline: 1
    readonly property int focusRing: 1
    readonly property int selectionRing: 2
    readonly property int activeUnderline: 2

    // ── Density ─────────────────────────────────────────────────────────────
    // compact (default) | normal | comfortable. Scales spacing and control
    // heights only — never type size.
    property string density: "compact"
    readonly property real densityScale: density === "comfortable" ? 1.25
                                       : density === "normal" ? 1.12
                                       : 1.0

    // ── Type ────────────────────────────────────────────────────────────────
    // Inter when installed, Noto Sans otherwise.
    readonly property string fontFamily: {
        const families = Qt.fontFamilies()
        if (families.indexOf("Inter") >= 0) return "Inter"
        if (families.indexOf("Noto Sans") >= 0) return "Noto Sans"
        return "sans-serif"
    }
    // Numeric readouts want tabular digits so values do not jitter.
    readonly property string monoFamily: {
        const families = Qt.fontFamilies()
        if (families.indexOf("Inter") >= 0) return "Inter"
        if (families.indexOf("Noto Sans Mono") >= 0) return "Noto Sans Mono"
        return "monospace"
    }

    readonly property int fsBase: 12
    readonly property int fsLabel: 11
    readonly property int fsControl: 12
    readonly property int fsHeading: 12
    readonly property int fsTitle: 15
    readonly property int fsRuler: 9
    readonly property int wHeading: Font.DemiBold
    readonly property int wNormal: Font.Normal

    // ── Control metrics ─────────────────────────────────────────────────────
    readonly property int hTitleBar: 44
    readonly property int hOptionsBar: 40
    readonly property int hDocTabs: 32
    readonly property int hStatusBar: 30
    readonly property int hRuler: 18
    readonly property int hControl: Math.round(24 * densityScale)
    // Text fields and dropdowns. Taller than a tool button so the type
    // has padding inside the box.
    readonly property int hField: hControl + s2
    readonly property int hRow: Math.round(22 * densityScale)
    readonly property int hLayerRow: Math.round(48 * densityScale)
    readonly property int hEffectRow: Math.round(24 * densityScale)
    readonly property int hTab: Math.round(32 * densityScale)
    readonly property int hDockHeader: Math.round(32 * densityScale)
    readonly property int szIcon: 16
    readonly property int szSliderHandle: 10
    readonly property int szSliderHandleActive: 12
    readonly property int szStatusDot: Math.round(8 * densityScale)
    readonly property int szIconHit: Math.round(24 * densityScale)
    readonly property int szToolCell: Math.round(38 * densityScale)
    readonly property int szThumb: 40
    readonly property int wToolDock: 200
    // Library workspace docks (brief §7): source 230–360, inspector 280–420,
    // filmstrip 110–190.
    readonly property int wSourceDock: 260
    readonly property int wSourceDockMin: 230
    readonly property int wSourceDockMax: 360
    readonly property int wInspector: 320
    readonly property int wInspectorMin: 280
    readonly property int wInspectorMax: 420
    readonly property int hFilmstrip: 128
    readonly property int hFilmstripMin: 110
    readonly property int hFilmstripMax: 190
    readonly property int hToolbar: 40
    readonly property int hWorkspaceBar: 44
    readonly property int szCardMin: 160
    readonly property int szCardMax: 480
    readonly property int szFilmThumb: 96
    // Colour labels: the six the concept shows, in this order.
    readonly property var labelNames: ["red", "orange", "yellow", "green", "blue", "purple"]
    readonly property var labelColors: ({ red: "#EF4444", orange: "#F97316", yellow: "#FACC15",
                                         green: "#22C55E", blue: "#3B82F6", purple: "#A855F7" })
    function labelColor(name) { return labelColors[name] !== undefined ? labelColors[name] : "transparent" }
    readonly property color star: colourCritical ? textPrimary : light ? "#C4920A" : "#E7EDF7"
    readonly property color starOff: colourCritical ? borderStrong : light ? "#C5CEDA" : "#3A4658"
    readonly property color pick: colourCritical ? textPrimary : "#34D399"
    readonly property color reject: colourCritical ? textMuted : "#EF4444"
    // Histogram channel fills over the panel ground.
    readonly property color histRed: Qt.rgba(0.94, 0.27, 0.27, 0.55)
    readonly property color histGreen: Qt.rgba(0.13, 0.77, 0.37, 0.55)
    readonly property color histBlue: Qt.rgba(0.23, 0.51, 0.96, 0.55)
    readonly property color histLuma: colourCritical ? Qt.rgba(0.9, 0.9, 0.9, 0.35) : light ? Qt.rgba(0.16, 0.2, 0.28, 0.55) : Qt.rgba(0.9, 0.93, 0.97, 0.35)
    readonly property int wToolGrid: 88
    readonly property int wRightDock: 372
    readonly property int wSplitter: 4
    readonly property int wScrollBar: 10
    readonly property int dragThreshold: 4

    // ── Motion ──────────────────────────────────────────────────────────────
    readonly property int dFast: reducedMotion ? 0 : 100
    readonly property int dNormal: reducedMotion ? 0 : 130
    readonly property int dSlow: reducedMotion ? 0 : 160
    readonly property int easing: Easing.OutCubic
    // Long enough that a pointer passing over the panel, or resting on a
    // control while reading it, is not interrupted.
    readonly property int tooltipDelay: 800
}
