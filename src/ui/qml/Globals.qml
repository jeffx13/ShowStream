pragma Singleton
import QtQml
import QtQuick
import AoNami

QtObject {

    property bool maximised: false
    property bool fullscreen: false
    property bool pipMode: false

    property real appWidth:  root ? root.width  : 0
    property real appHeight: root ? root.height : 0

    property int page: AppShell.Search

    property var root: null
    property var mpv: null

    property real libraryLastContentY: 0
    property real explorerLastContentY: 0
    property string lastSearch: ""
    property real imageAspectRatio: 319 / 225

    readonly property int defaultWidth: 1080
    readonly property int defaultHeight: 720

    property real uiScale: 1.0

    // Quick Controls styles apply their own default font, so controls have to name the app's.
    readonly property string fontFamily: Application.font.family

    function sp(n) {
        return Math.round(n * uiScale)
    }

    function gotoPage(page)     { if (root) root.gotoPage(page) }
    function togglePip()        { if (root) root.togglePip() }
    function toggleFullscreen() { if (root) root.toggleFullscreen() }
}
