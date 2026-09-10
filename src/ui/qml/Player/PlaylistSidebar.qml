pragma ComponentBehavior: Bound
import QtQuick
import "./../Components"
import QtQuick.Controls
import AoNami
import ".."

Rectangle {
    id: sideBar
    property alias treeView: treeView
    property string showName: ""
    property int    epCount: 0
    property string filterText: ""
    property bool   treeHasExpanded: false
    signal hideRequested()
    signal editorReleased()   // filter field lost focus -> let the page take keyboard shortcuts again
    color: Theme.surfaceDeep

    // Re-layout so filtered (0-height) rows collapse, and snap to the top so matches are in view.
    onFilterTextChanged: { treeView.contentY = 0; treeView.forceLayout(); treeView.returnToBounds() }

    function anyExpanded() {
        for (let r = 0; r < treeView.rows; r++)
            if (treeView.isExpanded(r)) return true
        return false
    }

    function toggleExpand() {
        if (anyExpanded()) { treeView.collapseRecursively(); treeView.contentY = 0 }
        else treeView.expandRecursively()
        treeView.forceLayout()
        treeHasExpanded = anyExpanded()
    }

    Rectangle {
        id: header
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 92
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.surface }
            GradientStop { position: 1.0; color: Theme.surfaceDeep }
        }

        Column {
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 14; rightMargin: 8 }
            spacing: 10

            Item {
                width: parent.width
                height: 36

                MarqueeText {
                    anchors { left: parent.left; right: closeBtn.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
                    height: 26
                    text: sideBar.showName.length > 0 ? sideBar.showName : "Playlist"
                    fontSize: 20
                    color: Theme.textPrimary
                    horizontalAlignment: Text.AlignLeft
                }

                IconButton {
                    id: closeBtn
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    iconName: "chevron-right"
                    iconSize: 24
                    boxRadius: 10
                    tip: "Close playlist"
                    onClicked: sideBar.hideRequested()
                }
            }

            Item {
                width: parent.width
                height: 36

                Rectangle {
                    visible: sideBar.epCount > 0
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    width: Math.min(countText.implicitWidth + 22, parent.width - headerBtns.width - 16)
                    height: 32
                    radius: 16
                    color: Qt.alpha(Theme.accent, 0.12)
                    border.color: Qt.alpha(Theme.accent, 0.30)
                    border.width: 1
                    Text {
                        id: countText
                        anchors { fill: parent; leftMargin: 11; rightMargin: 11 }
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        text: sideBar.epCount + (sideBar.epCount === 1 ? " ep" : " eps")
                        color: Theme.textAccent
                        font.pixelSize: Globals.sp(20)
                        font.weight: Font.Medium
                    }
                }

                Row {
                    id: headerBtns
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    spacing: 4
                    IconButton { iconName: "locate-fixed"; tip: "Scroll to current"; onClicked: sideBar.scrollToIndex(treeView.currentIndex) }
                    IconButton {
                        iconName: sideBar.treeHasExpanded ? "list-chevrons-down-up" : "list-chevrons-up-down"
                        tip: sideBar.treeHasExpanded ? "Collapse all" : "Expand all"
                        onClicked: sideBar.toggleExpand()
                    }
                    IconButton { iconName: "list-x"; tip: "Close all"; onClicked: App.playlist.clear() }
                }
            }
        }

        Rectangle {
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
            height: 2
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.5; color: Qt.alpha(Theme.accent, 0.55) }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }
    }

    Connections {
        target: App.playlist
        function onCurrentItemChanged(index) {
            selection.clear()
            treeView.currentIndex = index
            let cur = index
            while (cur.valid) {
                selection.select(cur, ItemSelectionModel.Select)
                cur = cur.parent
            }
            treeView.collapseRecursively()
            sideBar.scrollToIndex(index)
            sideBar.showName = App.playlist.currentShowName()
            sideBar.epCount = App.playlist.currentShowEpisodeCount()
        }
    }

    property var pendingScrollIndex: null

    Timer {
        id: scrollTimer
        interval: 50
        repeat: true
        property int tries: 0
        property real lastWidth: -1
        function begin() { tries = 0; lastWidth = -1; restart() }
        onTriggered: {
            if (!sideBar.pendingScrollIndex) { stop(); return }
            const settled = (sideBar.width === lastWidth)
            lastWidth = sideBar.width
            if (settled && sideBar.applyScroll(sideBar.pendingScrollIndex)) {
                sideBar.pendingScrollIndex = null
                stop()
            } else if (++tries > 40) {
                stop()
            }
        }
    }

    onVisibleChanged: if (visible && pendingScrollIndex) scrollTimer.begin()

    function applyScroll(index) {
        if (!visible || width <= 0 || treeView.height <= 0) return false
        treeView.expandToIndex(index)
        treeView.forceLayout()
        const row = treeView.rowAtIndex(index)
        if (row < 0) return false
        treeView.positionViewAtRow(row, TableView.AlignVCenter)
        treeHasExpanded = anyExpanded()
        return true
    }

    function scrollToIndex(index) {
        if (index === undefined || !index.valid) return
        if (filterText.length > 0 && App.playlist.isFilteredOut(index, filterText))
            filterField.text = ""
        pendingScrollIndex = index
        if (applyScroll(index)) pendingScrollIndex = null
        else if (visible) scrollTimer.begin()
    }

    Rectangle {
        id: filterRow
        anchors { top: header.bottom; left: parent.left; right: parent.right }
        height: 42
        color: Theme.surfaceDeep

        AppTextField {
            id: filterField
            anchors { fill: parent; leftMargin: 8; rightMargin: 8; topMargin: 5; bottomMargin: 5 }
            placeholderText: "Filter episodes..."
            color: Theme.textPrimary
            placeholderTextColor: Theme.textMuted
            fontSize: 19
            onTextChanged: sideBar.filterText = text
            onUnfocused: sideBar.editorReleased()
        }
    }

    TreeView {
        id: treeView
        model: App.playlist
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        boundsMovement: Flickable.StopAtBounds
        keyNavigationEnabled: false
        smooth: false
        pointerNavigationEnabled: false
        selectionBehavior: TableView.SelectRows
        property var currentIndex: undefined

        // Collapse filtered rows to 0 (implicitHeight isn't re-measured when hidden); negative means implicit.
        rowHeightProvider: function(row) {
            if (sideBar.filterText.length === 0) return -1
            return App.playlist.isFilteredOut(treeView.index(row, 0), sideBar.filterText) ? 0 : -1
        }

        anchors {
            top: filterRow.bottom
            right: parent.right
            left: parent.left
            bottom: parent.bottom
            topMargin: 2
        }

        ScrollBar.vertical: AppScrollBar { width: 9; minimumSize: 0.06 }

        selectionModel: ItemSelectionModel {
            id: selection
            model: App.playlist
        }

        delegate: Item {
            id: del
            implicitWidth: sideBar.width
            implicitHeight: card.height + 4
            clip: true
            visible: !filteredOut

            readonly property real indent: 20
            readonly property bool filteredOut: App.playlist.isFilteredOut(del.index, sideBar.filterText)

            required property bool isCurrentIndex
            required property string display
            required property bool isDeletable
            required property string link
            required property bool isWatched
            required property TreeView treeView
            required property bool isTreeNode
            required property bool expanded
            required property bool hasChildren
            required property int depth
            required property int row
            required property bool selected
            required property var index

            TapHandler {
                acceptedButtons: Qt.LeftButton
                acceptedModifiers: Qt.NoModifier
                onTapped: {
                    if (!del.hasChildren) {
                        Globals.mpv.pause()
                        App.playlist.loadIndex(del.index)
                        return
                    }
                    if (del.treeView.isExpanded(del.row))
                        del.treeView.collapse(del.row)
                    else {
                        del.treeView.expand(del.row)
                        sideBar.scrollToIndex(App.playlist.currentChild(del.index))
                    }
                    sideBar.treeHasExpanded = sideBar.anyExpanded()
                }
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: ctxMenu.popup()
            }

            AppMenu {
                id: ctxMenu
                modal: true
                Action { text: "Play"; enabled: !del.hasChildren; onTriggered: { Globals.mpv.pause(); App.playlist.loadIndex(del.index) } }
                Action { text: "Copy link"; enabled: del.link.length > 0; onTriggered: { App.copyToClipboard(del.link); Globals.mpv.showText("Copied link") } }
                Action { text: "Remove"; enabled: del.isDeletable; onTriggered: App.playlist.remove(del.index) }
            }

            Rectangle {
                id: card
                anchors {
                    left: parent.left
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    leftMargin: 4
                    rightMargin: 4
                }
                height: Math.max(del.hasChildren ? 34 : 40, itemText.height + 16)
                radius: 6
                color: del.selected ? Qt.alpha(Theme.accent, 0.14)
                     : cardHover.hovered ? Qt.alpha(Theme.accent, 0.08) : "transparent"
                border.color: del.selected ? Qt.alpha(Theme.accent, 0.35)
                            : del.hasChildren ? Theme.border : "transparent"
                border.width: (del.selected || del.hasChildren) ? 1 : 0
                Behavior on color { ColorAnimation { duration: 130 } }

                HoverHandler { id: cardHover }

                Rectangle {
                    visible: del.selected && !del.hasChildren
                    width: 3
                    radius: 1.5
                    color: Theme.accent
                    anchors {
                        left: parent.left
                        top: parent.top
                        bottom: parent.bottom
                        leftMargin: 1
                        topMargin: 8
                        bottomMargin: 8
                    }
                }

                AppIcon {
                    visible: del.isTreeNode && del.hasChildren
                    x: 8 + del.depth * del.indent
                    anchors.verticalCenter: parent.verticalCenter
                    name: "chevron-right"
                    size: 18
                    rotation: del.expanded ? 90 : 0
                    color: Theme.accent
                    Behavior on rotation { NumberAnimation { duration: 120 } }
                }

                Text {
                    id: itemText
                    x: del.isTreeNode ? (del.depth + 1) * del.indent + 4 : 8
                    width: card.width - x - 38
                    anchors.verticalCenter: parent.verticalCenter
                    maximumLineCount: 2
                    text: del.display
                    elide: Text.ElideRight
                    wrapMode: Text.WordWrap
                    font.pixelSize: Globals.sp(20)
                    color: del.selected       ? Theme.textAccent
                         : del.isCurrentIndex ? Theme.success
                         : del.isWatched      ? Theme.textMuted
                         : Theme.textSecondary
                }

                AppIcon {
                    visible: del.isWatched && !del.hasChildren && !del.isDeletable
                             && !del.isCurrentIndex && !cardHover.hovered
                    name: "check"
                    size: 16
                    color: Theme.textMuted
                    anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                }

                Item {
                    visible: cardHover.hovered && !del.hasChildren && !del.isDeletable && del.link.length > 0
                    width: 26; height: 26
                    anchors { right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
                    Rectangle {
                        x: 9; y: 4; width: 11; height: 13; radius: 3
                        color: "transparent"
                        border.color: copyArea.containsMouse ? Theme.accent : Theme.textMuted
                        border.width: 1.5
                    }
                    Rectangle {
                        x: 5; y: 8; width: 11; height: 13; radius: 3
                        color: Theme.surfaceDeep
                        border.color: copyArea.containsMouse ? Theme.accent : Theme.textMuted
                        border.width: 1.5
                    }
                    MouseArea {
                        id: copyArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { App.copyToClipboard(del.link); Globals.mpv.showText("Copied link") }
                    }
                }

                AppIcon {
                    visible: !del.selected && del.isDeletable
                    name: "x"
                    size: 16
                    color: delBtnArea.containsMouse ? Theme.danger : Theme.textMuted
                    anchors {
                        right: parent.right
                        rightMargin: 8
                        verticalCenter: parent.verticalCenter
                    }

                    MouseArea {
                        id: delBtnArea
                        anchors.fill: parent
                        anchors.margins: -6
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: App.playlist.remove(del.index)
                    }
                }
            }
        }
    }

}