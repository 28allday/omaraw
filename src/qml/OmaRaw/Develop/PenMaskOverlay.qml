import QtQuick
import OmaRaw.Ui

// Editable cubic paths. Pointer motion stays local; one release is one edit.
// The canvas covers only the viewport, including at native/high zoom.
FocusScope {
    id: root
    objectName: "penMaskOverlay"
    property bool drawing: false
    property bool newLocal: true
    property real feather: 0.02
    property rect viewport: Qt.rect(0, 0, width, height)
    property var draft: []
    property var dragged: null
    property int selectedNode: -1
    property var gesture: null
    readonly property var shape: engine.shapes[engine.activeShape] || ({})
    readonly property var nodes: drawing ? draft : dragged || (shape.shape === 4 ? shape.displayPoints || [] : [])
    readonly property bool canClose: drawing && draft.length >= 3
    signal finished()
    function clone(list) { return list.map(p => Object.assign({}, p)) }
    function clear() { draft = []; dragged = null; gesture = null; selectedNode = -1 }
    function cancel() { clear(); if (drawing) finished() }
    function finish() {
        if (!canClose) return
        const path = clone(draft)
        clear(); engine.addPenPath(path, feather, newLocal); finished()
    }
    function commit(list) { engine.setPenPath(engine.activeShape, list, shape.border); dragged = null }
    function removePoint() {
        if (drawing) { draft = draft.slice(0, -1); selectedNode = draft.length - 1; return }
        if (selectedNode < 0 || nodes.length <= 3) return
        const list = clone(nodes); list.splice(selectedNode, 1); selectedNode = -1; commit(list)
    }
    function cornerPoint() {
        if (drawing || selectedNode < 0) return
        const list = clone(nodes), p = list[selectedNode]
        p.inX = p.outX = p.x; p.inY = p.outY = p.y; commit(list)
    }
    function smoothPoint() {
        if (drawing || selectedNode < 0) return
        const list = clone(nodes), i = selectedNode, p = list[i]
        const prev = list[(i+list.length-1)%list.length], next = list[(i+1)%list.length]
        const dx = (next.x-prev.x)/6, dy = (next.y-prev.y)/6
        p.inX = p.x-dx; p.inY = p.y-dy; p.outX = p.x+dx; p.outY = p.y+dy; commit(list)
    }
    function centre(list) {
        let x = 0, y = 0
        for (const p of list) { x += p.x; y += p.y }
        return {x: x/Math.max(1,list.length), y: y/Math.max(1,list.length)}
    }
    function distance(x,y,px,py) { return Math.hypot(x-px*width,y-py*height) }
    function hit(x,y) {
        if (selectedNode >= 0 && selectedNode < nodes.length) {
            const p = nodes[selectedNode]
            for (const key of ["in", "out"])
                if (distance(x,y,p[key+"X"],p[key+"Y"]) < 9 && distance(p.x*width,p.y*height,p[key+"X"],p[key+"Y"]) > 3)
                    return {kind:key,index:selectedNode}
        }
        for (let i=0; i<nodes.length; ++i) if (distance(x,y,nodes[i].x,nodes[i].y)<10) return {kind:"node",index:i}
        const c=centre(nodes)
        if (nodes.length && distance(x,y,c.x,c.y)<10) return {kind:"move",index:-1}
        return null
    }
    function lerp(a,b,t) { return {x:a.x+(b.x-a.x)*t,y:a.y+(b.y-a.y)*t} }
    function splitAt(x,y,checkOnly) {
        if (drawing || nodes.length<3 || nodes.length>=512) return
        let best=12, segment=-1, position=0
        for(let i=0;i<nodes.length;++i) {
            const a=nodes[i],b=nodes[(i+1)%nodes.length]
            for(let j=1;j<40;++j) {
                const t=j/40,u=1-t
                const px=u*u*u*a.x+3*u*u*t*a.outX+3*u*t*t*b.inX+t*t*t*b.x
                const py=u*u*u*a.y+3*u*u*t*a.outY+3*u*t*t*b.inY+t*t*t*b.y
                const d=distance(x,y,px,py)
                if(d<best){best=d;segment=i;position=t}
            }
        }
        if(segment<0) return
        if(checkOnly)return true
        const list=clone(nodes),a=list[segment],b=list[(segment+1)%list.length],t=position
        const q0=lerp(a,{x:a.outX,y:a.outY},t),q1=lerp({x:a.outX,y:a.outY},{x:b.inX,y:b.inY},t),q2=lerp({x:b.inX,y:b.inY},b,t)
        const r0=lerp(q0,q1,t),r1=lerp(q1,q2,t),p=lerp(r0,r1,t)
        a.outX=q0.x;a.outY=q0.y;b.inX=q2.x;b.inY=q2.y
        list.splice(segment+1,0,{x:p.x,y:p.y,inX:r0.x,inY:r0.y,outX:r1.x,outY:r1.y})
        selectedNode=segment+1;commit(list)
    }
    onDrawingChanged: { clear(); if (drawing) forceActiveFocus() }
    onVisibleChanged: if (!visible) cancel()
    onNodesChanged: guide.requestPaint()
    onWidthChanged: guide.requestPaint()
    onHeightChanged: guide.requestPaint()
    onEnabledChanged: if (!enabled) cancel()
    onSelectedNodeChanged: guide.requestPaint()
    Connections {
        target: engine
        function onImageChanged() { root.cancel() }
        function onActiveLocalChanged() { root.cancel() }
        function onActiveShapeChanged() { root.cancel() }
        function onLocalsChanged() { if (!root.gesture && root.selectedNode >= root.nodes.length) root.selectedNode = -1; guide.requestPaint() }
    }
    Keys.onShortcutOverride: event => {
        if ((drawing || selectedNode >= 0) && [Qt.Key_Escape,Qt.Key_Return,Qt.Key_Enter,Qt.Key_Delete,Qt.Key_Backspace].indexOf(event.key)>=0) event.accepted=true
        if (drawing && event.key===Qt.Key_Z && (event.modifiers & Qt.ControlModifier)) event.accepted=true
    }
    Keys.onPressed: event => {
        if(event.key===Qt.Key_Escape) { cancel();event.accepted=true }
        else if(drawing && (event.key===Qt.Key_Return || event.key===Qt.Key_Enter)) { finish();event.accepted=true }
        else if(event.key===Qt.Key_Delete || event.key===Qt.Key_Backspace || (drawing && event.key===Qt.Key_Z && (event.modifiers & Qt.ControlModifier))) { removePoint();event.accepted=true }
    }
    Canvas {
        id: guide; objectName: "penMaskGuide"
        x: Math.floor(Math.max(0,root.viewport.x)); y: Math.floor(Math.max(0,root.viewport.y))
        width: Math.max(0,Math.ceil(Math.min(root.width,root.viewport.x+root.viewport.width))-x)
        height: Math.max(0,Math.ceil(Math.min(root.height,root.viewport.y+root.viewport.height))-y)
        onXChanged: requestPaint(); onYChanged: requestPaint(); onWidthChanged: requestPaint(); onHeightChanged: requestPaint()
        onPaint: {
            const c=getContext("2d");c.reset();c.translate(-x,-y)
            function path(list,closed,colour) {
                if(!list.length)return
                c.strokeStyle=colour;c.lineWidth=2;c.beginPath();c.moveTo(list[0].x*root.width,list[0].y*root.height)
                for(let i=1;i<list.length+(closed?1:0);++i) {
                    const a=list[(i-1)%list.length],b=list[i%list.length]
                    c.bezierCurveTo(a.outX*root.width,a.outY*root.height,b.inX*root.width,b.inY*root.height,b.x*root.width,b.y*root.height)
                }
                c.stroke()
            }
            for(const local of engine.locals) for(let i=0;i<local.shapes.length;++i) {
                const s=local.shapes[i]
                if(s.shape!==4 || (local.priority===engine.activeLocal && i===engine.activeShape))continue
                if(local.priority===engine.activeLocal || i===0)path(s.displayPoints||[],true,Qt.rgba(1,1,1,s.enabled===false?.25:.6))
            }
            path(root.nodes,!root.drawing,Theme.accent)
            if(root.selectedNode>=0 && root.selectedNode<root.nodes.length) {
                const p=root.nodes[root.selectedNode]
                c.strokeStyle=Theme.accent;c.lineWidth=1;c.beginPath()
                c.moveTo(p.inX*root.width,p.inY*root.height);c.lineTo(p.x*root.width,p.y*root.height);c.lineTo(p.outX*root.width,p.outY*root.height);c.stroke()
                for(const key of ["in","out"]) { c.beginPath();c.arc(p[key+"X"]*root.width,p[key+"Y"]*root.height,4,0,Math.PI*2);c.fillStyle=Theme.accent;c.fill() }
            }
            for(let i=0;i<root.nodes.length;++i) {
                const p=root.nodes[i],px=p.x*root.width,py=p.y*root.height
                c.fillStyle=i===root.selectedNode?Theme.accent:Theme.panelBg;c.strokeStyle=Theme.accent;c.lineWidth=2
                c.fillRect(px-4,py-4,8,8);c.strokeRect(px-4,py-4,8,8)
                if(root.drawing && i===0){c.beginPath();c.arc(px,py,8,0,Math.PI*2);c.stroke()}
            }
            if(!root.drawing && root.nodes.length) {
                const p=root.centre(root.nodes),px=p.x*root.width,py=p.y*root.height
                c.beginPath();c.moveTo(px-6,py);c.lineTo(px+6,py);c.moveTo(px,py-6);c.lineTo(px,py+6);c.stroke()
            }
        }
    }
    MouseArea {
        id: pointer; objectName: "penMaskPointer"; anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton; preventStealing: true
        cursorShape: root.drawing ? Qt.CrossCursor : Qt.ArrowCursor
        onPressed: mouse => {
            if(mouse.button===Qt.RightButton) { if(root.drawing)root.cancel();else mouse.accepted=false;return }
            if(root.drawing) {
                root.forceActiveFocus()
                if(root.canClose && root.distance(mouse.x,mouse.y,root.draft[0].x,root.draft[0].y)<10) { root.finish();return }
                if(root.draft.length>=512)return
                const x=Math.max(0,Math.min(1,mouse.x/width)),y=Math.max(0,Math.min(1,mouse.y/height))
                root.draft=root.draft.concat([{x:x,y:y,inX:x,inY:y,outX:x,outY:y}]);root.selectedNode=root.draft.length-1
                root.gesture={kind:"draw",index:root.selectedNode,x:mouse.x,y:mouse.y,original:root.clone(root.draft)}
            } else {
                const h=root.hit(mouse.x,mouse.y)
                if(!h){if(root.splitAt(mouse.x,mouse.y,true))root.forceActiveFocus();else mouse.accepted=false;return}
                root.forceActiveFocus();root.selectedNode=h.index
                if((mouse.modifiers & Qt.AltModifier) && h.kind==="node") {root.cornerPoint();return}
                root.gesture={kind:h.kind,index:h.index,x:mouse.x,y:mouse.y,original:root.clone(root.nodes)}
            }
        }
        onPositionChanged: mouse => {
            const g=root.gesture;if(!pressed || !g)return
            if(Math.hypot(mouse.x-g.x,mouse.y-g.y)<3 && !g.moved)return
            g.moved=true
            const list=root.clone(g.original),dx=(mouse.x-g.x)/width,dy=(mouse.y-g.y)/height
            if(g.kind==="move")for(const p of list){p.x+=dx;p.y+=dy;p.inX+=dx;p.inY+=dy;p.outX+=dx;p.outY+=dy}
            else {
                const p=list[g.index]
                if(g.kind==="node"){p.x+=dx;p.y+=dy;p.inX+=dx;p.inY+=dy;p.outX+=dx;p.outY+=dy}
                else if(g.kind==="draw"){p.outX=p.x+dx;p.outY=p.y+dy;p.inX=p.x-dx;p.inY=p.y-dy}
                else {
                    p[g.kind+"X"]+=dx;p[g.kind+"Y"]+=dy
                    if(!(mouse.modifiers & Qt.AltModifier)){const other=g.kind==="in"?"out":"in";p[other+"X"]=2*p.x-p[g.kind+"X"];p[other+"Y"]=2*p.y-p[g.kind+"Y"]}
                }
            }
            if(root.drawing)root.draft=list;else root.dragged=list
        }
        onReleased: {
            const g=root.gesture
            if(g && g.moved && !root.drawing && root.dragged)root.commit(root.dragged)
            root.gesture=null
        }
        onDoubleClicked: mouse => {
            if(root.drawing) {
                const n=root.draft.length
                if(n>1 && root.distance(root.draft[n-1].x*width,root.draft[n-1].y*height,root.draft[n-2].x,root.draft[n-2].y)<3)root.draft=root.draft.slice(0,-1)
                root.finish()
            } else root.splitAt(mouse.x,mouse.y,false)
        }
        onCanceled: {root.gesture=null;root.dragged=null}
    }
}
