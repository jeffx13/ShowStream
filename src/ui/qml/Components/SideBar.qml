pragma ComponentBehavior: Bound
import QtQuick
import ".."
import AoNami

Rectangle {
    id: sideBar

    property bool chromeVisible: true
    signal pageRequested(int page)

    readonly property int rail: 56
    property bool locked: false
    property bool lockedExpanded: false
    property bool hoverExpanded: false
    readonly property bool expanded: locked ? lockedExpanded : hoverExpanded

    function requestExpand() { if (!locked) expandTimer.restart() }

    visible: width > 0
    focus: false
    clip: true
    width: chromeVisible ? (expanded ? 160 : rail) : 0
    Behavior on width { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }

    gradient: Gradient {
        GradientStop { position: 0.0; color: Theme.surface }
        GradientStop { position: 1.0; color: Theme.surfaceDeep }
    }

    Rectangle {
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
        width: 1
        color: Theme.border
    }

    HoverHandler { onHoveredChanged: if (!hovered) { expandTimer.stop(); sideBar.hoverExpanded = false } }
    Timer { id: expandTimer; interval: 200; onTriggered: if (!sideBar.locked) sideBar.hoverExpanded = true }

    component SideItem: Item {
        id: si
        property int page: 0
        property string icon: ""
        property string label: ""
        property bool needsShow: false
        width: parent ? parent.width : 0
        height: 52
        readonly property bool isSelected: Globals.page === page
        readonly property bool isEnabled: needsShow ? App.show.exists : true

        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: 6; anchors.rightMargin: 6
            anchors.topMargin: 4; anchors.bottomMargin: 4
            radius: 10
            color: si.isSelected     ? Qt.alpha(Theme.accent, 0.15)
                 : itemHover.hovered ? Qt.alpha(Theme.textPrimary, 0.06) : "transparent"
            border.color: si.isSelected ? Theme.accent : "transparent"
            border.width: si.isSelected ? 1 : 0
            Behavior on color { ColorAnimation { duration: 140 } }
        }
        SideBarIcon {
            glyph: si.icon
            active: si.isSelected
            size: 34
            x: (sideBar.rail - width) / 2
            anchors.verticalCenter: parent.verticalCenter
            opacity: si.isEnabled ? 1.0 : 0.35
            Behavior on opacity { NumberAnimation { duration: 140 } }
        }
        Text {
            anchors { left: parent.left; leftMargin: sideBar.rail; right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
            text: si.label
            color: si.isSelected ? Theme.textPrimary : Theme.textSecondary
            font.pixelSize: Globals.sp(18)
            elide: Text.ElideRight
            opacity: sideBar.expanded ? (si.isEnabled ? 1.0 : 0.4) : 0.0
            visible: opacity > 0.01
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
        HoverHandler { id: itemHover; onHoveredChanged: if (hovered) sideBar.requestExpand() }
        MouseArea {
            anchors.fill: parent
            cursorShape: si.isEnabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor
            onClicked: if (si.isEnabled) sideBar.pageRequested(si.page)
        }
        AppToolTip { text: si.label; visible: itemHover.hovered && !sideBar.expanded }
    }

    Column {
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 10 }
        spacing: 2

        Item {
            width: parent.width
            height: 40
            Rectangle {
                width: 34; height: 34; radius: 9
                anchors.verticalCenter: parent.verticalCenter
                x: (sideBar.rail - width) / 2
                color: sideBar.locked ? Qt.alpha(Theme.accent, 0.18) : (pinHover.hovered ? Qt.alpha(Theme.textPrimary, 0.07) : "transparent")
                border.color: sideBar.locked ? Theme.accent : "transparent"
                border.width: 1
                AppIcon { anchors.centerIn: parent; name: "pin"; size: 17; color: sideBar.locked ? Theme.accent : Theme.textSecondary; opacity: sideBar.locked ? 1.0 : 0.7 }
                HoverHandler { id: pinHover }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (sideBar.locked) {
                            sideBar.locked = false
                        } else {
                            sideBar.lockedExpanded = sideBar.expanded
                            sideBar.locked = true
                        }
                    }
                }
                AppToolTip { text: sideBar.locked ? qsTr("Unlock sidebar") : (sideBar.expanded ? qsTr("Lock open") : qsTr("Lock collapsed")); visible: pinHover.hovered }
            }
        }

        SideItem { page: AppShell.Search;   icon: "search";     label: "Explore" }
        SideItem { page: AppShell.Info;     icon: "star";       label: "Details"; needsShow: true }
        SideItem { page: AppShell.Library;  icon: "list-video"; label: "Library" }
        SideItem { page: AppShell.Player;   icon: "tv";         label: "Player" }
        SideItem { page: AppShell.Download; icon: "download";   label: "Downloads" }
    }

    Column {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; bottomMargin: 10 }
        spacing: 2
        SideItem { page: AppShell.History;  icon: "history";     label: "History" }
        SideItem { page: AppShell.Log;      icon: "list-checks"; label: "Logs" }
        SideItem { page: AppShell.Settings; icon: "settings";    label: "Settings" }
    }
}
