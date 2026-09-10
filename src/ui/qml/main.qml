pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import AoNami
import "."
import "./Player"
import "./Components"
import "./Pages"

ApplicationWindow {
    id: root
    width: Globals.defaultWidth
    height: Globals.defaultHeight
    x: (Screen.desktopAvailableWidth - width) / 2
    y: (Screen.desktopAvailableHeight - height) / 2

    visible: true
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowMinimizeButtonHint
    onClosing: App.playlist.saveProgress()

    readonly property bool onPlayer: Globals.page === AppShell.Player

    color: onPlayer ? "#000000" : Theme.background
    Material.theme: Material.Dark
    Material.primary: Theme.background
    Material.accent: Theme.accent
    Material.foreground: Theme.textPrimary

    Binding { target: Theme; property: "name";         value: App.settings.themeName }
    Binding { target: Theme; property: "customAccent"; value: App.settings.accentColor }
    Binding { target: Globals; property: "uiScale";    value: App.settings.uiScale }

    // Sidebar order, which is what Ctrl+Tab follows - the page numbers would put History last.
    readonly property var navOrder: [
        AppShell.Search, AppShell.Info, AppShell.Library, AppShell.Player,
        AppShell.Download, AppShell.History, AppShell.Log, AppShell.Settings
    ]

    function stepPage(delta) {
        let at = navOrder.indexOf(Globals.page)
        if (at < 0) at = 0
        for (let i = 0; i < navOrder.length; ++i) {
            at = (at + delta + navOrder.length) % navOrder.length
            const page = navOrder[at]
            if (page === AppShell.Info && !App.show.exists) continue
            gotoPage(page)
            return
        }
    }

    property real savedX: x
    property real savedY: y
    property real savedW: width
    property real savedH: height

    function saveGeometry() {
        savedX = x
        savedY = y
        savedW = width
        savedH = height
    }

    function applyGeometry(nx, ny, nw, nh) {
        x = nx
        y = ny
        width = nw
        height = nh
    }

    function restoreGeometry() {
        applyGeometry(savedX, savedY, savedW, savedH)
    }

    function fillDesktop() {
        applyGeometry(Screen.virtualX, Screen.virtualY,
                      Screen.desktopAvailableWidth, Screen.desktopAvailableHeight)
    }

    function fillScreen() {
        applyGeometry(Screen.virtualX, Screen.virtualY, Screen.width, Screen.height)
        raise()
    }

    function toggleMaximised() {
        if (Globals.pipMode || Globals.fullscreen) return

        if (Globals.maximised) {
            Globals.maximised = false
            restoreGeometry()
        } else {
            saveGeometry()
            Globals.maximised = true
            fillDesktop()
        }
    }

    function toggleFullscreen() {
        if (Globals.pipMode) togglePip()

        if (Globals.fullscreen) {
            Globals.fullscreen = false
            if (Globals.maximised) fillDesktop()
            else                   restoreGeometry()
        } else {
            if (!Globals.maximised) saveGeometry()
            Globals.fullscreen = true
            fillScreen()
        }
    }

    function togglePip() {
        if (Globals.pipMode) {
            Globals.pipMode = false
            flags = Qt.Window | Qt.FramelessWindowHint | Qt.WindowMinimizeButtonHint

            if (Globals.fullscreen)      fillScreen()
            else if (Globals.maximised)  fillDesktop()
            else                         restoreGeometry()
        } else {
            if (!Globals.maximised && !Globals.fullscreen) saveGeometry()
            Globals.fullscreen = false
            Globals.maximised = false
            Globals.pipMode = true

            const pw = Math.round(Screen.desktopAvailableWidth * 0.33)
            const ph = Math.round(Screen.desktopAvailableHeight * 0.45)
            flags = Qt.Window | Qt.FramelessWindowHint | Qt.WindowMinimizeButtonHint | Qt.WindowStaysOnTopHint
            applyGeometry(Screen.desktopAvailableWidth - pw, Screen.desktopAvailableHeight - ph, pw, ph)
        }
    }

    function ensureFullyVisibleOnScreen() {
        x = Math.max(0, Math.min(x, Screen.desktopAvailableWidth - width))
        y = Math.max(0, Math.min(y, Screen.desktopAvailableHeight - height))
    }

    readonly property bool chromeVisible: !(Globals.pipMode || Globals.fullscreen)

    Component.onCompleted: {
        Globals.root = root

        if (App.playlist.playAt(0)) {
            root.requestActivate()
            Globals.page = AppShell.Player
            history = [AppShell.Player]
        } else if (!App.explorer.isLoading && App.explorer.count === 0) {
            App.browse(true)
        }

        deferredStartupTimer.start()
    }

    property var history: [AppShell.Search]
    property int historyIndex: 0

    property string nowPlayingTitle: ""
    property string nowPlayingEpisode: ""
    Connections {
        target: App.playlist
        function onCurrentItemChanged() {
            root.nowPlayingTitle = App.playlist.currentShowName()
            root.nowPlayingEpisode = App.playlist.currentItemName().replace(/\s*\n\s*/g, "  ")
        }
    }

    function gotoPage(page, isHistory = false) {
        if (Globals.fullscreen || Globals.page === page) return
        if (page === AppShell.Info && !App.show.exists) return

        if (page === AppShell.Player)
            Globals.mpv.peek(2000)

        Globals.page = page
        if (!isHistory) {
            history.splice(historyIndex + 1)
            history.push(page)
            historyIndex = history.length - 1
        }
    }

    // gotoPage() bails while fullscreen; stepping the index there would desync it from the page.
    function goHistory(delta) {
        if (Globals.fullscreen) return
        const at = historyIndex + delta
        if (at < 0 || at >= history.length) return
        historyIndex = at
        gotoPage(history[at], true)
    }

    Connections {
        target: Globals
        // Deferred: the target Loader activates and mpvPage's `enabled` flips in this same pass,
        // and a disabled item cannot take focus.
        function onPageChanged() { Qt.callLater(root.focusCurrentPage) }
    }

    function focusCurrentPage() {
        if (root.onPlayer) { mpvPage.forceActiveFocus(); return }
        // StackLayout shows exactly one item, so the visible loaded page is the current one.
        for (let i = 0; i < pageStack.children.length; i++) {
            const loader = pageStack.children[i] as Loader
            const page = loader ? loader.item as Item : null
            if (page && page.visible) { page.forceActiveFocus(); return }
        }
    }

    TitleBar {
        id: titleBar
        height: root.chromeVisible ? 44 : 0
        z: 6
        anchors { top: parent.top; left: parent.left; right: parent.right }

        canGoBack: root.historyIndex > 0
        canGoForward: root.historyIndex + 1 < root.history.length
        nowPlayingTitle: root.nowPlayingTitle
        nowPlayingEpisode: root.nowPlayingEpisode

        onMoveRequested: root.startSystemMove()
        onMaximiseToggled: root.toggleMaximised()
        onMinimiseRequested: root.showMinimized()
        onCloseRequested: root.close()
        onHistoryStep: (delta) => root.goHistory(delta)
        onPlayerRequested: root.gotoPage(AppShell.Player)
    }

    Item {
        id: contentArea
        anchors {
            top: titleBar.bottom
            left: parent.left
            leftMargin: root.chromeVisible ? sideBar.width : 0
            right: parent.right
            bottom: parent.bottom
        }
    }

    SideBar {
        id: sideBar
        z: 5
        chromeVisible: root.chromeVisible
        anchors {
            left: parent.left
            top: titleBar.bottom
            bottom: parent.bottom
        }
        onPageRequested: (page) => root.gotoPage(page)
    }

    Timer {
        id: deferredStartupTimer
        interval: 3000
        repeat: false
        onTriggered: App.library.fetchUnwatchedEpisodes(App.library.libraryType)
    }

    Item {
        z: 1
        anchors.fill: contentArea
        opacity: root.onPlayer ? 0 : 1
        visible: opacity > 0.001
        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

        // Cache the page as one texture during the cross-fade so it isn't re-rendered every frame.
        layer.enabled: opacity > 0.001 && opacity < 0.999

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.background }
                GradientStop { position: 1.0; color: Theme.bgBottom }
            }
        }

        StackLayout {
            id: pageStack
            anchors.fill: parent

            // The player is drawn separately, so it has no entry here.
            readonly property var pageOrder: [
                AppShell.Search, AppShell.Info, AppShell.Library,
                AppShell.Download, AppShell.Log, AppShell.Settings, AppShell.History
            ]
            currentIndex: Math.max(0, pageOrder.indexOf(Globals.page))

            Repeater {
                model: [
                    "Pages/ExplorerPage.qml",
                    "Pages/InfoPage.qml",
                    "Pages/LibraryPage.qml",
                    "Pages/DownloadPage.qml",
                    "Pages/LogPage.qml",
                    "Pages/SettingsPage.qml",
                    "Pages/HistoryPage.qml"
                ]
                delegate: Loader {
                    required property int index
                    required property string modelData
                    readonly property bool current: pageStack.currentIndex === index && !root.onPlayer
                    active: false
                    source: active ? modelData : ""
                    Component.onCompleted: if (current) active = true
                    function focusPage() {
                        const page = item as Item
                        if (page) page.forceActiveFocus()
                    }
                    onCurrentChanged: {
                        if (!current) return
                        if (!active) active = true
                        else focusPage()
                    }
                    onLoaded: if (current) focusPage()
                }
            }
        }
    }

    LoadingScreen {
        z: 100
        anchors.fill: contentArea
        // Only the pages that can start a load; the player has its own spinner in the control bar.
        readonly property bool cancellablePage: Globals.page === AppShell.Search
                                             || Globals.page === AppShell.Info
                                             || Globals.page === AppShell.Library
        loading: {
            switch (Globals.page) {
            case AppShell.Search:  return App.explorer.isLoading || App.show.isLoading
            case AppShell.Info:    return App.playlist.isLoading
            case AppShell.Library:
            case AppShell.History: return App.show.isLoading
            default: return false
            }
        }
        cancellable: cancellablePage
        onCancelled: {
            if (App.explorer.isLoading) App.explorer.cancel()
            if (App.show.isLoading)     App.show.cancel()
            if (App.playlist.isLoading) App.playlist.cancel()
        }
    }

    MpvPage {
        id: mpvPage
        // Player page and PiP only, or right-clicks elsewhere reach the player's context menu.
        enabled: root.onPlayer || Globals.pipMode
        anchors.fill: contentArea
    }

    QuickSearch {
        id: quickSearch
        anchors.fill: parent
        z: 200
        onSearched: (query) => {
            Globals.lastSearch = query
            App.search(query)
            root.gotoPage(AppShell.Search)
        }
    }

    Notifier {
        id: notifier
        onLogsRequested: root.gotoPage(AppShell.Log)
        onClosed: {
            if (mpvPage.visible) mpvPage.forceActiveFocus()
            else root.focusCurrentPage()
        }
    }

    // Above every other item, chrome included, so nothing of the app shows through.
    Image {
        id: bossScreen
        anchors.fill: parent
        z: 1000
        visible: false
        fillMode: Image.PreserveAspectCrop
        source: "qrc:/AoNami/resources/images/periodic-table.jpg"
    }

    Connections {
        target: AppShell
        function onErrorReported(message, header) { notifier.show(message, header, true) }
        function onInfoReported(message, header)  { notifier.show(message, header, false) }
        function onNavigateRequested(page)        { root.gotoPage(page) }
        function onHistoryStepRequested(delta)    { root.goHistory(delta) }
    }

    Shortcut { sequence: "Alt+Right"; onActivated: root.goHistory(1) }
    Shortcut { sequence: "Alt+Left";  onActivated: root.goHistory(-1) }
    Shortcut { sequence: "Ctrl+Tab";       onActivated: root.stepPage(1) }
    Shortcut { sequence: "Ctrl+Shift+Tab"; onActivated: root.stepPage(-1) }

    Shortcut { sequence: "Ctrl+W"; onActivated: root.close() }
    Shortcut {
        sequence: "Ctrl+R"
        enabled: Globals.page === AppShell.Info && App.show.exists
        onActivated: App.reloadShow()
    }
    Shortcut { sequence: "Ctrl+K"; onActivated: quickSearch.open = !quickSearch.open }
    Shortcut { sequence: "1"; onActivated: root.gotoPage(AppShell.Search) }
    Shortcut { sequence: "2"; onActivated: root.gotoPage(AppShell.Info) }
    Shortcut { sequence: "3"; onActivated: root.gotoPage(AppShell.Library) }
    Shortcut { sequence: "4"; onActivated: root.gotoPage(AppShell.Player) }
    Shortcut { sequence: "5"; onActivated: root.gotoPage(AppShell.Download) }
    Shortcut { sequence: "6"; onActivated: root.gotoPage(AppShell.Log) }

    Shortcut {
        sequence: "Ctrl+Q"
        onActivated: {
            if (Globals.pipMode) root.togglePip()
            if (Globals.maximised) root.toggleMaximised()
            if (Globals.fullscreen) root.toggleFullscreen()
            bossScreen.visible = true
            root.lower()
            root.showMinimized()
            if (Globals.mpv) Globals.mpv.pause()
        }
    }
    Shortcut {
        sequence: "Ctrl+Space"
        onActivated: bossScreen.visible = !bossScreen.visible
    }
}
