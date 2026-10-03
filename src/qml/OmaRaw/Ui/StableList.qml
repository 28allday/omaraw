import QtQuick

// A Repeater model that survives a data refresh. Engine lists arrive as a
// new array after every parameter read-back, and a Repeater over a new
// array destroys and recreates every delegate — which killed a slider drag
// in progress about 120 ms in, so the handle had to be pressed again.
// `model` only changes when the set of keys changes (rows added, removed
// or reordered); a delegate binds its live entry by index from `source`,
// which updates in place.
QtObject {
    id: root
    property var source: []
    // Key of an entry; the default keys by position, so only a change in
    // count rebuilds the delegates.
    property var key: function (entry, index) { return index }
    readonly property string signature: (root.source || []).map((e, i) => String(root.key(e, i))).join("|")
    property var model: []
    onSignatureChanged: model = root.source || []
    Component.onCompleted: model = root.source || []
}
