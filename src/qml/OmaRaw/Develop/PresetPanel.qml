pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

// Presets: built-in looks and the ones saved from the current settings.
// Click applies (one render); + saves; right-click for auto-apply rules
// (any preset) or to delete (a saved one).
Column {
    id: root
    objectName: "presetPanel"
    width: parent ? parent.width : 260
    spacing: 0
    property var dockPanel: null
    readonly property real listHeight: browser.height
    property var presets: backend.presets()
    property string filter: ""
    property string categoryFilter: ""
    property bool uncategorisedOnly: false
    property real maximumListHeight: Theme.hRow * 8
    property string openGroup: ""
    readonly property bool filtering: filter.trim() !== "" || categoryFilter !== "" || uncategorisedOnly
    readonly property var categoryNames: presets.map(p => p.category || "").filter((name, i, all) => name !== "" && all.indexOf(name) === i).sort((a, b) => a.localeCompare(b))
    function reload() {
        presets = backend.presets()
        if (categoryFilter !== "" && !presets.some(p => p.category === categoryFilter)) categoryFilter = ""
    }
    Connections { target: backend; function onPresetsChanged() { root.reload() } }
    // Names of presets that carry an auto-apply rule.
    readonly property var ruled: { backend.presetRuleCount; return backend.presetRules().map(r => r.preset) }
    PresetRuleDialog { id: ruleDialog }
    // Favourites first, then built-ins, then saved; a search cuts across all of them.
    readonly property var shown: presets.filter(p => (!uncategorisedOnly || !p.category)
        && (categoryFilter === "" || p.category === categoryFilter)
        && (filter.trim() === "" || [p.name, p.category || "", p.description || ""].concat(p.tags || []).join(" ").toLowerCase().indexOf(filter.trim().toLowerCase()) >= 0))
        .sort((a, b) => (b.favourite ? 1 : 0) - (a.favourite ? 1 : 0)
            || root.presets.findIndex(p => p.id === a.id) - root.presets.findIndex(p => p.id === b.id))
    readonly property var recent: backend.recentPresets.map(n => presets.find(p => p.name === n)).filter(p => p !== undefined)
    readonly property var groups: {
        const result = []
        const favourites = shown.filter(p => p.favourite)
        if (favourites.length) result.push({key: "favourites", title: qsTr("Favourites"), presets: favourites})
        if (recent.length) result.push({key: "recent", title: qsTr("Recent"), presets: recent})
        for (const category of categoryNames)
            result.push({key: "category:" + category, title: category, presets: shown.filter(p => p.category === category)})
        const uncategorised = shown.filter(p => !p.category)
        if (uncategorised.length) result.push({key: "uncategorised", title: qsTr("Uncategorised"), presets: uncategorised})
        return result
    }
    readonly property var browserRows: {
        if (filtering) return shown.map(p => ({kind: "preset", preset: p}))
        const rows = []
        for (const group of groups) {
            rows.push({kind: "group", key: group.key, title: group.title, count: group.presets.length})
            if (openGroup === group.key)
                for (const preset of group.presets) rows.push({kind: "preset", preset: preset})
        }
        return rows
    }
    function hasOrganisation(preset) {
        return (!!preset.category || (preset.tags || []).length > 0) && (!preset.builtin || !!preset.description)
    }
    readonly property real rowsHeight: browserRows.reduce((sum, row) => sum + (row.kind === "group"
        ? Theme.hRow + Theme.s1 : hasOrganisation(row.preset) ? Theme.hRow * 2 : Theme.hRow), 0)
    function toggleGroup(key) {
        openGroup = openGroup === key ? "" : key
        Qt.callLater(() => {
            const index = browserRows.findIndex(row => row.kind === "group" && row.key === key)
            if (index >= 0) browser.positionViewAtIndex(index, root.openGroup === key ? ListView.Beginning : ListView.Contain)
        })
    }
    function apply(p) { backend.applyPreset(p.name) }
    function openModulePreset(operation, label) { saveDialog.moduleFilter = operation; saveDialog.moduleTitle = label; saveDialog.open() }
    function openStylePreset() { saveDialog.moduleFilter = ""; saveDialog.moduleTitle = ""; saveDialog.open() }

    DockHeader {
        dockPanel: root.dockPanel
        width: parent.width
        tabs: [qsTr("Presets")]
        helpSection: "develop"
        trailing: [
            IconButton { iconName: "plus"; text: qsTr("Save current settings as a preset"); tip: qsTr("Names the current adjustments so they can be applied to other photos in one click."); enabled: engine.imageId >= 0; onClicked: root.openStylePreset() }
        ]
    }
    SearchField {
        x: Theme.s2; width: parent.width - Theme.s2 * 2
        placeholder: qsTr("Search names, categories or tags")
        tip: qsTr("Narrows the list to presets whose name, category or tags contain what you type.")
        onTextChanged: root.filter = text
        onCleared: root.filter = ""
    }
    ComboField {
        objectName: "presetCategoryFilter"
        x: Theme.s2; width: parent.width - Theme.s2 * 2
        model: [qsTr("All categories"), qsTr("Uncategorised")].concat(root.categoryNames)
        tipTitle: qsTr("Preset category"); tip: qsTr("Shows only the presets filed under one category.")
        currentIndex: root.uncategorisedOnly ? 1 : root.categoryFilter === "" ? 0 : Math.max(0, root.categoryNames.indexOf(root.categoryFilter) + 2)
        onActivated: index => {
            root.uncategorisedOnly = index === 1
            root.categoryFilter = index > 1 ? root.categoryNames[index - 2] : ""
        }
        Accessible.name: qsTr("Preset category")
    }
    Item { width: 1; height: Theme.s1 }
    ListView {
        id: browser
        objectName: "presetBrowser"
        width: parent.width
        height: Math.min(root.maximumListHeight, root.rowsHeight)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.browserRows
        C.ScrollBar.vertical: ScrollBar {
            id: browserScrollBar
            policy: root.rowsHeight > browser.height ? C.ScrollBar.AlwaysOn : C.ScrollBar.AlwaysOff
        }
        delegate: Loader {
            id: entry
            required property var modelData
            width: ListView.view.width - (browserScrollBar.visible ? Theme.s2 : 0)
            height: item ? item.implicitHeight : Theme.hRow
            sourceComponent: modelData.kind === "group" ? folder : preset
            Component {
                id: folder
                C.AbstractButton {
                    objectName: "presetFolder_" + entry.modelData.key
                    implicitHeight: Theme.hRow + Theme.s1
                    width: entry.width
                    hoverEnabled: true
                    focusPolicy: Qt.TabFocus
                    text: entry.modelData.title
                    Accessible.name: qsTr("%1, %2 presets").arg(text).arg(entry.modelData.count)
                    Accessible.description: root.openGroup === entry.modelData.key ? qsTr("Collapse category") : qsTr("Expand category")
                    onClicked: root.toggleGroup(entry.modelData.key)
                    background: Rectangle {
                        color: parent.down ? Theme.controlBg : parent.hovered ? Theme.hovered(Theme.panelBg) : "transparent"
                        border.width: parent.visualFocus ? Theme.focusRing : 0
                        border.color: Theme.accent
                    }
                    contentItem: Item {
                        Icon {
                            x: Theme.s3; anchors.verticalCenter: parent.verticalCenter
                            name: root.openGroup === entry.modelData.key ? "chevron-down" : "chevron-right"
                            size: 13; color: Theme.textMuted
                        }
                        Text {
                            anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 20
                            anchors.right: count.left; anchors.rightMargin: Theme.s2
                            anchors.verticalCenter: parent.verticalCenter
                            text: entry.modelData.title; textFormat: Text.PlainText; elide: Text.ElideRight
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl; font.weight: Theme.wHeading
                            color: Theme.textSecondary
                        }
                        Text {
                            id: count
                            anchors.right: parent.right; anchors.rightMargin: Theme.s3
                            anchors.verticalCenter: parent.verticalCenter
                            text: entry.modelData.count
                            font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                        }
                    }
                }
            }
            Component {
                id: preset
                PresetRow { width: entry.width; modelData: entry.modelData.preset }
            }
        }
    }
    Text {
        visible: root.shown.length === 0
        x: Theme.s3; topPadding: Theme.s2
        text: qsTr("No presets match.")
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
    }

    component PresetRow: Rectangle {
        id: row
        required property var modelData
        readonly property string organisation: [modelData.category || ""].concat(modelData.tags || []).filter(t => t !== "").join(" · ")
        readonly property bool showOrganisation: root.hasOrganisation(modelData)
        implicitHeight: showOrganisation ? Theme.hRow * 2 : Theme.hRow
        height: implicitHeight
        color: hh.hovered ? Theme.hovered(Theme.panelBg) : "transparent"
        HoverHandler { id: hh }
        Icon {
            anchors.left: parent.left; anchors.leftMargin: Theme.s3
            anchors.verticalCenter: parent.verticalCenter
            name: row.modelData.builtin ? "sparkles" : "bookmark"; size: 13
            color: Theme.textMuted
        }
        Column {
            anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 20
            anchors.right: starButton.left; anchors.rightMargin: Theme.s1
            anchors.verticalCenter: parent.verticalCenter
            Text {
                width: parent.width
                textFormat: Text.PlainText
                text: row.modelData.name; elide: Text.ElideRight
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
                color: engine.imageId >= 0 ? Theme.textSecondary : Theme.textMuted
            }
            Text {
                width: parent.width; visible: row.showOrganisation
                text: row.modelData.description ? row.modelData.category + " · " + row.modelData.description : row.organisation
                textFormat: Text.PlainText; elide: Text.ElideRight
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
        }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            enabled: engine.imageId >= 0
            onTapped: root.apply(row.modelData)
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: rowMenu.popup()
        }
        ContextMenu {
            id: rowMenu
            MenuAction { text: row.modelData.favourite ? qsTr("Remove from favourites") : qsTr("Add to favourites"); iconName: "star"; onTriggered: backend.toggleFavouritePreset(row.modelData.name) }
            MenuAction { text: qsTr("Auto-apply on import…"); iconName: "sparkles"; onTriggered: ruleDialog.openFor(row.modelData.name) }
            MenuAction { text: qsTr("Category and tags…"); iconName: "tag"; enabled: !row.modelData.builtin; onTriggered: metadataDialog.openFor(row.modelData) }
            MenuAction { text: qsTr("Delete preset"); iconName: "trash-2"; enabled: !row.modelData.builtin; onTriggered: backend.deletePreset(row.modelData.id) }
        }
        // A dot marks a preset with an auto-apply rule; the star, a favourite (visible on hover otherwise).
        Rectangle {
            visible: root.ruled.indexOf(row.modelData.name) >= 0
            anchors.right: starButton.left; anchors.rightMargin: Theme.s1
            anchors.verticalCenter: parent.verticalCenter
            width: 6; height: 6; radius: 3; color: Theme.accent
        }
        IconButton {
            id: starButton
            anchors.right: parent.right; anchors.rightMargin: Theme.s1
            anchors.verticalCenter: parent.verticalCenter
            iconName: "star"; text: row.modelData.favourite ? qsTr("Favourite, click to remove") : qsTr("Make a favourite")
            tip: qsTr("Favourites have their own folder and appear first within each category.")
            checked: row.modelData.favourite === true
            opacity: row.modelData.favourite || hh.hovered ? 1 : 0
            onClicked: backend.toggleFavouritePreset(row.modelData.name)
        }
        Tooltip {
            visible: hh.hovered
            text: row.modelData.name
            description: (row.modelData.description || row.organisation)
                + (engine.imageId < 0 ? "\n" + qsTr("Open a photo in the engine first") : "")
        }
    }

    component OrganisationFields: Column {
        property alias category: categoryInput.text
        property alias tagsText: tagsInput.text
        readonly property var tags: tagsInput.text.split(/\r?\n/)
        spacing: Theme.s2
        SearchField { id: categoryInput; width: parent.width; placeholder: qsTr("Category (optional)"); tip: qsTr("A folder name for the preset list, such as Portrait or Film.") }
        Text { text: qsTr("Tags · one per line"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary }
        C.ScrollView {
            width: parent.width; height: Theme.hRow * 3
            C.TextArea {
                id: tagsInput
                placeholderText: qsTr("Portrait\nWarm light")
                wrapMode: TextEdit.Wrap
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
                color: Theme.textPrimary; placeholderTextColor: Theme.textMuted
                selectionColor: Theme.accent; selectedTextColor: Theme.accentText
                Accessible.name: qsTr("Preset tags, one per line")
                background: Rectangle { color: Theme.inputBg; border.width: Theme.hairline; border.color: tagsInput.activeFocus ? Theme.accent : Theme.border; radius: Theme.rControl }
            }
        }
    }

    C.Popup {
        id: metadataDialog
        objectName: "presetOrganisationDialog"
        property int presetId: 0
        property string presetTitle: ""
        function openFor(preset) {
            presetId = preset.id; presetTitle = preset.name
            metadataFields.category = preset.category || ""
            metadataFields.tagsText = (preset.tags || []).join("\n")
            open()
        }
        modal: true; parent: C.Overlay.overlay; anchors.centerIn: parent
        width: 360; padding: Theme.s4
        background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }
        Column {
            width: parent.width; spacing: Theme.s3
            Text { width: parent.width; text: qsTr("Organise %1").arg(metadataDialog.presetTitle); wrapMode: Text.WordWrap; font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; color: Theme.textPrimary }
            OrganisationFields { id: metadataFields; objectName: "presetMetadataFields"; width: parent.width }
            Row {
                anchors.right: parent.right; spacing: Theme.s2
                ToolButton { text: qsTr("Cancel"); showLabel: true; onClicked: metadataDialog.close() }
                ToolButton { objectName: "presetOrganisationSave"; text: qsTr("Save"); showLabel: true; onClicked: { if (backend.setPresetMetadata(metadataDialog.presetId, metadataFields.category, metadataFields.tags)) metadataDialog.close() } }
            }
        }
    }

    C.Popup {
        id: saveDialog
        objectName: "savePresetDialog"
        property string moduleFilter: ""
        property string moduleTitle: ""
        property var checkedGroups: ({})
        readonly property var groups: engine.settingsGroups()
        readonly property var chosen: Object.keys(checkedGroups).filter(key => checkedGroups[key])
        property string saveError: ""
        function setAll(on) { const selected = {}; for (const group of groups) selected[group.key] = on; checkedGroups = selected }
        function toggle(key) { const selected = Object.assign({}, checkedGroups); selected[key] = !selected[key]; checkedGroups = selected }
        modal: true
        parent: C.Overlay.overlay
        anchors.centerIn: parent
        width: moduleFilter === "" ? 600 : 360; padding: Theme.s4
        background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }
        onOpened: {
            if (Object.keys(checkedGroups).length === 0) {
                const selected = {}; for (const group of groups) selected[group.key] = group.defaultOn && group.key !== "local"
                checkedGroups = selected
            }
            presetName.text = ""; saveFields.category = ""; saveFields.tagsText = ""; saveError = ""; presetName.focusInput()
        }
        function save() {
            if (presetName.text.trim() === "" || (moduleFilter === "" && chosen.length === 0)) return
            const saved = moduleFilter !== ""
                ? backend.saveCurrentModulePreset(presetName.text.trim(), moduleFilter, saveFields.category, saveFields.tags)
                : backend.saveCurrentPreset(presetName.text.trim(), chosen, saveFields.category, saveFields.tags)
            if (saved > 0) close()
            else saveError = backend.statusMessage
        }
        Column {
            width: parent.width; spacing: Theme.s3
            Text { width: parent.width; text: saveDialog.moduleFilter === "" ? qsTr("Save current settings as a preset") : qsTr("Save %1 preset").arg(saveDialog.moduleTitle); wrapMode: Text.WordWrap; font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading; color: Theme.textPrimary }
            Row {
                width: parent.width; spacing: Theme.s4
                Column {
                    width: saveDialog.moduleFilter !== "" ? parent.width : (parent.width - parent.spacing) / 2; spacing: Theme.s3
                    SearchField { id: presetName; objectName: "savePresetName"; width: parent.width; placeholder: qsTr("Preset name"); tip: qsTr("The name the preset is listed and searched by."); live: false; onAccepted: saveDialog.save() }
                    OrganisationFields { id: saveFields; width: parent.width }
                }
                Column {
                    visible: saveDialog.moduleFilter === ""
                    width: (parent.width - parent.spacing) / 2; spacing: Theme.s1
                    Text { text: qsTr("Include adjustments"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary }
                    Repeater {
                        model: saveDialog.groups
                        Toggle {
                            required property var modelData
                            objectName: "presetGroup_" + modelData.key
                            width: parent.width; label: modelData.label
                            tip: qsTr("Whether this group of settings goes into the preset.")
                            checkable: false; checked: saveDialog.checkedGroups[modelData.key] === true
                            onClicked: saveDialog.toggle(modelData.key)
                        }
                    }
                    Row {
                        spacing: Theme.s2
                        ToolButton { text: qsTr("All"); showLabel: true; tip: qsTr("Ticks every group."); onClicked: saveDialog.setAll(true) }
                        ToolButton { text: qsTr("None"); showLabel: true; tip: qsTr("Clears every group."); onClicked: saveDialog.setAll(false) }
                    }
                }
            }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                visible: saveDialog.moduleFilter === "" && (saveDialog.chosen.indexOf("local") >= 0 || saveDialog.chosen.indexOf("retouch") >= 0)
                text: qsTr("Local adjustments and retouch replace those groups on the target photo, including when the saved group is empty.")
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
            Text { width: parent.width; wrapMode: Text.WordWrap; visible: text !== ""; text: saveDialog.saveError; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.danger }
            Row {
                anchors.right: parent.right; spacing: Theme.s2
                ToolButton { text: qsTr("Cancel"); showLabel: true; onClicked: saveDialog.close() }
                ToolButton { objectName: "savePresetAccept"; text: qsTr("Save"); showLabel: true; enabled: presetName.text.trim() !== "" && (saveDialog.moduleFilter !== "" || saveDialog.chosen.length > 0) && !engine.busy; onClicked: saveDialog.save() }
            }
        }
    }
}
