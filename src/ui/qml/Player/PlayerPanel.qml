pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../Components"
import AoNami
import ".."

Popup {
    id: panel
    required property MpvPlayer player
    visible: false
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // `page` indexes the StackLayout below; a null model marks a tab that is a page, not a list.
    readonly property int trackPage: 0
    readonly property var allTabs: [
        { id: "servers", label: "Servers", model: App.playlist.serverList,   page: 3 },
        { id: "video",   label: "Video",   model: panel.player.videoList,    page: panel.trackPage },
        { id: "audio",   label: "Audio",   model: panel.player.audioList,    page: panel.trackPage },
        { id: "subs",    label: "Subs",    model: panel.player.subtitleList, page: panel.trackPage },
        { id: "general", label: "General", model: null,                      page: 1 },
        { id: "skip",    label: "Skip",    model: null,                      page: 2 }
    ]

    readonly property var visibleTabs: allTabs.filter(function(tab) {
        if (!tab.model) return true
        // Subs stays with no tracks: that is exactly when you want to go looking.
        if (tab.id === "subs") return true
        // One server means nothing to pick.
        return tab.model.count > (tab.id === "servers" ? 1 : 0)
    })

    property int activeTabIndex: 0
    property string activeTabId: "servers"

    property int subsSubPage: 0   // 0 = the player's tracks, 1 = SubDL search
    onSubsSubPageChanged: if (subsSubPage === 1) subtitleSearchTab.refresh()
    readonly property var activeTab: (activeTabIndex >= 0 && activeTabIndex < visibleTabs.length)
                                     ? visibleTabs[activeTabIndex] : null

    function syncActiveTab() {
        let i = -1
        for (let k = 0; k < visibleTabs.length; k++)
            if (visibleTabs[k].id === activeTabId) { i = k; break }
        if (i < 0)
            for (let k = 0; k < visibleTabs.length; k++)
                if (visibleTabs[k].id === "video") { i = k; break }
        activeTabIndex = i >= 0 ? i : 0
    }
    onVisibleTabsChanged: syncActiveTab()

    background: Rectangle {
        radius: 18
        color: Theme.overlayScrim
        border.color: Theme.overlayFillHover
        border.width: 1

        component EdgeGlow: Rectangle {
            id: glow
            property color tint: Theme.accentStrong
            height: 1
            radius: 1
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.5; color: glow.tint }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        EdgeGlow {
            anchors { top: parent.top; left: parent.left; right: parent.right
                      topMargin: 1; leftMargin: 20; rightMargin: 20 }
        }
        EdgeGlow {
            tint: Theme.accentSoft
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right
                      bottomMargin: 1; leftMargin: 40; rightMargin: 40 }
        }
    }

    // Opacity-only - a scale animation re-rasterises the panel over the live video each frame.
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 160; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 120; easing.type: Easing.InCubic }
    }

    onOpened: {
        syncActiveTab()
        Qt.callLater(() => { trackTab.centerCurrent(); serverTab.centerCurrent() })
    }

    contentItem: ColumnLayout {
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 10

            Rectangle {
                anchors.fill: parent
                radius: 12
                color: Theme.overlayFillSoft

                Row {
                    id: tabRow
                    anchors {
                        fill: parent
                        margins: 3
                    }
                    spacing: 2
                    readonly property real tabWidth:
                        (width - (panel.visibleTabs.length - 1) * spacing) / panel.visibleTabs.length

                    Repeater {
                        // The array itself, not its length: indexing visibleTabs from the delegate
                        // reads past the end while a shrunk list still has its old delegates.
                        model: panel.visibleTabs
                        delegate: AbstractButton {
                            id: tab
                            required property var modelData
                            required property int index
                            readonly property bool isActive: panel.activeTabIndex === tab.index
                            readonly property int itemCount: tab.modelData.model ? tab.modelData.model.count : -1

                            width: tabRow.tabWidth
                            height: tabRow.height
                            focusPolicy: Qt.NoFocus
                            onClicked: { panel.activeTabIndex = tab.index; panel.activeTabId = tab.modelData.id }

                            background: Rectangle {
                                radius: 10
                                color: tab.isActive ? Theme.accent : (tab.hovered ? Theme.overlayFill : "transparent")
                                Behavior on color { ColorAnimation { duration: 120 } }

                                Rectangle {
                                    visible: tab.isActive
                                    anchors {
                                        top: parent.top
                                        left: parent.left
                                        right: parent.right
                                        topMargin: 1
                                        leftMargin: 8
                                        rightMargin: 8
                                    }
                                    height: 1
                                    radius: 1
                                    color: Theme.overlayFillActive
                                }
                            }

                            contentItem: Item {
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 5
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: tab.modelData.label
                                        font.pixelSize: Globals.sp(18)
                                        font.bold: tab.isActive
                                        color: tab.isActive ? Theme.onOverlay : (tab.hovered ? Theme.onOverlayDim : Theme.onOverlayFaint)
                                        elide: Text.ElideRight
                                        width: Math.min(implicitWidth, tab.width - 14 - (tabCount.visible ? tabCount.width + 5 : 0))
                                        Behavior on color { ColorAnimation { duration: 120 } }
                                    }
                                    Rectangle {
                                        id: tabCount
                                        visible: tab.itemCount >= 0
                                        anchors.verticalCenter: parent.verticalCenter
                                        // Min width fits two digits, so 9 -> 10 doesn't elide the label.
                                        width: Math.max(20, tabCountText.implicitWidth + 8)
                                        height: 18
                                        radius: 9
                                        color: tab.isActive ? Theme.overlayFillActive : Theme.overlayFill
                                        Text {
                                            id: tabCountText
                                            anchors.centerIn: parent
                                            text: tab.itemCount
                                            font.pixelSize: Globals.sp(14)
                                            font.weight: Font.Medium
                                            color: tab.isActive ? Theme.onOverlay : Theme.onOverlayFaint
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                height: 3
                radius: 1.5
                color: Theme.accent
                y: parent.height - 1

                // Uniform tabs, so the active one's box follows from the index; picking the
                // delegate out of tabRow.children relies on where Repeater stacks itself.
                x: tabRow.x + panel.activeTabIndex * (tabRow.tabWidth + tabRow.spacing) + 12
                width: Math.max(0, tabRow.tabWidth - 24)

                Behavior on x     { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

                Rectangle {
                    anchors {
                        horizontalCenter: parent.horizontalCenter
                        top: parent.top
                    }
                    width: parent.width * 0.6
                    height: 6
                    radius: 3
                    color: Theme.accentMuted
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 10
            spacing: 8
            visible: panel.activeTab && panel.activeTab.id === "subs"

            Repeater {
                model: [qsTr("Tracks"), qsTr("Search")]
                delegate: Rectangle {
                    id: subTab
                    required property string modelData
                    required property int index
                    readonly property bool selected: panel.subsSubPage === index

                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    radius: 9
                    color: selected            ? Qt.alpha(Theme.accent, 0.22)
                         : subTabHover.hovered ? Qt.alpha(Theme.onOverlay, 0.07)
                                               : "transparent"
                    Behavior on color { ColorAnimation { duration: 120 } }

                    Text {
                        anchors.centerIn: parent
                        text: subTab.modelData
                        color: subTab.selected ? Theme.accent : Theme.onOverlayMuted
                        font.pixelSize: Globals.sp(18)
                        font.bold: subTab.selected
                    }

                    HoverHandler { id: subTabHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: panel.subsSubPage = subTab.index }
                }
            }
        }

        // Without this a fetched subtitle can hold a slot with no row on either page to clear it.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 8
            spacing: 8
            visible: panel.activeTab && panel.activeTab.id === "subs"
                     && (panel.player.primarySubId !== 0 || panel.player.secondarySubId !== 0)

            Repeater {
                model: [1, 2]
                delegate: Rectangle {
                    id: slotChip
                    required property int modelData
                    readonly property int slotId: modelData === 1 ? panel.player.primarySubId
                                                                  : panel.player.secondarySubId
                    visible: slotId !== 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    radius: 8
                    color: Qt.alpha(Theme.accent, 0.14)

                    RowLayout {
                        anchors { fill: parent; leftMargin: 9; rightMargin: 6 }
                        spacing: 6

                        Text {
                            text: slotChip.modelData
                            color: Theme.accent
                            font.pixelSize: Globals.sp(13)
                            font.bold: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: panel.player.subNameForId(slotChip.slotId)
                            color: Theme.onOverlayMuted
                            font.pixelSize: Globals.sp(14)
                            elide: Text.ElideMiddle
                        }
                        AppIcon {
                            name: "x"
                            size: 13
                            color: clearOne.hovered ? Theme.danger : Theme.onOverlayDim
                            HoverHandler { id: clearOne; cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                onTapped: slotChip.modelData === 1 ? panel.player.setPrimarySub(0)
                                                                   : panel.player.setSecondarySub(0)
                            }
                        }
                    }
                }
            }

            Text {
                text: qsTr("Clear")
                color: clearBoth.hovered ? Theme.accent : Theme.onOverlayDim
                font.pixelSize: Globals.sp(14)
                visible: panel.player.primarySubId !== 0 && panel.player.secondarySubId !== 0
                HoverHandler { id: clearBoth; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: panel.player.clearSubs() }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8
            currentIndex: {
                if (!panel.activeTab) return panel.trackPage
                if (panel.activeTab.id === "subs" && panel.subsSubPage === 1) return 4
                return panel.activeTab.page
            }

            // One list for all three track kinds - the tab bar swaps its model.
            TrackTab {
                id: trackTab
                readonly property var tab: (panel.activeTab && panel.activeTab.page === panel.trackPage)
                                           ? panel.activeTab : null
                player: panel.player
                kind: tab ? tab.id : ""
                trackModel: tab ? tab.model : null
            }

            PlaybackTab { player: panel.player }

            SkipTab { player: panel.player }

            ServerTab { id: serverTab }

            SubtitleSearchTab { id: subtitleSearchTab }
        }
    }
}
