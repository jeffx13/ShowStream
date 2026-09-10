pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../Components"
import AoNami
import ".."

Item {
    id: explorerPage
    focus: true

    // Browse mode for the toolbar's active-state highlight: 0 Latest, 1 Popular, 2 Search.
    property int browseMode: 0

    // The search field owns the focus in this toolbar; nothing else may take it.
    component ToolbarButton: AppButton {
        fontSize: 20
        radius: 10
        focusPolicy: Qt.NoFocus
        focus: false
        activeFocusOnTab: false
        Layout.fillHeight: true
        leftPadding: 16
        rightPadding: 16
    }

    component ToolbarComboBox: AppComboBox {
        fontSize: 20
        focus: false
        activeFocusOnTab: false
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 2
    }

    function search() {
        let q = searchTextField.text.trim()
        if (q.length > 0)
            App.settings.prependToHistory("search/history", q)
        historyPopup.close()
        explorerPage.forceActiveFocus()
        if (q.length > 0) { browseMode = 2; App.search(q) }
        else              { browseMode = 1; App.browse(false) }
    }

    HoverHandler {
        cursorShape: Qt.ArrowCursor
    }

    Component.onDestruction: {
        Globals.lastSearch = searchTextField.text
        Globals.explorerLastContentY = gridView.contentY
    }

    AppPopup {
        id: historyPopup
        parent: Overlay.overlay
        modal: false
        padding: 6
        margins: 0
        backgroundRadius: 10

        property var historyItems: []

        contentItem: Column {
            spacing: 0

            ListView {
                id: historyList
                model: historyPopup.historyItems
                width: parent.width
                height: implicitHeight
                implicitHeight: Math.min(count * 36, 252)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                spacing: 2

                delegate: AbstractButton {
                    id: historyBtn
                    required property string modelData
                    width: historyList.width
                    height: 34
                    focusPolicy: Qt.NoFocus

                    background: Rectangle {
                        radius: 6
                        color: historyBtn.hovered ? Theme.border : "transparent"
                        Behavior on color { ColorAnimation { duration: 80 } }
                    }

                    contentItem: RowLayout {
                        anchors { fill: parent; leftMargin: 10; rightMargin: 4 }
                        spacing: 8
                        AppIcon {
                            name: "search"
                            size: 16
                            color: Theme.textMuted
                        }
                        Text {
                            Layout.fillWidth: true
                            text: historyBtn.modelData
                            color: Theme.textSecondary
                            font.pixelSize: Globals.sp(20)
                            elide: Text.ElideRight
                        }
                        AppIcon {
                            name: "arrow-up-left"
                            size: 16
                            color: historyBtn.hovered ? Theme.accent : Theme.textMuted
                            Behavior on color { ColorAnimation { duration: 80 } }
                        }
                        IconButton {
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            iconName: "x"
                            iconSize: 13
                            boxRadius: 4
                            hoverColor: Qt.alpha(Theme.accent, 0.12)
                            iconColor: Theme.textMuted
                            onClicked: {
                                App.settings.removeFromHistory("search/history", historyBtn.modelData)
                                let h = App.settings.value("search/history", [])
                                if (h.length === 0) {
                                    historyPopup.close()
                                } else {
                                    historyPopup.historyItems = h
                                }
                            }
                        }
                    }

                    onClicked: {
                        searchTextField.text = historyBtn.modelData
                        historyPopup.close()
                        explorerPage.search()
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.border
                visible: historyPopup.historyItems.length > 0
            }

            AbstractButton {
                id: clearAllBtn
                width: parent.width
                height: 32
                focusPolicy: Qt.NoFocus

                background: Rectangle {
                    radius: 6
                    color: clearAllBtn.hovered ? Theme.border : "transparent"
                    Behavior on color { ColorAnimation { duration: 80 } }
                }

                contentItem: Text {
                    text: "Clear History"
                    color: clearAllBtn.hovered ? Theme.danger : Theme.textMuted
                    font.pixelSize: Globals.sp(16)
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    Behavior on color { ColorAnimation { duration: 80 } }
                }

                onClicked: {
                    App.settings.clearHistory("search/history")
                    historyPopup.close()
                }
            }
        }
    }

    Card {
        id: searchBarCard
        height: Math.max(48, parent.height * 0.065)
        radius: 14
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            topMargin: 10
            leftMargin: 8
            rightMargin: 8
        }

        RowLayout {
            anchors {
                fill: parent
                margins: 6
            }
            spacing: 6

            AppTextField {
                id: searchTextField
                color: Theme.textPrimary
                placeholderText: qsTr("Enter query!")
                placeholderTextColor: Theme.textMuted
                text: Globals.lastSearch
                fontSize: 20
                showClearButton: true
                focusPolicy: Qt.NoFocus
                focus: false
                activeFocusOnTab: false
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.preferredWidth: 5
                onAccepted: explorerPage.search()
                onActiveFocusChanged: {
                    if (activeFocus) {
                        let h = App.settings.value("search/history", [])
                        if (h.length > 0) {
                            historyPopup.historyItems = h
                            let p = searchTextField.mapToItem(Overlay.overlay, 0, searchTextField.height)
                            historyPopup.x = p.x
                            historyPopup.y = p.y + 4
                            historyPopup.width = searchTextField.width
                            historyPopup.open()
                        }
                    } else {
                        historyPopup.close()
                    }
                }
            }

            ToolbarButton {
                text: "Search"
                onClicked: explorerPage.search()
            }

            ToolbarButton {
                text: "Latest"
                backgroundDefaultColor: explorerPage.browseMode === 0 ? Theme.accent : Theme.border
                onClicked: { explorerPage.forceActiveFocus(); explorerPage.browseMode = 0; App.browse(true) }
            }

            ToolbarButton {
                text: "Popular"
                backgroundDefaultColor: explorerPage.browseMode === 1 ? Theme.accent : Theme.border
                onClicked: { explorerPage.forceActiveFocus(); explorerPage.browseMode = 1; App.browse(false) }
            }

            ToolbarComboBox {
                id: providerComboBox
                text: "text"
                model: App.providers
                currentIndex: App.providers.currentIndex
                onActivated: (index) => App.providers.currentIndex = index
            }

            ToolbarComboBox {
                text: ""
                model: App.providers.showTypes
                currentIndex: App.providers.currentTypeIndex
                currentIndexColor: Qt.alpha(Theme.accent, 0.25)
                onActivated: (index) => App.providers.currentTypeIndex = index
            }
        }
    }

    MediaGridView {
        id: gridView
        model: App.explorer
        focus: false
        anchors {
            top: searchBarCard.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            rightMargin: 20
            topMargin: 6
        }
        imageAspectRatio: Globals.imageAspectRatio
        Component.onCompleted: contentY = Globals.explorerLastContentY

        readonly property real fetchThreshold: height * 1.0

        function tryFetchMore() {
            if (!App.explorer.canFetchMore()) return
            if (gridView.height <= 0) return

            let distanceFromBottom = contentHeight - (contentY + height)
            if (contentHeight <= height || distanceFromBottom < fetchThreshold) {
                App.explorer.fetchMore()
            }
        }

        onContentYChanged: tryFetchMore()
        onContentHeightChanged: tryFetchMore()
        onHeightChanged: tryFetchMore()

        Connections {
            target: App.explorer
            function onIsLoadingChanged() {
                if (!App.explorer.isLoading) {
                    fetchDebounce.restart()
                }
            }
        }

        Timer {
            id: fetchDebounce
            interval: 50
            repeat: false
            onTriggered: gridView.tryFetchMore()
        }

        ScrollBar.vertical: AppScrollBar {
            parent: gridView.parent
            anchors { top: gridView.top; left: gridView.right; bottom: gridView.bottom }
            width: 6
            barOpacity: 0.4
        }

        onImageAspectRatioChanged: {
            let lastContentY = contentY
            App.explorer.reset()
            contentY = lastContentY
        }

        delegate: ShowItem {
            id: showTile
            required property string title
            required property string link
            required property string cover
            required property string latestTxt
            required property int index

            showTitle: title
            showCover: cover
            badgeText: latestTxt
            width: gridView.cellWidth
            height: gridView.cellHeight
            aspectRatio: Globals.imageAspectRatio

            // Library membership has no per-row notifier; bump this to re-evaluate the badge.
            property int libraryRevision: 0
            libraryType: { showTile.libraryRevision; return App.library.libraryTypeOf(showTile.link) }

            Connections {
                target: App.library
                function onLibraryChanged() { showTile.libraryRevision++ }
                function onModelReset()     { showTile.libraryRevision++ }
            }

            onImageLoaded: (sourceAspectRatio) => {
                if (index !== 0) return
                if (Math.abs(Globals.imageAspectRatio - sourceAspectRatio) < 0.01) return
                Globals.imageAspectRatio = sourceAspectRatio
            }

            onImageClicked: (mouse) => {
                explorerPage.forceActiveFocus()
                if (mouse.button === Qt.LeftButton) {
                    App.loadShow(index, false)
                } else if (mouse.button === Qt.RightButton) {
                    contextMenu.index = index
                    contextMenu.libraryType = App.library.libraryTypeOf(link)
                    contextMenu.link = link
                    contextMenu.popup()
                } else if (mouse.button === Qt.MiddleButton) {
                    App.appendToPlaylists(index, false, false)
                }
            }

            onPlayClicked: {
                explorerPage.forceActiveFocus()
                App.appendToPlaylists(index, false, true)
            }
            onAddClicked: {
                explorerPage.forceActiveFocus()
                contextMenu.index = index
                contextMenu.libraryType = App.library.libraryTypeOf(link)
                contextMenu.link = link
                contextMenu.popup()
            }
        }

        add: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1.0
                duration: 400
            }
            NumberAnimation {
                property: "scale"
                from: 0
                to: 1.0
                duration: 400
            }
        }

        displaced: Transition {
            NumberAnimation {
                properties: "x,y"
                duration: 400
                easing.type: Easing.OutBounce
            }
            NumberAnimation {
                property: "opacity"
                to: 1.0
            }
            NumberAnimation {
                property: "scale"
                to: 1.0
            }
        }
    }

    AppMenu {
        id: contextMenu
        modal: true
        property int index
        property int libraryType
        property string link

        Action {
            text: "Play"
            onTriggered: App.appendToPlaylists(contextMenu.index, false, true)
        }

        Action {
            text: "Queue"
            onTriggered: App.appendToPlaylists(contextMenu.index, false, false)
        }

        LibraryTypeMenu {
            currentType: contextMenu.libraryType
            onPicked: (type) => App.addToLibrary(contextMenu.index, type)
        }

        Action {
            text: "Remove From Library"
            enabled: contextMenu.libraryType !== -1
            onTriggered: App.library.remove(contextMenu.link)
        }
    }

    TapHandler {
        onTapped: explorerPage.forceActiveFocus()
    }

    Keys.enabled: true
    Keys.onPressed: event => {
        if (event.modifiers & Qt.ControlModifier) {
            if (event.key === Qt.Key_R) App.explorer.reload()
        } else {
            switch (event.key) {
                case Qt.Key_Escape:
                case Qt.Key_Alt:
                if (searchTextField.activeFocus) explorerPage.forceActiveFocus()
                break

                case Qt.Key_Tab:
                providerComboBox.popup.close()
                App.providers.cycle()
                event.accepted = true
                break

                case Qt.Key_Enter:
                case Qt.Key_Return:
                if (gridView.currentIndex >= 0 && !searchTextField.activeFocus) {
                    App.loadShow(gridView.currentIndex, false)
                    event.accepted = true
                } else {
                    search()
                }
                break

                case Qt.Key_Slash:
                searchTextField.forceActiveFocus()
                event.accepted = true
                break

                case Qt.Key_P:
                explorerPage.browseMode = 1
                App.browse(false)
                break

                case Qt.Key_L:
                explorerPage.browseMode = 0
                App.browse(true)
                break

                case Qt.Key_Left:
                case Qt.Key_Right:
                case Qt.Key_Up:
                case Qt.Key_Down:
                gridView.moveCursor(event.key)
                event.accepted = true
                break
            }
        }
    }
}
